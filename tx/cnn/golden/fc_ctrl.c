#include "fc_ctrl.h"
#include <string.h>

void fc_ctrl_init(fc_ctrl_t *m) { fc_ctrl_reset(m); }

void fc_ctrl_reset(fc_ctrl_t *m)
{
    m->state = FC_IDLE; m->state_next = FC_IDLE;
    m->layer = 0;       m->layer_next = 0;
    m->group = 0;       m->group_next = 0;
    m->i = 0;           m->i_next = 0;
    m->fill_cnt = 0;    m->fill_cnt_next = 0;
}

static uint16_t rom_row(uint8_t layer, uint8_t group, uint16_t i)
{
    const fc_layer_cfg_t *c = &FC_CFG[layer - 1];
    return (uint16_t)(c->rom_base + (uint16_t)group * c->n_in + i);
}

/* always @(*) */
void fc_ctrl_comb(fc_ctrl_t *m, const fc_ctrl_in_t *in, fc_ctrl_out_t *out)
{
    memset(out, 0, sizeof *out);

    m->state_next    = m->state;
    m->layer_next    = m->layer;
    m->group_next    = m->group;
    m->i_next        = m->i;
    m->fill_cnt_next = m->fill_cnt;

    // ========== input fill (independent of the FSM) ==========
    out->fc_in_ready = (m->fill_cnt < FC_FC1_IN);
    out->fc1_in_we       = in->fc_in_valid && out->fc_in_ready;
    out->fc1_in_waddr    = m->fill_cnt;
    if (out->fc1_in_we)
        m->fill_cnt_next = (uint16_t)(m->fill_cnt + 1);

    // ========== Next State Logic ==========
    switch (m->state)
    {
        case FC_IDLE:
            if (m->fill_cnt > 0)
            {
                m->state_next = FC_RUN;
                m->layer_next = 1;
                m->group_next = 0;
                m->i_next     = 0;
            }
            break;

        case FC_RUN:
        {
            const fc_layer_cfg_t *c = &FC_CFG[m->layer - 1];
            uint8_t avail  = !(m->layer == 1 && m->group == 0) || (m->i < m->fill_cnt);
            uint8_t lastin = (m->i + 1 == c->n_in);
            uint8_t can    = avail && (!lastin || in->hold_free);
            if (can)
            {
                out->mac_en = 1;
                out->first  = (m->i == 0);
                out->last   = lastin;
                if (!lastin)
                    m->i_next = (uint16_t)(m->i + 1);
                else
                {
                    m->i_next = 0;
                    if (m->group + 1 < c->groups)
                    {
                        m->group_next = (uint8_t)(m->group + 1);
                        m->state_next = FC_RUN;
                    }
                    else
                        m->state_next = FC_FLUSH;
                }
            }
            break;
        }

        case FC_FLUSH:
            if (!in->mac_busy && in->hold_free)
            {
                if (m->layer == 1)
                    m->fill_cnt_next = out->fc1_in_we ? 1 : 0; /* fc1_in is free again */
                if (m->layer < FC_LAYERS)
                {
                    m->layer_next = (uint8_t)(m->layer + 1);
                    m->group_next = 0;
                    m->i_next     = 0;
                    m->state_next = FC_RUN;
                }
                else
                {
                    m->layer_next = 0;
                    m->state_next = FC_IDLE;
                }
            }
            break;
    }

    // ========== Output Logic ==========
    out->layer     = m->layer;
    out->group     = m->group;
    out->feature_raddr   = m->i;
    out->bias_addr = m->layer ? (uint8_t)(FC_CFG[m->layer - 1].bias_base + m->group) : 0;
    /* row the next cycle issues: where the counters land after this cycle */
    out->weight_addr  = (m->layer_next >= 1) ? rom_row(m->layer_next, m->group_next, m->i_next) : 0;
}

/* always @(posedge clk) */
void fc_ctrl_seq(fc_ctrl_t *m)
{
    m->state    = m->state_next;
    m->layer    = m->layer_next;
    m->group    = m->group_next;
    m->i        = m->i_next;
    m->fill_cnt = m->fill_cnt_next;
}

const char *fc_state_name(fc_state_t s)
{
    switch (s)
    {
        case FC_IDLE:  return "IDLE";
        case FC_RUN:   return "RUN";
        case FC_FLUSH: return "FLUSH";
        default:       return "?";
    }
}
