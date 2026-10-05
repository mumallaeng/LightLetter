/*
 *  iic_sccb_cfg.c
 *
 *  SCCB over the Zynq-7000 PS I2C controller (XIicPs), routed through EMIO.
 *  See iic_sccb_cfg.h for the rationale and the Vivado side of the change.
 */

#include "iic_sccb_cfg.h"

#include "xparameters.h"
#include "xiicps.h"
#include "xil_printf.h"
#include "sleep.h"
#include <stddef.h>

/*===========================================================================
 *  State
 *===========================================================================*/
static XIicPs iic_inst;
static int    iic_ready     = 0;
static u32    iic_err_count = 0;
static int    iic_last_err  = IIC_SCCB_OK;

/*
 * The controller must be idle before the next transfer starts. Polled mode
 * returns as soon as the FIFO is drained, not when the STOP has gone out on
 * the wire, so this wait is not optional.
 *
 * A bounded loop rather than while(BusIsBusy()) : if SDA is stuck low - a
 * missing pull-up, or a slave left mid-transfer - the unbounded version
 * hangs the application forever with no message. That is a much worse
 * failure than returning an error.
 */
static int iic_wait_idle(void)
{
    /*
     * A tight spin with a plain counter, deliberately. The previous version
     * called usleep(1) per turn with a guard of 100000, on the assumption
     * that it would give up after about 100 ms. On bare metal usleep(1) is
     * nowhere near 1 us once the call overhead is counted, so the real
     * ceiling was seconds - per transfer. That is how a bounded loop still
     * ends up looking like a hang.
     *
     * At 100 kHz a byte takes about 90 us, so a few hundred thousand reads
     * of a register is a generous ceiling and a cheap one.
     */
    u32 guard = 200000U;

    while (XIicPs_BusIsBusy(&iic_inst)) {
        if (--guard == 0U) {
            iic_last_err = IIC_SCCB_ERR_TIMEOUT;
            iic_err_count++;
            return IIC_SCCB_ERR_TIMEOUT;
        }
    }
    return IIC_SCCB_OK;
}

/*===========================================================================
 *  Bring-up
 *===========================================================================*/
int iic_sccb_init(void)
{
    XIicPs_Config *cfg;
    int status;

    iic_ready     = 0;
    iic_err_count = 0;
    iic_last_err  = IIC_SCCB_OK;

    cfg = XIicPs_LookupConfig(IIC_SCCB_DEVICE_ID);
    if (cfg == NULL) {
        xil_printf("iic_sccb: no config for device id %d. Did you enable\r\n"
                   "          I2C in the PS block and re-export the XSA?\r\n",
                   IIC_SCCB_DEVICE_ID);
        iic_last_err = IIC_SCCB_ERR_LOOKUP;
        return IIC_SCCB_ERR_LOOKUP;
    }

    status = XIicPs_CfgInitialize(&iic_inst, cfg, cfg->BaseAddress);
    if (status != XST_SUCCESS) {
        iic_last_err = IIC_SCCB_ERR_INIT;
        return IIC_SCCB_ERR_INIT;
    }

    /* Catches a controller that is not clocked or not present at all. */
    status = XIicPs_SelfTest(&iic_inst);
    if (status != XST_SUCCESS) {
        xil_printf("iic_sccb: controller self test failed\r\n");
        iic_last_err = IIC_SCCB_ERR_SELFTEST;
        return IIC_SCCB_ERR_SELFTEST;
    }

    status = XIicPs_SetSClk(&iic_inst, IIC_SCCB_CLK_HZ);
    if (status != XST_SUCCESS) {
        xil_printf("iic_sccb: cannot set SCLK to %d Hz\r\n", IIC_SCCB_CLK_HZ);
        iic_last_err = IIC_SCCB_ERR_CLK;
        return IIC_SCCB_ERR_CLK;
    }

    iic_ready = 1;

    /* The OV5640 needs a moment after power up before it answers SCCB.
     * The bit-bang version got this for free from its own slowness. */
    usleep(5000);

    return IIC_SCCB_OK;
}

