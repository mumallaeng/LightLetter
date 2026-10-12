/*
 *  OV5640.c
 *
 *  OV5640 MIPI CSI-2 driver, plain C.
 *  Converted from Digilent's C++ driver (sdk/appsrc/pcam_vdma_hdmi/ov5640/)
 *  and restructured to match the PZ7020 DVP driver's file layout.
 *
 *  ---------------------------------------------------------------------
 *  WHAT CHANGED IN THE C++ -> C CONVERSION, AND WHY
 *
 *   1. TEMPLATES AND CLASSES ARE GONE.
 *      Digilent's OV5640 was a class holding references to an I2C_Client and
 *      a GPIO_Client, both abstract bases with template implementations
 *      (PS_IIC<T>, PS_GPIO<T>) parameterised on an interrupt controller.
 *      That buys runtime substitution of the bus, which nothing in this
 *      project uses : there is exactly one I2C controller and one GPIO.
 *      Here the bus is a module (iic_sccb_cfg) and the driver calls it
 *      directly, which is what the PZ7020 code does.
 *
 *   2. EXCEPTIONS ARE GONE.
 *      The C++ version threw HardwareError on a bad chip ID and on every I2C
 *      failure, and main() wrapped the whole bring-up in one try/catch. In C
 *      the functions return a status and main checks it at each step. This is
 *      not merely a translation : it makes the failure point visible. The
 *      catch-all told you "something in bring-up failed"; the return codes
 *      tell you it was the chip ID and not the bus.
 *
 *   3. THE INTERRUPT PATH IS GONE.
 *      PS_IIC and AXI_VDMA drove their transfers from ScuGic interrupts.
 *      For a bring-up test that is machinery without a purpose : the register
 *      traffic is a few hundred bytes at boot and VDMA free-runs afterwards.
 *      Both are polled here, so ScuGicInterruptController.h has no C
 *      counterpart, and the block design does not need IRQ_F2P.
 *
 *   4. usleep(1000000) x 2 SURVIVED.
 *      Digilent's reset() power-cycles the sensor with a full second on each
 *      side. It looks like superstition and it is not : the Pcam's regulator
 *      takes time to bleed down, and a short cycle leaves the sensor in a
 *      half-powered state where SCCB answers but MIPI never starts. Shorten
 *      it and you will chase that fault for an afternoon.
 *  ---------------------------------------------------------------------
 */

#include "OV5640.h"
#include "OV5640_REG.h"
#include "../cam_gpio/cam_gpio.h"
#include "xil_printf.h"

/*===========================================================================
 *  Local state
 *===========================================================================*/
static u16 img_width  = OV5640_DEF_H_PIXEL;
static u16 img_height = OV5640_DEF_V_PIXEL;

/* NO SHADOW REGISTERS HERE - and that is a deliberate difference from the
 * PZ7020 driver, which keeps a large shadow table.
 *
 * The reason the PZ7020 driver needs shadows is in its own comments : that
 * project started on a GPIO bit-bang bus whose read path ACKed the last byte
 * instead of NACKing it, so roughly one read in fifteen came back wrong. With
 * read-modify-write, a bad read does not merely fail - it writes garbage into
 * a live control register.
 *
 * This project has hardware I2C from the first line of code, and iic_sccb_read
 * reports a NACK instead of inventing a value. Seeding a shadow table with
 * post-reset defaults would introduce a NEW failure mode : if a default is
 * wrong, or a config table changes a register the shadow does not track, the
 * shadow and the sensor drift apart silently. Reading the register is both
 * simpler and more truthful.
 *
 * If iic_sccb_error_count() starts climbing, fix the bus - do not paper over
 * it with a cache. */

/*===========================================================================
 *  1. SCCB ACCESS
 *
 *  OV5640 register addresses are 16 bit, so a write is
 *      START | ID | addr[15:8] | addr[7:0] | data | STOP
 *  and a read is a write of the address followed by a repeated start.
 *  All of that protocol lives in iic_sccb_cfg.c. This file never touches
 *  the bus directly.
 *===========================================================================*/
