/*
 *  main.c
 *
 *  Zybo Z7-20 + Pcam 5C (OV5640, MIPI CSI-2) -> DDR -> HDMI, 720p60.
 *  Bring-up test program. Plain C.
 *
 *  This is the MIPI counterpart of the PZ7020 DVP test program, and it is
 *  laid out the same way on purpose : bring up the bus, bring up the sensor,
 *  bring up the video path, then sit in a super-loop. Read them side by side.
 *
 *  ---------------------------------------------------------------------
 *  THE ONE THING THAT IS GENUINELY DIFFERENT : THE BRING-UP ORDER
 *
 *  On PZ7020 the order barely mattered. The DVP bus is a clock and some data
 *  lines; if the capture RTL starts late it simply misses a frame and picks
 *  up the next one.
 *
 *  MIPI CSI-2 is a packet protocol. The receiver has to see a packet header
 *  to know where a packet starts. Join the stream in the middle and it never
 *  recovers on its own. So the sequence below is not stylistic :
 *
 *      reset      : consumer -> producer     (VDMA, CSI-2, D-PHY, sensor)
 *      configure  : everything, still halted
 *      enable     : producer -> consumer     (D-PHY, CSI-2, ... , sensor last)
 *
 *  The sensor is woken LAST, by OV5640_SetMode720p(). By then every stage
 *  downstream is already running and waiting. Move that call earlier and you
 *  get a black screen that survives a reset button but not a power cycle -
 *  one of the more annoying faults to diagnose from scratch.
 *
 *  ---------------------------------------------------------------------
 *  IF NOTHING APPEARS, PRESS 't' FIRST
 *
 *  't' turns on the sensor's internal colour bar generator. It splits the
 *  problem in half in one keystroke :
 *
 *      bars visible  -> SCCB, PLL, D-PHY, CSI-2, demosaic, VDMA, VTC, HDMI
 *                       are all fine. Look at the lens, the cap, the light.
 *      bars missing  -> the fault is in that chain, and the lens is
 *                       irrelevant. Start at 'i'.
 *
 *  Terminal : 115200 8N1.
 */

#include "xil_types.h"
#include "xparameters.h"
#include "xil_printf.h"
#include "xuartps_hw.h"
#include "sleep.h"

#include "xaxivdma.h"

#include "cam_gpio/cam_gpio.h"
#include "iic_sccb_cfg/iic_sccb_cfg.h"
#include "ov5640/OV5640.h"
#include "mipi_rx/mipi_rx.h"
#include "gamma/gamma.h"
#include "vdma_api/vdma_api.h"
#include "display_ctrl_hdmi/display_ctrl.h"

/*===========================================================================
 *  Platform glue
 *===========================================================================*/
#define VDMA_ID         XPAR_AXIVDMA_0_DEVICE_ID
#define DISP_VTC_ID     XPAR_VTC_0_DEVICE_ID

/*
 *  Frame buffer at DDR base + 160 MB.
 *
 *  Digilent's demo uses this same offset. It is far above anything the .elf,
 *  stack or heap will reach, which matters because DDR has no partitions -
 *  the only thing keeping code and pixels apart is this constant. Three
 *  720p RGB888 frames are 3 x 1280 x 720 x 3 = 8.3 MB, so there is room to
 *  spare in the board's 1 GB.
 */
#define FRAME_BUFFER_ADDR   (XPAR_PS7_DDR_0_S_AXI_BASEADDR + 0x0A000000)

XAxiVdma     vdma;
DisplayCtrl  dispCtrl;
VideoMode    vd_mode;

/* Menu state */
static Camera_pattern cur_pattern = PATTERN_OFF;
static Camera_awb     cur_awb     = AWB_ADVANCED;
static Camera_state   cur_vflip   = OFF;
static Camera_state   cur_mirror  = OFF;
static Gamma_factor   cur_gamma   = GAMMA_1_1_8;

/*===========================================================================
 *  A very small non-blocking menu
 *
 *  Kept in this file rather than a cam_menu module because there are only
 *  nine keys. The PZ7020 project splits it out because its menu drives the
 *  whole sensor ISP and runs to hundreds of lines; there is no such surface
 *  here, for the reason explained in OV5640.h - we tap the sensor BEFORE its
 *  ISP, so most of those controls would do nothing.
 *===========================================================================*/
