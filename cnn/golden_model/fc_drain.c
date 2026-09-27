#include "fc_drain.h"
#include <string.h>

/* same rounding as quantizer.v / quantizer_signed.v: floor shift, then round half to even */
int16_t fc_quant_value(fc_acc_t x, uint8_t scale_exp, uint8_t relu, uint8_t *dbg_sat)
{
    if (relu && x < 0)
        x = 0;
    fc_acc_t q   = x >> scale_exp;                      /* arithmetic shift = floor */
    fc_acc_t rem = scale_exp ? (x & (((fc_acc_t)1 << scale_exp) - 1)) : 0;
    fc_acc_t half = scale_exp ? ((fc_acc_t)1 << (scale_exp - 1)) : 0;
    if (scale_exp && (rem > half || (rem == half && (q & 1))))
        q += 1;
    fc_acc_t lo = relu ? 0 : -32768;
    if (q > 32767) { if (dbg_sat) *dbg_sat = 1; return 32767; }
    if (q < lo)    { if (dbg_sat) *dbg_sat = 1; return (int16_t)lo; }
    return (int16_t)q;
}

void fc_drain_init(fc_drain_t *m)
{
    fc_drain_reset(m);
    m->dbg_sat_cnt = 0;
    m->dbg_overrun_cnt = 0;
}

void fc_drain_reset(fc_drain_t *m)
{
    memset(m->hold, 0, sizeof m->hold); memset(m->hold_next, 0, sizeof m->hold_next);
    m->valid = 0; m->valid_next = 0;
    m->idx = 0;   m->idx_next = 0;
    m->len = 0;   m->len_next = 0;
    m->layer = 0; m->layer_next = 0;
    m->group = 0; m->group_next = 0;
    m->w_dbg_sat = 0; m->w_dbg_overrun = 0;
}

/* always @(*) */
void fc_drain_comb(fc_drain_t *m, const fc_drain_in_t *in, fc_drain_out_t *out)
{
    const fc_layer_cfg_t *c = (m->layer >= 1 && m->layer <= FC_LAYERS) ? &FC_CFG[m->layer - 1] : &FC_CFG[0];

    memcpy(m->hold_next, m->hold, sizeof m->hold);
    m->valid_next = m->valid;
    m->idx_next   = m->idx;
    m->len_next   = m->len;
    m->layer_next = m->layer;
    m->group_next = m->group;
    m->w_dbg_sat = 0;
    m->w_dbg_overrun = 0;

    // ========== Output Logic ==========
    memset(out, 0, sizeof *out);
    out->hold_free = !m->valid;

    uint8_t pop = 0;
    if (m->valid)
    {
        int16_t y = fc_quant_value(m->hold[m->idx], c->scale_exp, c->relu, &m->w_dbg_sat);
        if (m->layer == FC_LAYERS)
        {
            out->logit_valid = 1;
            out->logit_data  = y;
            pop = in->logit_ready;
        }
        else
        {
            out->act_we    = 1;
            out->act_layer = m->layer;
            out->act_waddr = (uint8_t)(m->group * FC_P + m->idx);
            out->act_wdata = (uint16_t)y;
            pop = 1;
        }
    }

    // ========== Next State ==========
    if (pop)
    {
        if (m->idx + 1 >= m->len)
        {
            m->valid_next = 0;
            m->idx_next   = 0;
        }
        else
            m->idx_next = (uint8_t)(m->idx + 1);
    }

    /* a finished group lands here; fc_ctrl guarantees the register is free by then */
    if (in->sum_valid)
    {
        if (m->valid && !(pop && m->idx + 1 >= m->len))
            m->w_dbg_overrun = 1;
        const fc_layer_cfg_t *nc = &FC_CFG[in->layer - 1];
        memcpy(m->hold_next, in->sum, sizeof m->hold_next);
        m->valid_next = 1;
        m->idx_next   = 0;
        m->len_next   = fc_group_len(nc, in->group);
        m->layer_next = in->layer;
        m->group_next = in->group;
    }
}

/* always @(posedge clk) */
void fc_drain_seq(fc_drain_t *m)
{
    memcpy(m->hold, m->hold_next, sizeof m->hold);
    m->valid = m->valid_next;
    m->idx   = m->idx_next;
    m->len   = m->len_next;
    m->layer = m->layer_next;
    m->group = m->group_next;
    if (m->w_dbg_sat)     m->dbg_sat_cnt++;
    if (m->w_dbg_overrun) m->dbg_overrun_cnt++;
}
