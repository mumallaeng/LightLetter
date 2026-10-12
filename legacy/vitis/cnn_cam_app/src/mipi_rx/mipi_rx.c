/*
 *  mipi_rx.c
 */

#include "mipi_rx.h"
#include "xparameters.h"
#include "xil_io.h"
#include "xil_printf.h"
#include "sleep.h"

/*---------------------------------------------------------------------------
 *  Register map, copied from the IP's own driver headers
 *  (vivado-library/ip/MIPI_*_RX/drivers/.../src/MIPI_*_RX.h)
 *
 *  Written out here rather than including those headers so that this file
 *  compiles even if the platform has not regenerated the IP drivers yet -
 *  a common state right after a block design change.
 *---------------------------------------------------------------------------*/
#define MIPI_CR_OFFSET          0x00
#define MIPI_VERSION_OFFSET     0x0C

#define MIPI_CR_RESET_MASK      0x1
#define MIPI_CR_ENABLE_MASK     0x2

#define MIPI_VER_MAJOR_SHIFT    16
#define MIPI_VER_MAJOR_MASK     0xFFFF0000
#define MIPI_VER_MINOR_MASK     0x0000FFFF

/*---------------------------------------------------------------------------
 *  Base addresses
 *
 *  These come from xparameters.h, which Vitis regenerates from the .xsa. If
 *  the names below do not exist, the platform has not been rebuilt since the
 *  block design changed - rebuild the platform, do not edit these by hand.
 *---------------------------------------------------------------------------*/
#define DPHY_BASE   XPAR_MIPI_D_PHY_RX_0_S_AXI_LITE_BASEADDR
#define CSI2_BASE   XPAR_MIPI_CSI_2_RX_0_S_AXI_LITE_BASEADDR

void mipi_rx_reset(void)
{
    /* Assert reset, clear enable. Consumer first : CSI-2, then D-PHY. */
    Xil_Out32(CSI2_BASE + MIPI_CR_OFFSET, MIPI_CR_RESET_MASK);
    Xil_Out32(DPHY_BASE + MIPI_CR_OFFSET, MIPI_CR_RESET_MASK);
    usleep(1000);
}

void mipi_rx_enable(void)
{
    /* Release reset and enable. Producer first : D-PHY, then CSI-2.
     *
     * Writing ENABLE alone also clears RESET, since they are separate bits in
     * the same register and we write the whole word. That is intentional and
     * matches Digilent's code. */
    Xil_Out32(DPHY_BASE + MIPI_CR_OFFSET, MIPI_CR_ENABLE_MASK);
    Xil_Out32(CSI2_BASE + MIPI_CR_OFFSET, MIPI_CR_ENABLE_MASK);
    usleep(1000);
}

void mipi_rx_print_version(void)
{
    u32 v;

    v = Xil_In32(DPHY_BASE + MIPI_VERSION_OFFSET);
    xil_printf("  MIPI_D_PHY_RX  v%d.%d  @ 0x%08X\r\n",
               (int)((v & MIPI_VER_MAJOR_MASK) >> MIPI_VER_MAJOR_SHIFT),
               (int)(v & MIPI_VER_MINOR_MASK),
               (unsigned)DPHY_BASE);

    v = Xil_In32(CSI2_BASE + MIPI_VERSION_OFFSET);
    xil_printf("  MIPI_CSI_2_RX  v%d.%d  @ 0x%08X\r\n",
               (int)((v & MIPI_VER_MAJOR_MASK) >> MIPI_VER_MAJOR_SHIFT),
               (int)(v & MIPI_VER_MINOR_MASK),
               (unsigned)CSI2_BASE);
}
