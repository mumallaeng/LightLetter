/*
 *  gamma.h
 *
 *  AXI_GammaCorrection control. One 32 bit register, one 3 bit field.
 *
 *  ---------------------------------------------------------------------
 *  !! THIS IP IS NOT OPTIONAL, DESPITE ITS NAME !!
 *
 *  It is easy to look at "gamma correction" and conclude it is a cosmetic
 *  stage you can leave out of a first build. It is not. Read the port widths:
 *
 *      AXI_BayerToRGB       m_axis_video_tdata  32 bit
 *      AXI_GammaCorrection  s_axis_video_tdata  32 bit
 *                           m_axis_video_tdata  24 bit
 *      AXI VDMA S2MM        24 bit
 *      v_axi4s_vid_out      24 bit  (RGB 8 bits per component)
 *
 *  AXI_BayerToRGB emits TEN bits per colour component, packed as
 *  "00" & R(10) & B(10) & G(10) = 32 bits. Nothing downstream accepts that.
 *  AXI_GammaCorrection is the only thing in the design that converts 10 bits
 *  per component down to 8, and it does that by indexing a stored curve.
 *  The gamma shape is a side effect of the lookup; the bit depth reduction is
 *  the part the pipeline cannot do without.
 *
 *  Remove this IP and the block design will not even connect - Vivado refuses
 *  the 32-to-24 bit stream join. If you force it with a width converter, you
 *  get a picture built from the wrong bits.
 *
 *  ---------------------------------------------------------------------
 *  IF YOU WANT "NO GAMMA"
 *
 *  Write GAMMA_1_0. Factor 1.0 is a linear curve, so the IP becomes a pure
 *  10-to-8 bit truncation and the picture is untouched. That is the honest way
 *  to take gamma out of the equation, and it costs nothing in the PL.
 *
 *  The register resets to 0, which IS GAMMA_1_0. So a design that never calls
 *  anything in this file still produces a correct picture, just a flat looking
 *  one. That is probably why it is easy to believe the IP does nothing.
 */

#ifndef GAMMA_H
#define GAMMA_H

#include "xil_types.h"

/* Values are the 3 bit field in the IP's only register, taken from
 * StoredGammaCoefs.vhd. */
typedef enum {
    GAMMA_1_0   = 0,    /* linear - pure 10 to 8 bit conversion */
    GAMMA_1_1_2 = 1,
    GAMMA_1_1_5 = 2,
    GAMMA_1_1_8 = 3,    /* Digilent's default */
    GAMMA_1_2_2 = 4
} Gamma_factor;

void          gamma_init(void);         /* sets 1/1.8, Digilent's default */
void          gamma_set(Gamma_factor f);
Gamma_factor  gamma_get(void);
const char   *gamma_name(Gamma_factor f);

#endif /* GAMMA_H */
