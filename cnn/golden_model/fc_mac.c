#include <string.h>
#include "fc_mac.h"

void fc_mac_init(fc_mac_t *m, const fc_param_t *p)
{
    m->p = *p;
    fc_mac_reset(m);
}

void fc_mac_reset(fc_mac_t *m)
{
    memset(m->prod, 0, sizeof(m->prod));
    memset(m->prod_next, 0, sizeof(m->prod_next));

    m->prod_valid = 0;  m->prod_valid_next = 0;
    m->sum        = 0;  m->sum_next        = 0;
    m->sum_valid  = 0;  m->sum_valid_next  = 0;

    m->w_dbg_ch_ovf   = 0;
    m->dbg_ch_ovf_cnt = 0;
}

/* always @(*) */
void fc_mac_comb(fc_mac_t *m, const fc_mac_in_t *in, fc_mac_out_t *out)
{
    // ========== Stage 1: LANES products ==========
    for (uint8_t i = 0; i < m->p.lanes; i++)
        m->prod_next[i] = (int64_t)(uint32_t)in->x[i] * (int64_t)in->w[i];
    m->prod_valid_next = in->mac_en;

    // ========== Stage 2: adder tree, wrapped to CH_W like the RTL ==========
    int64_t acc = 0;
    for (uint8_t i = 0; i < m->p.lanes; i++)
        acc += m->prod[i];

    m->sum_next       = ob_sext(acc, OB_CH_W);
    m->sum_valid_next = m->prod_valid;

    // ========== Output Logic ==========
    out->ch_result = m->sum;
    out->mac_valid = m->sum_valid;

    m->w_dbg_ch_ovf = m->prod_valid && !ob_fits(acc, OB_CH_W);
}

/* always @(posedge clk) */
void fc_mac_seq(fc_mac_t *m)
{
    memcpy(m->prod, m->prod_next, sizeof(m->prod));
    m->prod_valid = m->prod_valid_next;
    m->sum        = m->sum_next;
    m->sum_valid  = m->sum_valid_next;

    m->dbg_ch_ovf_cnt += m->w_dbg_ch_ovf;
}
