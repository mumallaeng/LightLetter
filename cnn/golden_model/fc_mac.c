#include <string.h>
#include "fc_mac.h"

/* one balanced adder level: pairs nodes up, an odd node moves up untouched */
static uint8_t tree_level(const ob_ch_t *in, uint8_t n, ob_ch_t *out, uint8_t *ovf)
{
    uint8_t cur = (uint8_t)((n + 1) >> 1);

    for (uint8_t g = 0; g < cur; g++)
    {
        int64_t v = (2 * g + 1 < n) ? in[2 * g] + in[2 * g + 1] : in[2 * g];
        *ovf |= !ob_fits(v, OB_CH_W);
        out[g] = ob_sext(v, OB_CH_W);
    }
    return cur;
}

void fc_mac_init(fc_mac_t *m, const fc_param_t *p)
{
    m->p = *p;

    uint8_t levels = 1;
    while ((1u << levels) < p->lanes)
        levels++;
    m->levels = (p->lanes > 1) ? levels : 1;
    m->split  = (uint8_t)(m->levels / 2);
    m->midn   = (uint8_t)((p->lanes + (1u << m->split) - 1) >> m->split);

    fc_mac_reset(m);
}

void fc_mac_reset(fc_mac_t *m)
{
    memset(m->prod, 0, sizeof(m->prod));
    memset(m->prod_next, 0, sizeof(m->prod_next));
    memset(m->mid, 0, sizeof(m->mid));
    memset(m->mid_next, 0, sizeof(m->mid_next));

    m->prod_valid = 0;  m->prod_valid_next = 0;
    m->mid_valid  = 0;  m->mid_valid_next  = 0;
    m->sum        = 0;  m->sum_next        = 0;
    m->sum_valid  = 0;  m->sum_valid_next  = 0;

    m->w_dbg_ch_ovf   = 0;
    m->dbg_ch_ovf_cnt = 0;
}

/* always @(*) */
void fc_mac_comb(fc_mac_t *m, const fc_mac_in_t *in, fc_mac_out_t *out)
{
    ob_ch_t node[FC_MAX_LANES];
    uint8_t ovf = 0;

    // ========== Stage 1: LANES products ==========
    for (uint8_t i = 0; i < m->p.lanes; i++)
        m->prod_next[i] = (int64_t)(uint32_t)in->x[i] * (int64_t)in->w[i];
    m->prod_valid_next = in->mac_en;

    // ========== Stage 2: adder tree up to the split ==========
    uint8_t n = m->p.lanes;
    for (uint8_t i = 0; i < n; i++)
        node[i] = ob_sext(m->prod[i], OB_CH_W);

    for (uint8_t lv = 1; lv <= m->split; lv++)
        n = tree_level(node, n, node, &ovf);

    memcpy(m->mid_next, node, sizeof(ob_ch_t) * n);
    m->mid_valid_next = m->prod_valid;

    // ========== Stage 3: the split down to one value ==========
    memcpy(node, m->mid, sizeof(ob_ch_t) * m->midn);
    n = m->midn;

    for (uint8_t lv = (uint8_t)(m->split + 1); lv <= m->levels; lv++)
        n = tree_level(node, n, node, &ovf);

    m->sum_next       = node[0];
    m->sum_valid_next = m->mid_valid;

    // ========== Output Logic ==========
    out->ch_result = m->sum;
    out->mac_valid = m->sum_valid;

    m->w_dbg_ch_ovf = (m->prod_valid || m->mid_valid) && ovf;
}

/* always @(posedge clk) */
void fc_mac_seq(fc_mac_t *m)
{
    memcpy(m->prod, m->prod_next, sizeof(m->prod));
    memcpy(m->mid, m->mid_next, sizeof(m->mid));
    m->prod_valid = m->prod_valid_next;
    m->mid_valid  = m->mid_valid_next;
    m->sum        = m->sum_next;
    m->sum_valid  = m->sum_valid_next;

    m->dbg_ch_ovf_cnt += m->w_dbg_ch_ovf;
}
