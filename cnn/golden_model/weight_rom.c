#include <string.h>
#include "weight_rom.h"

void weight_rom_reset(weight_rom_t *m,
                      const wgt_t weight[L2_OUT_CH][L2_IN_CH][L2_K][L2_K])
{
    memset(m, 0, sizeof(*m));

    for (int oc = 0; oc < L2_OUT_CH; oc++)
        for (int grp = 0; grp < L2_NUM_PASSES; grp++)
            for (int lane = 0; lane < L2_LANES; lane++)
                for (int ky = 0; ky < L2_K; ky++)
                    for (int kx = 0; kx < L2_K; kx++)
                        m->data[oc][grp][lane * 9 + ky * 3 + kx] =
                            weight[oc][grp * L2_LANES + lane][ky][kx];
}

/* always @(*) */
void weight_rom_comb(weight_rom_t *m, const weight_rom_in_t *in,
                     weight_rom_out_t *out)
{
    /* ---------------- assign: ROM 출력 -> out_ch_sel MUX ---------------- */
    memcpy(out->weight, m->data[in->out_ch_sel][m->grp_q], sizeof(out->weight));
    out->grp = m->grp_q;

    /* ---------------- next state ---------------- */
    m->grp_q_next = in->is_ch35;
}

/* always @(posedge clk) */
void weight_rom_seq(weight_rom_t *m)
{
    m->grp_q = m->grp_q_next;
}