int OV5640_Init(void)
{
    int status = iic_sccb_init();
    if (status != IIC_SCCB_OK) {
        xil_printf("OV5640: SCCB (PS I2C) init failed, code %d\r\n", status);
    }
    return status;
}

void OV5640_WriteSCCB(u16 regAddr, u8 val)
{
    (void)iic_sccb_write(regAddr, val);
}

u8 OV5640_ReadSCCB(u16 regAddr)
{
    return iic_sccb_read_byte(regAddr);
}

void OV5640_ModifySCCB(u16 regAddr, u8 mask, u8 val)
{
    u8  cur;
    int status = iic_sccb_read(regAddr, &cur);

    if (status != IIC_SCCB_OK) {
        /* Do NOT write on a failed read. Writing back a value we did not
         * actually observe is exactly the fault the PZ7020 project spent an
         * afternoon on : corrupt 0x3820/0x3821 and the sensor readout
         * geometry changes, the frame stops matching VTC, and the video
         * freezes. Failing loudly is cheaper. */
        xil_printf("OV5640: read-modify-write aborted, reg 0x%04X read failed\r\n",
                   regAddr);
        return;
    }

    cur = (u8)((cur & (u8)~mask) | (val & mask));
    OV5640_WriteSCCB(regAddr, cur);
}

void OV5640_Config(const OV5640_RegVal *table)
{
    int i;
    for (i = 0; table[i].addr != OV5640_REG_END; i++) {
        OV5640_WriteSCCB(table[i].addr, table[i].val);
    }
}

/*---------------------------------------------------------------------------
 *  Group write
 *
 *  Registers written between Begin() and Commit() are latched together at the
 *  next frame boundary. Without this a live change can land mid-frame and
 *  show as a visible band.
 *---------------------------------------------------------------------------*/
void OV5640_GroupBegin(void)
{
    OV5640_WriteSCCB(REG_GROUP_ACCESS, GROUP_START);
}

void OV5640_GroupCommit(void)
{
    OV5640_WriteSCCB(REG_GROUP_ACCESS, GROUP_END);
    OV5640_WriteSCCB(REG_GROUP_ACCESS, GROUP_LAUNCH);
}

/*===========================================================================
 *  2. BRING-UP
 *===========================================================================*/

/*
 * Hardware power cycle through the sensor's power-down pin.
 *
 * This is the step that has no equivalent in the PZ7020 project, where the
 * sensor's reset pin was wired straight to the FPGA and released by the RTL.
 * Here it is a PS EMIO GPIO bit, and nothing releases it unless we do.
 *
 * The two full seconds are Digilent's and are load-bearing. See the note at
 * the top of this file.
 */
void OV5640_PowerCycle(void)
{
    cam_gpio_set(0);        /* sensor off  */
    usleep(1000000);
    cam_gpio_set(1);        /* sensor on   */
    usleep(1000000);
}

u16 OV5640_ReadID(void)
{
    u16 id;
    id  = (u16)(OV5640_ReadSCCB(REG_CHIP_ID_HIGH) << 8);    /* 0x56 */
    id |= OV5640_ReadSCCB(REG_CHIP_ID_LOW);                 /* 0x40 */
    return id;
}

void OV5640_ResetSW(void)
{
    /* bit 7 : software reset, bit 6 : software power down */
    OV5640_WriteSCCB(REG_SYS_CTRL0, REG_SYS_CTRL0_RESET);
    usleep(1000000);    /* Digilent waits a full second here too */
}

/*
 * Common bring-up.
 *
 * Returns 0 on success, 1 if the chip ID does not read back as 0x5640.
 *
 * !! ON RETURN THE SENSOR IS IN SOFTWARE POWER DOWN AND IS NOT STREAMING. !!
 *
 * That is deliberate and it is the whole reason bring-up is split in two.
 * Between this call and OV5640_SetMode720p() the caller enables the MIPI
 * D-PHY and CSI-2 receiver. If the sensor were already streaming, those first
 * packets would hit a receiver still held in reset; the CSI-2 link would lose
 * packet framing and never resynchronise, and the symptom on screen is a
 * black or torn image that a power cycle does not fix.
 *
 * The write order below is Digilent's order. Do not rearrange it.
 */