static void menu_help(void)
{
    xil_printf("\r\n--- keys ---------------------------------------\r\n");
    xil_printf("  t  test pattern (colour bars) on/off   [%s]\r\n",
               cur_pattern == PATTERN_COLOR_BAR ? "ON " : "off");
    xil_printf("  w  auto white balance : adv / simple / off  [%d]\r\n",
               (int)cur_awb);
    xil_printf("  v  vertical flip on/off                [%s]\r\n",
               cur_vflip == ON ? "ON " : "off");
    xil_printf("  m  horizontal mirror on/off            [%s]\r\n",
               cur_mirror == ON ? "ON " : "off");
    xil_printf("  g  gamma factor : 1.0 / 1.2 / 1.5 / 1.8 / 2.2  [%s]\r\n",
               gamma_name(cur_gamma));
    xil_printf("  i  dump key sensor registers\r\n");
    xil_printf("  c  dump MIPI receiver versions\r\n");
    xil_printf("  e  show and clear the SCCB error count\r\n");
    xil_printf("  ?  this help\r\n");
    xil_printf("------------------------------------------------\r\n");
}

static int key_available(void)
{
    return XUartPs_IsReceiveData(STDIN_BASEADDRESS);
}

static void menu_run(void)
{
    char c;

    if (!key_available()) {
        return;                 /* non-blocking : nothing waiting, go home */
    }

    c = (char)XUartPs_RecvByte(STDIN_BASEADDRESS);

    switch (c) {

    case 't':
        cur_pattern = (cur_pattern == PATTERN_OFF)
                    ? PATTERN_COLOR_BAR : PATTERN_OFF;
        OV5640_ShowTestPattern(cur_pattern);
        xil_printf("test pattern : %s\r\n",
                   cur_pattern == PATTERN_OFF ? "off" : "colour bars");
        break;

    case 'w':
        cur_awb = (Camera_awb)((cur_awb + 1) % 3);
        OV5640_SetAWB(cur_awb);
        xil_printf("AWB : %s\r\n",
                   cur_awb == AWB_ADVANCED ? "advanced" :
                   cur_awb == AWB_SIMPLE   ? "simple"   : "disabled");
        break;

    case 'v':
        cur_vflip = (cur_vflip == ON) ? OFF : ON;
        OV5640_FlipVertical(cur_vflip);
        xil_printf("vertical flip : %s\r\n", cur_vflip == ON ? "on" : "off");
        xil_printf("  note : this also shifts the Bayer phase, so the\r\n"
                   "         colours may go wrong. That is expected here -\r\n"
                   "         the demosaic lives in the PL, not the sensor.\r\n");
        break;

    case 'm':
        cur_mirror = (cur_mirror == ON) ? OFF : ON;
        OV5640_MirrorHorizontal(cur_mirror);
        xil_printf("horizontal mirror : %s\r\n",
                   cur_mirror == ON ? "on" : "off");
        break;

    case 'g':
        cur_gamma = (Gamma_factor)((cur_gamma + 1) % 5);
        gamma_set(cur_gamma);
        xil_printf("gamma : %s\r\n", gamma_name(cur_gamma));
        xil_printf("  note : AXI_GammaCorrection also does the 10 -> 8 bit\r\n"
                   "         conversion, so it cannot be removed. 1.0 is a\r\n"
                   "         linear curve, i.e. conversion only.\r\n");
        break;

    case 'i':
        OV5640_DumpKeyRegs();
        break;

    case 'c':
        xil_printf("\r\n--- MIPI receiver ---\r\n");
        mipi_rx_print_version();
        break;

    case 'e':
        xil_printf("SCCB errors : %d\r\n", (int)iic_sccb_error_count());
        iic_sccb_clear_errors();
        break;

    case '?':
    case '\r':
    case '\n':
        menu_help();
        break;

    default:
        break;
    }
}

/*===========================================================================
 *  main
 *===========================================================================*/