/*===========================================================================
 *  Register write
 *
 *  One transfer: [addr_hi][addr_lo][data], then STOP.
 *===========================================================================*/
int iic_sccb_write(u16 reg_addr, u8 value)
{
    u8  buf[3];
    int status;

    if (!iic_ready) return IIC_SCCB_ERR_INIT;

    buf[0] = (u8)(reg_addr >> 8);
    buf[1] = (u8)(reg_addr & 0x00FF);
    buf[2] = value;

    status = XIicPs_MasterSendPolled(&iic_inst, buf, 3, IIC_SCCB_SLAVE_ADDR);
    if (status != XST_SUCCESS) {
        /* No HOLD is involved here, but a NACK can still leave the FIFO
         * with residue. Cheap insurance, same as on the read path. */
        XIicPs_Abort(&iic_inst);
        iic_last_err = IIC_SCCB_ERR_NACK;
        iic_err_count++;
        return IIC_SCCB_ERR_NACK;
    }

    return iic_wait_idle();
}

/*===========================================================================
 *  Register read
 *
 *  Two phases with a REPEATED START between them :
 *
 *      S | 0x3C W | A | addr_hi | A | addr_lo | A |
 *          Sr | 0x3C R | A | data | NA | P
 *
 *  ---------------------------------------------------------------------
 *  WHAT XIICPS_REP_START_OPTION ACTUALLY DOES  (and what it does not)
 *
 *  This confuses everyone once, so it is written down. The option has
 *  nothing to do with ACK or NACK. It controls the HOLD bit in the
 *  controller's CR register, i.e. whether a transfer ends with a STOP or
 *  keeps the bus so the next one begins with a repeated START.
 *
 *  Reading the driver source (xiicps_options.c, xiicps_master.c) :
 *
 *   - SetOptions(REP_START) only sets a software flag, IsRepeatedStart.
 *     The comment in the driver is explicit: "The hold bit in CR will be
 *     written by driver when the next transfer is initiated."
 *
 *   - MasterSendPolled sees the flag, asserts CR.HOLD before the transfer,
 *     and at the end skips the "clear HOLD" step. So the bus stays held -
 *     no STOP goes out. That is the repeated start being set up.
 *
 *   - ClearOptions(REP_START) also only clears the flag. It does NOT touch
 *     CR.HOLD. "The hold bit in CR will be cleared by driver when the
 *     following transfer ends." So calling it here, between the two
 *     transfers, cannot produce a premature STOP. This is the part that
 *     looks wrong and is not.
 *
 *   - MasterRecvPolled then writes the slave address again, which emits the
 *     repeated START, and because the flag is now clear it clears CR.HOLD
 *     after the byte arrives - which is the STOP.
 *
 *  ---------------------------------------------------------------------
 *  WHERE THE NACK COMES FROM
 *
 *  The master must NACK the last byte of a read, and it does - but no
 *  software writes that bit. MasterRecvPolled loads the byte count into
 *  the controller's TRANS_SIZE register, and the hardware counts down and
 *  NACKs the final byte on its own. There is no API for it because there
 *  is no choice to make.
 *
 *  This is exactly what the bit-bang version got wrong. It had to generate
 *  the ninth clock itself, and sccb_ack() always drove SDA low - an ACK,
 *  meaning "keep sending" - and then issued a STOP. Moving to hardware I2C
 *  does not just make that easier to get right; it removes the opportunity
 *  to get it wrong.
 *===========================================================================*/
