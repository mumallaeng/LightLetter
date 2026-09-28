/*
 *  iic_sccb_cfg.h
 *
 *  SCCB over the Zynq-7000 PS hardware I2C controller, routed to EMIO so it
 *  comes out on the same PL pins the bit-bang version used (J18 / K19).
 *
 *  This replaced an EMIO GPIO bit-bang layer (emio_sccb_cfg), which has been
 *  removed from the project.
 *
 *  ---------------------------------------------------------------------
 *  WHY HARDWARE I2C RATHER THAN BIT-BANG  (worth reading once)
 *
 *  A bit-bang driver toggles two GPIO pins with usleep() between edges.
 *  That has three problems, and none of them show up until you start using
 *  the bus in anger :
 *
 *   1. IT CANNOT SEE AN ACK. Its ack routine drove SDA low and pulsed
 *      the clock. It never sampled the line, so a device that is absent,
 *      held in reset, or busy looks exactly like one that answered. Every
 *      write "succeeds". Hardware I2C samples the ACK bit and reports a
 *      NACK as an error, which is how you find a wiring fault in one run
 *      instead of one afternoon.
 *
 *   2. ITS TIMING IS NOT REALLY CONTROLLED. usleep() on bare metal is a
 *      busy loop; an interrupt lands in the middle of a bit and the clock
 *      low period stretches. Slaves tolerate that, but it makes the bus
 *      slow and the waveform ugly on a scope. The PS controller generates
 *      the clock in hardware from a divider.
 *
 *   3. IT DRIVES THE LINES PUSH-PULL. Real I2C is open-drain: everyone can
 *      only pull low, and a resistor pulls high. Push-pull happens to work
 *      with one master and one slave, and stops working the moment anything
 *      else shares the bus or the slave tries clock stretching.
 *
 *  Point 1 is the one that bit us. See the shadow-register note in OV5640.c
 *  for how a read that silently returned the wrong byte froze the video.
 *
 *  ---------------------------------------------------------------------
 *  !! HARDWARE PREREQUISITE - CHECK THIS BEFORE BLAMING THE CODE !!
 *
 *  Because hardware I2C is open-drain, SCL and SDA need PULL-UP RESISTORS
 *  (typically 2.2k - 4.7k to the camera's 2.8 V / 3.3 V rail). The bit-bang
 *  version worked without them because it drove both directions actively.
 *
 *  Almost every OV5640 module carries its own pull-ups on the flex, so this
 *  usually just works. If iic_sccb_init() reports the chip ID as 0x0000 or
 *  0xFFFF, suspect the pull-ups first, before the software.
 *
 *  ---------------------------------------------------------------------
 *  VIVADO SIDE - what has to change in the block design
 *
 *   1. ZYNQ7 Processing System -> MIO Configuration -> I/O Peripherals
 *      -> tick I2C 0, and set its pin selection to EMIO.
 *   2. The block now has an IIC_0 interface. Right click -> Make External.
 *      Vivado inserts the IOBUFs, so you get IIC_0_scl_io and IIC_0_sda_io
 *      as true bidirectional top-level ports.
 *   3. Delete the two old GPIO EMIO port assignments for the camera SCL and
 *      SDA (EMIO 54 / 55) if nothing else uses them.
 *   4. XDC : point the new ports at the same package pins as before.
 *
 *        set_property -dict {PACKAGE_PIN J18 IOSTANDARD LVCMOS33} \
 *            [get_ports IIC_0_scl_io]
 *        set_property -dict {PACKAGE_PIN K19 IOSTANDARD LVCMOS33} \
 *            [get_ports IIC_0_sda_io]
 *
 *      Confirm J18 / K19 against your own XDC - those are the pins the
 *      EMIO version used on PZ7020-StarLite.
 *   5. Regenerate the bitstream and export the XSA, then update the Vitis
 *      platform so xparameters.h gains XPAR_XIICPS_0_DEVICE_ID.
 */

#ifndef IIC_SCCB_CFG_H
#define IIC_SCCB_CFG_H

#include "xil_types.h"

/*---------------------------------------------------------------------------
 *  Configuration
 *---------------------------------------------------------------------------*/

/* Which PS I2C controller was routed to EMIO. Change to
 * XPAR_XIICPS_1_DEVICE_ID if you enabled I2C 1 instead. */
#ifndef IIC_SCCB_DEVICE_ID
#  define IIC_SCCB_DEVICE_ID    XPAR_XIICPS_0_DEVICE_ID
#endif

/* Bus clock. The OV5640 handles 400 kHz, but 100 kHz is kinder to a long
 * camera ribbon and the register traffic here is tiny anyway. */
#ifndef IIC_SCCB_CLK_HZ
#  define IIC_SCCB_CLK_HZ       100000
#endif

/* 7 bit address. The datasheet quotes 0x78 as the 8 bit write address,
 * which is the 7 bit address 0x3C shifted left by one. XIicPs wants the
 * 7 bit form - passing 0x78 here is the classic first mistake. */
#define IIC_SCCB_SLAVE_ADDR     0x3C

/*---------------------------------------------------------------------------
 *  API
 *
 *  Unlike the bit-bang layer these return a status, because now there is
 *  something real to report.
 *    0            = success
 *    negative     = failure, see the IIC_SCCB_ERR_* codes
 *---------------------------------------------------------------------------*/
#define IIC_SCCB_OK              0
#define IIC_SCCB_ERR_LOOKUP     -1      /* device id not in xparameters.h   */
#define IIC_SCCB_ERR_INIT       -2      /* XIicPs_CfgInitialize failed      */
#define IIC_SCCB_ERR_SELFTEST   -3      /* controller self test failed      */
#define IIC_SCCB_ERR_CLK        -4      /* requested SCLK not achievable    */
#define IIC_SCCB_ERR_NACK       -5      /* nobody acknowledged - wiring?    */
#define IIC_SCCB_ERR_TIMEOUT    -6      /* bus stayed busy                  */

int  iic_sccb_init(void);

int  iic_sccb_write(u16 reg_addr, u8 value);
int  iic_sccb_read (u16 reg_addr, u8 *value);

/* Convenience: read and return the byte, 0xFF on failure. Use the two
 * functions above when you care about the difference. */
u8   iic_sccb_read_byte(u16 reg_addr);

/* Error bookkeeping, so a long init sequence can be checked once at the end
 * rather than at every single call site. */
u32  iic_sccb_error_count(void);
void iic_sccb_clear_errors(void);
int  iic_sccb_last_error(void);

/* There is deliberately no bus scan here. See the note at the bottom of
 * iic_sccb_cfg.c : probing absent addresses provokes NACKs that this
 * controller's polled receive does not recover from cheaply, and the scan
 * ended up hanging the boot it was supposed to be diagnosing.
 * OV5640_InitSensor() reading the chip ID is the check that matters. */

#endif /* IIC_SCCB_CFG_H */
