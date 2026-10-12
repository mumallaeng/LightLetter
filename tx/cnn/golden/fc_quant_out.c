#include "fc_quant_out.h"
#include <string.h>

/* same rounding as quantizer.v: floor shift, then round half to even */
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

void fc_quant_out_init(fc_quant_out_t *m)
{
    fc_quant_out_reset(m);
    m->dbg_sat_cnt = 0;
    m->dbg_overrun_cnt = 0;
}

void fc_quant_out_reset(fc_quant_out_t *m)
{
    memset(m->hold, 0, sizeof m->hold); memset(m->hold_next, 0, sizeof m->hold_next);
    m->valid = 0; m->valid_next = 0;
    m->idx = 0;   m->idx_next = 0;
    m->len = 0;   m->len_next = 0;
    m->layer = 0; m->layer_next = 0;
    m->group = 0; m->group_next = 0;
    memset(&m->s1, 0, sizeof m->s1); memset(&m->s1_next, 0, sizeof m->s1_next);
    memset(&m->s2, 0, sizeof m->s2); memset(&m->s2_next, 0, sizeof m->s2_next);
    m->w_dbg_sat = 0; m->w_dbg_overrun = 0;
}

int fc_quant_out_empty(const fc_quant_out_t *m)
{
    return !(m->valid || m->s1.valid || m->s2.valid);
}

/* always @(*) */
void fc_quant_out_comb(fc_quant_out_t *m, const fc_quant_out_in_t *in, fc_quant_out_out_t *out)
{
    memcpy(m->hold_next, m->hold, sizeof m->hold);
    m->valid_next = m->valid;
    m->idx_next   = m->idx;
    m->len_next   = m->len;
    m->layer_next = m->layer;
    m->group_next = m->group;
    m->s1_next    = m->s1;
    m->s2_next    = m->s2;
    m->w_dbg_sat = 0;
    m->w_dbg_overrun = 0;

    // ========== Output Logic ==========
    memset(out, 0, sizeof *out);
    out->hold_free = fc_quant_out_empty(m);

    uint8_t s2_logit = m->s2.valid && m->s2.layer == FC_LAYERS;
    uint8_t adv      = !s2_logit || in->logit_ready;
    uint8_t pop      = m->valid && adv;

    if (m->s2.valid)
    {
        if (s2_logit)
        {
            out->logit_valid = 1;
            out->logit_data  = m->s2.y;
        }
        else
        {
            out->feature_we    = 1;
            out->feature_layer = m->s2.layer;
            out->feature_waddr = m->s2.waddr;
            out->feature_wdata = (uint16_t)m->s2.y;
        }
    }

    // ========== Next State ==========
    if (adv)
    {
        if (m->s1.valid)
        {
            const fc_layer_cfg_t *c = &FC_CFG[m->s1.layer - 1];
            m->s2_next.y = fc_quant_value(m->s1.cur, c->scale_exp, c->relu, &m->w_dbg_sat);
        }
        m->s2_next.valid = m->s1.valid;
        m->s2_next.layer = m->s1.layer;
        m->s2_next.waddr = m->s1.waddr;

        m->s1_next.valid = pop;
        if (pop)
        {
            m->s1_next.cur   = m->hold[m->idx];
            m->s1_next.layer = m->layer;
            m->s1_next.waddr = (uint8_t)(m->group * FC_P + m->idx);
        }
    }

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
void fc_quant_out_seq(fc_quant_out_t *m)
{
    memcpy(m->hold, m->hold_next, sizeof m->hold);
    m->valid = m->valid_next;
    m->idx   = m->idx_next;
    m->len   = m->len_next;
    m->layer = m->layer_next;
    m->group = m->group_next;
    m->s1    = m->s1_next;
    m->s2    = m->s2_next;
    if (m->w_dbg_sat)     m->dbg_sat_cnt++;
    if (m->w_dbg_overrun) m->dbg_overrun_cnt++;
}