int iic_sccb_read(u16 reg_addr, u8 *value)
{
    u8  addr[2];
    int status;

    if (!iic_ready) return IIC_SCCB_ERR_INIT;
    if (value == NULL) return IIC_SCCB_ERR_INIT;

    addr[0] = (u8)(reg_addr >> 8);
    addr[1] = (u8)(reg_addr & 0x00FF);

    XIicPs_SetOptions(&iic_inst, XIICPS_REP_START_OPTION);

    status = XIicPs_MasterSendPolled(&iic_inst, addr, 2, IIC_SCCB_SLAVE_ADDR);

    /* Clear the flag before anything else, so the receive below - or the
     * abort on the error path - is the transfer that releases CR.HOLD.
     * This only clears the flag; CR.HOLD is still asserted at this point. */
    XIicPs_ClearOptions(&iic_inst, XIICPS_REP_START_OPTION);

    if (status != XST_SUCCESS) {
        /*
         * The address phase was NACKed while the bus was held. Nothing will
         * come along to clear CR.HOLD now, so without this the controller
         * sits on the bus with SCL low and every later transfer fails - one
         * missing camera turns into a dead I2C bus.
         *
         * XIicPs_Abort() rewrites CR with its reset value and flushes the
         * FIFOs, which drops HOLD and frees the lines.
         */
        XIicPs_Abort(&iic_inst);
        iic_last_err = IIC_SCCB_ERR_NACK;
        iic_err_count++;
        return IIC_SCCB_ERR_NACK;
    }

    status = XIicPs_MasterRecvPolled(&iic_inst, value, 1, IIC_SCCB_SLAVE_ADDR);
    if (status != XST_SUCCESS) {
        XIicPs_Abort(&iic_inst);        /* leave the bus usable */
        iic_last_err = IIC_SCCB_ERR_NACK;
        iic_err_count++;
        return IIC_SCCB_ERR_NACK;
    }

    return iic_wait_idle();
}

u8 iic_sccb_read_byte(u16 reg_addr)
{
    u8 v = 0xFF;
    (void)iic_sccb_read(reg_addr, &v);
    return v;
}

/*===========================================================================
 *  Diagnostics
 *===========================================================================*/
u32  iic_sccb_error_count(void) { return iic_err_count; }
void iic_sccb_clear_errors(void){ iic_err_count = 0; iic_last_err = IIC_SCCB_OK; }
int  iic_sccb_last_error(void)  { return iic_last_err; }

/*
 *  THE BUS SCAN AND THE SPEED SWEEP THAT USED TO BE HERE ARE GONE.
 *
 *  Both were mine, both were wrong, and the second one hung the boot. The
 *  reason is worth keeping even though the code is not.
 *
 *  Probing an address that nobody answers means provoking a NACK on
 *  purpose, and XIicPs_MasterRecvPolled() is not built for that. Its
 *  polling loop leans on the interrupt status register to notice the NACK,
 *  the recovery afterwards is not free, and XIicPs_BusIsBusy() can stay
 *  asserted while the lines crawl back up through a weak pull-up. Do it
 *  once and you might get away with it. Do it 112 times in a row, before
 *  the sensor has even been initialised, and the boot stops looking like a
 *  slow scan and starts looking like a hang - which is exactly what it did.
 *
 *  The first version was worse in a quieter way: it probed with a zero
 *  length write, which this controller never puts on the wire at all, so it
 *  reported an empty bus no matter what was connected.
 *
 *  The lesson is not "scanning is hard". It is that a diagnostic which can
 *  fail, and which runs before the thing it is diagnosing, is worse than no
 *  diagnostic at all. It moves the fault into itself.
 *
 *  What is left is the check that was always sufficient : OV5640_ReadID()
 *  reads 0x300A / 0x300B and OV5640_InitSensor() refuses to continue unless
 *  it sees 0x5640. That path only ever talks to the address we care about,
 *  and its first phase is a write - which does return promptly on NACK,
 *  unlike a read. If the camera is absent it says so in a few milliseconds.
 *
 *  If you ever do want a scan, do it with the controller's slave monitor
 *  mode (XIICPS_SLAVE_MON_OPTION and XIicPs_SetupSlaveMonitor), which is
 *  the mechanism actually intended for asking "is anyone there".
 */