int main(void)
{
    u16 w, h;

    xil_printf("\r\n\r\n");
    xil_printf("=================================================\r\n");
    xil_printf(" Zybo Z7-20 + Pcam 5C (MIPI CSI-2) + HDMI  720p60\r\n");
    xil_printf("=================================================\r\n");

    /*-------------------------------------------------------------------
     *  1. Camera power
     *
     *  Before anything else, because on this board the sensor comes up
     *  held in power down and SCCB will answer anyway. Skipping this gives
     *  a successful chip ID read and a permanently black screen.
     *-------------------------------------------------------------------*/
    if (cam_gpio_init() != CAM_GPIO_OK) {
        xil_printf("EMIO GPIO did not come up. Stopping.\r\n");
        xil_printf("  check : PS block has EMIO GPIO enabled with Width=1,\r\n"
                   "          GPIO_0 made external, XDC pin G20 with PULLUP,\r\n"
                   "          XSA re-exported and the platform updated.\r\n");
        return 1;
    }

    /*-------------------------------------------------------------------
     *  2. Hold the MIPI receiver in reset
     *
     *  Done now, before the sensor can possibly start talking.
     *-------------------------------------------------------------------*/
    mipi_rx_reset();
    mipi_rx_print_version();

    /*-------------------------------------------------------------------
     *  3. SCCB bus
     *
     *  Hardware I2C can actually report failure, which is half the reason
     *  for using it - a bit-bang layer always claims success.
     *-------------------------------------------------------------------*/
    if (OV5640_Init() != IIC_SCCB_OK) {
        xil_printf("SCCB bus did not come up. Stopping.\r\n");
        xil_printf("  check : I2C 0 enabled in the PS block and routed to\r\n"
                   "          EMIO, IIC_0 made external, XDC pins F20/F19,\r\n"
                   "          XSA re-exported and the platform updated.\r\n");
        return 1;
    }

    /*-------------------------------------------------------------------
     *  4. Sensor power cycle, then common bring-up
     *
     *  OV5640_InitSensor() deliberately leaves the sensor in power down.
     *-------------------------------------------------------------------*/
    OV5640_PowerCycle();

    if (OV5640_InitSensor() != 0) {
        xil_printf("OV5640 detected failed!\r\n");
        xil_printf("  check : Pcam flat cable fully latched (press the\r\n"
                   "          connector down with two fingers), 5V external\r\n"
                   "          supply with JP6 on WALL, and the F20/F19 pins\r\n");
        return 1;
    }
    xil_printf("OV5640 detected successful!\r\n");

    if (iic_sccb_error_count()) {
        xil_printf("WARNING : %d SCCB error(s) during init sequence\r\n",
                   (int)iic_sccb_error_count());
        iic_sccb_clear_errors();
    }

    /*-------------------------------------------------------------------
     *  5. Video path : VDMA first, so DDR is ready to swallow frames
     *     the moment the sensor starts.
     *-------------------------------------------------------------------*/
    vd_mode = VMODE_1280x720;

    run_vdma_frame_buffer(&vdma, VDMA_ID, vd_mode.width, vd_mode.height,
                          FRAME_BUFFER_ADDR, 0, 0, BOTH);

    /*-------------------------------------------------------------------
     *  6. Release the MIPI receiver, THEN wake the sensor.
     *
     *  This is the ordering the whole file exists to get right. See the
     *  note at the top.
     *-------------------------------------------------------------------*/
    mipi_rx_enable();

    /* Gamma resets to 1.0 (linear), which is already a correct picture -
     * the IP is in the path for the 10 to 8 bit conversion, not for looks.
     * This just applies Digilent's preferred curve. */
    gamma_init();

    OV5640_SetMode720p();
    OV5640_SetAWB(cur_awb);

    /*-------------------------------------------------------------------
     *  7. HDMI out
     *
     *  No clock call anywhere in here. The pixel clock is fixed at
     *  74.25 MHz in the block design and rgb2dvi makes its own 5x from it.
     *-------------------------------------------------------------------*/
    DisplayInitialize(&dispCtrl, DISP_VTC_ID);
    DisplaySetMode(&dispCtrl, &vd_mode);
    DisplayStart(&dispCtrl);

    OV5640_GetImageInfo(&w, &h);
    xil_printf("camera  : %dx%d RAW10, 2 lane MIPI\r\n", w, h);
    xil_printf("display : %dx%d @ 74.25 MHz fixed\r\n",
               vd_mode.width, vd_mode.height);
    xil_printf("frame buffer at 0x%08X\r\n", (unsigned)FRAME_BUFFER_ADDR);

    menu_help();

    /*-------------------------------------------------------------------
     *  8. Super-loop
     *
     *  The loop lives here rather than inside menu_run() so that this file
     *  stays the one place that decides what the system does each pass.
     *  Adding a second job means adding a line here.
     *
     *  menu_run() is non-blocking : with no key waiting it returns at once,
     *  so anything else added to this loop keeps its timing.
     *-------------------------------------------------------------------*/
    for (;;) {

        menu_run();

        /* Room for the next job. Keep each one short and non-blocking - a
         * call that waits in here stalls every other job in the loop, which
         * is the failure mode this structure exists to avoid. */
    }

    /* not reached */
}
