/*
 *  cam_gpio.h
 *
 *  One EMIO GPIO bit, driving the Pcam 5C's sensor power-down pin.
 *
 *  ---------------------------------------------------------------------
 *  WHY THIS MODULE EXISTS AT ALL
 *
 *  The PZ7020 project has no equivalent. There the OV5640 module's reset pin
 *  went straight to an FPGA pin and the capture RTL released it, so software
 *  never had to think about sensor power.
 *
 *  On the Pcam 5C the sensor's power-down pin comes back to the PL and is
 *  wired to PS EMIO GPIO. Nothing releases it unless software does, and the
 *  failure mode when you forget is unhelpful : depending on how the board
 *  came up, SCCB may still answer and the chip ID may read back correctly,
 *  because the SCCB block is on a different power domain from the MIPI
 *  transmitter. So bring-up "succeeds" and no pixels ever arrive.
 *
 *  If the chip ID reads 0x5640 and the screen is still black, come back and
 *  check that OV5640_PowerCycle() actually ran.
 *
 *  ---------------------------------------------------------------------
 *  VIVADO SIDE
 *
 *   1. ZYNQ7 PS -> MIO Configuration -> GPIO -> EMIO GPIO, tick it,
 *      Width = 1.
 *   2. The block gains a GPIO_0 interface. Right click -> Make External.
 *      The port comes out as cam_gpio_tri_io[0:0].
 *   3. XDC :
 *        set_property PACKAGE_PIN G20 [get_ports {cam_gpio_tri_io[0]}]
 *        set_property IOSTANDARD LVCMOS33 [get_ports {cam_gpio_tri_io[0]}]
 *        set_property PULLUP true [get_ports {cam_gpio_tri_io[0]}]
 *
 *      The pull-up matters. It holds the sensor enabled through configuration,
 *      before the PS has run a single instruction.
 */

#ifndef CAM_GPIO_H
#define CAM_GPIO_H

#include "xil_types.h"

/* Zynq-7000 GPIO numbering : MIO is 0..53, EMIO starts at 54.
 * With EMIO width 1, our only bit is pin 54. */
#define CAM_GPIO_PIN        54

#define CAM_GPIO_OK          0
#define CAM_GPIO_ERR_LOOKUP -1
#define CAM_GPIO_ERR_INIT   -2

/* Configures the pin as an output and drives it high (sensor enabled). */
int  cam_gpio_init(void);

/* 1 = sensor powered, 0 = sensor in power down. */
void cam_gpio_set(u8 level);

#endif /* CAM_GPIO_H */