u8 OV5640_InitSensor(void)
{
    if (OV5640_ReadID() != OV5640_CHIP_ID) {
        return 1;
    }

    /* [1]=0 system input clock from pad, so the sensor runs off the external
     * 24 MHz oscillator on the Pcam while we reset it */
    OV5640_WriteSCCB(REG_SYS_ROOT_DIVIDER, 0x11);
    OV5640_ResetSW();

    /* Common table. Sets the PLL for a 672 MHz MIPI serial clock, configures
     * the two lanes, and leaves the sensor in software power down. */
    OV5640_Config(ov5640_cfg_init);

    img_width  = OV5640_DEF_H_PIXEL;
    img_height = OV5640_DEF_V_PIXEL;

    return 0;
}

/*
 * Mode table : 1280x720 binned, RAW10, 60 fps.
 *
 * Wrapped in power down / power up so the whole table lands at once. Writing
 * PLL registers while the sensor is streaming produces a burst of malformed
 * CSI-2 packets on the way through.
 */
u8 OV5640_SetMode720p(void)
{
    OV5640_WriteSCCB(REG_SYS_CTRL0, REG_SYS_CTRL0_PWDN);
    OV5640_Config(ov5640_cfg_720p_60fps);
    OV5640_WriteSCCB(REG_SYS_CTRL0, REG_SYS_CTRL0_PWUP);

    img_width  = 1280;
    img_height = 720;

    return 0;
}

void OV5640_GetImageInfo(u16 *width, u16 *height)
{
    *width  = img_width;
    *height = img_height;
}

/*===========================================================================
 *  3. THE SMALL SET OF CONTROLS THAT STILL MATTER ON A RAW PATH
 *===========================================================================*/

/*
 * Auto white balance.
 *
 * Unlike brightness or saturation, AWB is not purely a post-demosaic ISP
 * function : it scales the raw R / Gr / Gb / B channel gains, which happens
 * upstream of where we tap the data. So this one does change the picture.
 *
 * ADVANCED is Digilent's default. The image still comes out slightly yellow -
 * that is a known limitation of this sensor's AWB, documented by Digilent, not
 * a fault in the configuration.
 */
void OV5640_SetAWB(Camera_awb mode)
{
    OV5640_WriteSCCB(REG_SYS_CTRL0, REG_SYS_CTRL0_PWDN);

    switch (mode) {
    case AWB_ADVANCED:  OV5640_Config(ov5640_cfg_awb_advanced); break;
    case AWB_SIMPLE:    OV5640_Config(ov5640_cfg_awb_simple);   break;
    case AWB_DISABLED:
    default:            OV5640_Config(ov5640_cfg_awb_disable);  break;
    }

    OV5640_WriteSCCB(REG_SYS_CTRL0, REG_SYS_CTRL0_PWUP);
}

/*
 * Internal colour bar pattern.
 *
 * This is the single most useful bring-up tool in the file, so it is worth
 * saying what it proves and what it does not.
 *
 *   Bars visible  -> the whole chain works : SCCB, sensor PLL, MIPI D-PHY,
 *                    CSI-2 framing, demosaic, VDMA, DDR, VTC, HDMI.
 *                    Whatever is wrong is in front of the sensor - lens cap,
 *                    exposure, focus, lighting.
 *   Bars missing  -> the fault is in the chain, and the lens is irrelevant.
 *
 * That one keystroke splits the search space in half. Reach for it before
 * touching anything else.
 *
 * Note the bars are generated BEFORE the Bayer formatter, so they arrive as
 * RAW data and get demosaiced like a real image. Expect slightly soft edges
 * between bars - that is the demosaic doing its job, not a fault.
 */
