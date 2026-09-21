#include <string.h>
#include "weight_rom.h"

void weight_rom_reset(weight_rom_t *m, const wgt_t *weight, int c_out, int c_in)
{
    memset(m, 0, sizeof(*m));

    /* 입력 채널 ic -> 그룹 ic / 3, lane ic % 3 */
    for (int oc = 0; oc < c_out; oc++)
        for (int ic = 0; ic < c_in; ic++)
            for (int k = 0; k < CE_KK; k++)
                m->data[oc][ic / CE_LANES][(ic % CE_LANES) * CE_KK + k] =
                    weight[(oc * c_in + ic) * CE_KK + k];
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
