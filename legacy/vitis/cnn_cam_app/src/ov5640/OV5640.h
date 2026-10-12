/*
 *  OV5640.h
 *
 *  OV5640 MIPI CSI-2 camera driver for Xilinx Zynq-7000 bare metal (Vitis).
 *  Plain C. Written to sit next to the PZ7020 DVP driver so the two can be
 *  read side by side in class.
 *
 *  Board  : Digilent Zybo Z7-20 (XC7Z020) + Pcam 5C
 *  SCCB   : Zynq PS hardware I2C (I2C0) routed to EMIO, pins F20 / F19
 *  Format : RAW10 Bayer, 1280x720 @ 60 fps, 2 MIPI lanes
 *
 *  ---------------------------------------------------------------------
 *  DIFFERENCES FROM THE PZ7020 DVP DRIVER  (worth reading once)
 *
 *   1. THE SENSOR OUTPUTS RAW BAYER, NOT RGB565.
 *      On PZ7020 the sensor's ISP produced finished RGB565 pixels and the
 *      capture RTL just unpacked them. Here the sensor sends RAW10 and the
 *      demosaic happens in the PL, in AXI_BayerToRGB. That is why there is
 *      no OV5640_SetColorFormat() in this driver: changing the format would
 *      break the block design, not just the picture.
 *
 *   2. THERE IS NO FRAME CONTROL CALL.
 *      OV5640_SetFrameControl() on PZ7020 set the output window and the
 *      total line/frame size, because those set the DVP pixel clock that the
 *      XDC constrained. On MIPI the link runs at a fixed serial rate set by
 *      the PLL registers in the mode table, so the geometry is baked into
 *      ov5640_cfg_720p_60fps and there is nothing to pass in.
 *
 *   3. THE SENSOR NEEDS A HARDWARE POWER CYCLE FIRST.
 *      Pcam 5C ties the sensor's power-down pin to a PL pin driven from PS
 *      EMIO GPIO. Without OV5640_PowerCycle() the chip ID read can succeed
 *      while the MIPI transmitter stays asleep. See cam_gpio.h.
 *
 *   4. BRING-UP IS TWO STEPS, NOT ONE.
 *      OV5640_InitSensor() applies the common table and LEAVES THE SENSOR IN
 *      POWER DOWN. OV5640_SetMode720p() applies the mode table and wakes it.
 *      The gap between the two is where the MIPI receiver gets enabled. Doing
 *      it the other way round means the first frames arrive at a receiver
 *      that is still in reset, and the link never recovers.
 *  ---------------------------------------------------------------------
 *
 *  !! WARNINGS FOR THIS PARTICULAR SYSTEM !!
 *
 *   - The PL path expects RAW10 on 2 lanes. Do not call OV5640_SetISPFormat()
 *     with ISP_RGB unless you have also changed MIPI_CSI_2_RX, and note that
 *     that IP only implements RAW10 - the RGB565 branch is declared but never
 *     generated. See section 4.2 of the build document.
 *   - Resolution is tied to three places : this driver, the VTC / VDMA
 *     settings in main.c, and the fixed 74.25 MHz pixel clock in the block
 *     design. Change all three.
 */

#ifndef OV5640_H
#define OV5640_H

#include "xil_types.h"
#include "sleep.h"

/* SCCB rides on the Zynq PS hardware I2C controller (XIicPs), routed to EMIO
 * so it comes out on PL pins F20 / F19. This is the same module the PZ7020
 * project uses - the read-ACK note in iic_sccb_cfg.h applies unchanged. */
#include "../iic_sccb_cfg/iic_sccb_cfg.h"

/*===========================================================================
 *  Register table element type
 *  (the tables themselves live in OV5640_REG.h)
 *===========================================================================*/
typedef struct {
    u16 addr;
    u8  val;
} OV5640_RegVal;

#define OV5640_REG_END      0xFFFF      /* table terminator address        */

/*===========================================================================
 *  Device constants
 *===========================================================================*/
#define OV5640_SCCB_ID      0x78        /* 8 bit write address; read = |1  */
#define OV5640_CHIP_ID      0x5640

#define OV5640_DEF_H_PIXEL  1280
#define OV5640_DEF_V_PIXEL  720

/*===========================================================================
 *  Enumerations, mirroring the PZ7020 driver style
 *===========================================================================*/
typedef enum { OFF = 0, ON } Camera_state;

typedef enum {
    AWB_DISABLED = 0,
    AWB_SIMPLE,
    AWB_ADVANCED        /* Digilent's default. Best of a mediocre set. */
} Camera_awb;

typedef enum {
    PATTERN_OFF = 0,
    PATTERN_COLOR_BAR   /* eight colour bars, generated inside the sensor */
} Camera_pattern;

typedef enum {
    ISP_RAW = 0,        /* the only format this PL path accepts */
    ISP_RGB
} Camera_ispfmt;

/*===========================================================================
 *  Low level - SCCB access
 *===========================================================================*/
/* Brings up the I2C controller. Returns 0 on success, negative on failure. */
int  OV5640_Init(void);
void OV5640_WriteSCCB(u16 regAddr, u8 val);
u8   OV5640_ReadSCCB(u16 regAddr);
void OV5640_ModifySCCB(u16 regAddr, u8 mask, u8 val);   /* read-modify-write */
void OV5640_Config(const OV5640_RegVal *table);

/* Group write : several registers take effect together at a frame boundary */
void OV5640_GroupBegin(void);
void OV5640_GroupCommit(void);

/*===========================================================================
 *  Bring-up
 *
 *  Call order is fixed :
 *      OV5640_Init()          SCCB bus
 *      OV5640_PowerCycle()    hardware power down pin, via EMIO GPIO
 *      OV5640_InitSensor()    ID check + common table. ENDS IN POWER DOWN.
 *      ... enable the MIPI receiver here ...
 *      OV5640_SetMode720p()   mode table + wake up. Streaming starts.
 *===========================================================================*/
void OV5640_PowerCycle(void);
u16  OV5640_ReadID(void);
void OV5640_ResetSW(void);
u8   OV5640_InitSensor(void);   /* 0 = ok, 1 = wrong or missing chip ID */
u8   OV5640_SetMode720p(void);
void OV5640_GetImageInfo(u16 *width, u16 *height);

/*===========================================================================
 *  The small set of controls this test program exposes
 *
 *  The PZ7020 driver has a large ISP section (brightness, contrast, gamma,
 *  colour matrix, sharpen, de-noise). Almost none of it applies here : those
 *  blocks sit AFTER the demosaic inside the sensor's ISP, and we are taking
 *  the output from BEFORE it, as RAW Bayer. Turning up the saturation would
 *  change nothing on screen.
 *
 *  What is left is the handful of things that still act on the raw path.
 *===========================================================================*/
void OV5640_SetAWB(Camera_awb mode);        /* acts on the raw gains        */
void OV5640_ShowTestPattern(Camera_pattern pattern);
void OV5640_FlipVertical(Camera_state en);
void OV5640_MirrorHorizontal(Camera_state en);
void OV5640_SetISPFormat(Camera_ispfmt fmt);    /* !! see the warning above  */

/*===========================================================================
 *  Debug helpers
 *===========================================================================*/
void OV5640_DumpKeyRegs(void);

#endif /* OV5640_H */