void OV5640_ShowTestPattern(Camera_pattern pattern)
{
    switch (pattern) {
    case PATTERN_COLOR_BAR:
        /* [7]=1 colour bar enable, [3:2]=00 eight colour bar */
        OV5640_WriteSCCB(REG_PRE_ISP_TEST_SET1, 0x80);
        break;
    case PATTERN_OFF:
    default:
        OV5640_WriteSCCB(REG_PRE_ISP_TEST_SET1, 0x00);
        break;
    }
}

/*
 * Flip and mirror.
 *
 * !! THESE CHANGE THE BAYER PHASE. !!
 *
 * The sensor's colour filter array is a fixed BG/GR checkerboard. Flipping
 * the readout shifts which colour lands on the first pixel of the first line,
 * and AXI_BayerToRGB in the PL is configured for one specific phase. So a
 * flip can turn the picture upside down AND swap red with blue, or produce a
 * fine colour grid.
 *
 * On PZ7020 this did not happen, because there the sensor's own ISP did the
 * demosaic and compensated internally. Here the demosaic is outside the
 * sensor and knows nothing about the flip.
 *
 * Left in deliberately : it is a good demonstration of why the demosaic phase
 * matters, and of what "colour looks like a grid" means in practice.
 */
void OV5640_FlipVertical(Camera_state en)
{
    /* 0x3820 [2]=ISP vflip, [1]=sensor vflip */
    OV5640_ModifySCCB(REG_TIMING_TC_REG20, 0x06, en ? 0x06 : 0x00);
}

void OV5640_MirrorHorizontal(Camera_state en)
{
    /* 0x3821 [2]=ISP mirror, [1]=sensor mirror */
    OV5640_ModifySCCB(REG_TIMING_TC_REG21, 0x06, en ? 0x06 : 0x00);
}

/*
 * ISP output format mux.
 *
 * ISP_RAW (0x03) is what the block design expects. ISP_RGB (0x01) makes the
 * sensor emit RGB565 over CSI-2, which sounds like it would let us delete
 * AXI_BayerToRGB - and it would, if MIPI_CSI_2_RX supported it. It does not:
 * the IP declares an RGB565 data type constant but never generates the branch
 * that uses it, so the receiver drops every packet.
 *
 * Exposed anyway so the failure can be demonstrated rather than described.
 */
void OV5640_SetISPFormat(Camera_ispfmt fmt)
{
    OV5640_WriteSCCB(REG_SYS_CTRL0, REG_SYS_CTRL0_PWDN);
    OV5640_WriteSCCB(REG_FORMAT_MUX_CONTROL, (fmt == ISP_RGB) ? 0x01 : 0x03);
    OV5640_WriteSCCB(REG_SYS_CTRL0, REG_SYS_CTRL0_PWUP);
}

/*===========================================================================
 *  4. DEBUG
 *===========================================================================*/
void OV5640_DumpKeyRegs(void)
{
    static const struct { u16 addr; const char *name; } regs[] = {
        { 0x300A, "CHIP_ID_H     " },
        { 0x300B, "CHIP_ID_L     " },
        { 0x3008, "SYS_CTRL0     " },
        { 0x300E, "MIPI_CTRL00   " },
        { 0x3034, "PLL_CTRL0     " },
        { 0x3035, "PLL_CTRL1     " },
        { 0x3036, "PLL_MULTIPLIER" },
        { 0x3037, "PLL_CTRL3     " },
        { 0x3820, "TIMING_TC_20  " },
        { 0x3821, "TIMING_TC_21  " },
        { 0x4300, "FORMAT_CONTROL" },
        { 0x501F, "FORMAT_MUX    " },
        { 0x503D, "PRE_ISP_TEST1 " },
    };
    unsigned i;

    xil_printf("\r\n--- OV5640 key registers ---\r\n");
    for (i = 0; i < sizeof(regs) / sizeof(regs[0]); i++) {
        xil_printf("  0x%04X %s = 0x%02X\r\n",
                   regs[i].addr, regs[i].name, OV5640_ReadSCCB(regs[i].addr));
    }
    xil_printf("  SCCB errors so far : %d\r\n", (int)iic_sccb_error_count());
    xil_printf("----------------------------\r\n");
}
