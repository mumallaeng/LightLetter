/*
 *  gamma.c
 */

#include "gamma.h"
#include "xparameters.h"
#include "xil_io.h"
#include "xil_printf.h"

/*
 * The generated symbol name depends on how Vitis names an IP that has no
 * driver of its own. Digilent's C++ code used the first form. Which one you
 * get can also change with the AXI-Lite interface name (this IP calls it
 * "s_axil", not "S_AXI"), so try them in order rather than guessing.
 *
 * If none of these exist, open xparameters.h and search for
 * GAMMACORRECTION - then add that spelling here.
 */
#if   defined(XPAR_AXI_GAMMACORRECTION_0_BASEADDR)
#  define GAMMA_BASE  XPAR_AXI_GAMMACORRECTION_0_BASEADDR
#elif defined(XPAR_AXI_GAMMACORRECTION_0_S_AXIL_BASEADDR)
#  define GAMMA_BASE  XPAR_AXI_GAMMACORRECTION_0_S_AXIL_BASEADDR
#elif defined(XPAR_AXI_GAMMACORRECTION_0_S_AXI_BASEADDR)
#  define GAMMA_BASE  XPAR_AXI_GAMMACORRECTION_0_S_AXI_BASEADDR
#else
#  error "AXI_GammaCorrection base address not found in xparameters.h - \
rebuild the Vitis platform from a freshly exported XSA, then check the name."
#endif

static Gamma_factor cur = GAMMA_1_0;

void gamma_init(void)
{
    gamma_set(GAMMA_1_1_8);     /* Digilent's default */
}

void gamma_set(Gamma_factor f)
{
    if (f > GAMMA_1_2_2) {
        return;
    }
    Xil_Out32(GAMMA_BASE, (u32)f);
    cur = f;
}

Gamma_factor gamma_get(void)
{
    return cur;
}

const char *gamma_name(Gamma_factor f)
{
    switch (f) {
    case GAMMA_1_0:     return "1.0 (linear)";
    case GAMMA_1_1_2:   return "1/1.2";
    case GAMMA_1_1_5:   return "1/1.5";
    case GAMMA_1_1_8:   return "1/1.8";
    case GAMMA_1_2_2:   return "1/2.2";
    default:            return "?";
    }
}
