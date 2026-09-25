/*
 *  mipi_rx.h
 *
 *  Reset and enable for Digilent's MIPI_D_PHY_RX and MIPI_CSI_2_RX cores.
 *
 *  ---------------------------------------------------------------------
 *  WHAT THESE TWO IPS ACTUALLY NEED FROM SOFTWARE
 *
 *  Almost nothing, and that surprises people. Both cores expose exactly one
 *  AXI-Lite register :
 *
 *      offset 0x0   CR      bit 0 = reset, bit 1 = enable
 *      offset 0xC   VERSION major[31:16] / minor[15:0], read only
 *
 *  Lane count, data type and line rate are all BUILD TIME parameters set in
 *  the IP customisation dialog. There is no software knob for them. If the
 *  link is not working, changing something here will not help - the fix is in
 *  the block design.
 *
 *  Digilent's C++ code drove these registers through the generated
 *  MIPI_D_PHY_RX.h / MIPI_CSI_2_RX.h macros, which are just Xil_Out32 with a
 *  base address. This module wraps them so main.c reads as a sequence of
 *  intentions rather than a column of register pokes.
 *
 *  ---------------------------------------------------------------------
 *  THE ORDER IS NOT ARBITRARY
 *
 *  Reset : CSI-2 first, then D-PHY.
 *  Enable: D-PHY first, then CSI-2.
 *
 *  That is, reset from the consumer end backwards and enable from the
 *  producer end forwards. Same principle as the whole bring-up sequence in
 *  main.c, applied one level down. If CSI-2 came out of reset after the D-PHY
 *  had already started delivering bytes, it would join the stream part way
 *  through a packet and lose framing.
 *
 *  Digilent's own code does it in this order. It is easy to miss because the
 *  two calls sit next to each other and look interchangeable.
 */

#ifndef MIPI_RX_H
#define MIPI_RX_H

#include "xil_types.h"

/* Hold both cores in reset. Call before the sensor is woken up. */
void mipi_rx_reset(void);

/* Release both cores. Call after OV5640_InitSensor() and BEFORE
 * OV5640_SetMode720p(), which is what starts the sensor streaming. */
void mipi_rx_enable(void);

/* Reads the VERSION register of each core and prints it.
 *
 * Worth running once on a new build. These registers answer even when the
 * MIPI link itself is dead, so a sensible version number proves that the
 * AXI-Lite plumbing, the address map and xparameters.h all agree - which
 * removes three suspects before you start looking at the camera. */
void mipi_rx_print_version(void);

#endif /* MIPI_RX_H */
