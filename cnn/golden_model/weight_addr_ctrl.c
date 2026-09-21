#include <string.h>
#include "weight_addr_ctrl.h"

const char *wac_state_name(wac_state_t s)
{
    return s == W_IDLE ? "IDLE" : "WEIGHT_CAL";
}

void weight_addr_ctrl_reset(weight_addr_ctrl_t *m, uint8_t c_out)
{
    memset(m, 0, sizeof(*m));
    m->c_out = c_out;
}

/* always @(*) */
void weight_addr_ctrl_comb(weight_addr_ctrl_t *m, const weight_addr_ctrl_in_t *in,
                           weight_addr_ctrl_out_t *out)
{
    /* ---------------- assign ---------------- */
    out->mac_done     = m->mac_done;
    out->out_ch_sel   = m->out_ch_sel;
    out->is_ch35      = in->is_ch35;
    out->weight_valid = (m->state == W_WEIGHT_CAL);

    /* ---------------- next state ---------------- */
    m->state_next      = m->state;
    m->out_ch_sel_next = m->out_ch_sel;
    m->mac_done_next   = m->mac_done;

    switch (m->state)
    {
    case W_IDLE:
        m->out_ch_sel_next = 0;
        m->mac_done_next   = 0;
        if (in->mac_start)
            m->state_next = W_WEIGHT_CAL;
        break;

    case W_WEIGHT_CAL:
        if (m->out_ch_sel == m->c_out - 1)
        {
            m->mac_done_next = 1;
            m->out_ch_sel_next = 0;
            m->state_next    = W_IDLE;
        }
        else
        {
            m->out_ch_sel_next = m->out_ch_sel + 1;
        }
        break;
    }
}

/* always @(posedge clk) */
void weight_addr_ctrl_seq(weight_addr_ctrl_t *m)
{
    m->state      = m->state_next;
    m->out_ch_sel = m->out_ch_sel_next;
    m->mac_done   = m->mac_done_next;
}
