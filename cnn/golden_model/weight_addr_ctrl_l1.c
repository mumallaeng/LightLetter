#include <string.h>
#include "weight_addr_ctrl_l1.h"

const char *weight_addr_ctrl_l1_state_name(weight_addr_ctrl_l1_state_t s)
{
    return s == W1_IDLE ? "IDLE" : "WEIGHT_CAL";
}

void weight_addr_ctrl_l1_reset(weight_addr_ctrl_l1_t *m, uint8_t c_out)
{
    memset(m, 0, sizeof(*m));
    m->c_out = c_out;
}

/* always @(*) */
void weight_addr_ctrl_l1_comb(weight_addr_ctrl_l1_t *m, const weight_addr_ctrl_l1_in_t *in,
                              weight_addr_ctrl_l1_out_t *out)
{
    /* ---------------- assign ---------------- */
    out->mac_done   = m->mac_done;
    out->out_ch_sel = m->out_ch_sel;
    out->cal_valid  = (m->state == W1_WEIGHT_CAL);

    /* ---------------- next state ---------------- */
    m->state_next      = m->state;
    m->out_ch_sel_next = m->out_ch_sel;
    m->mac_done_next   = m->mac_done;

    switch (m->state)
    {
    case W1_IDLE:
        m->out_ch_sel_next = 0;
        m->mac_done_next   = 0;
        if (in->mac_start)
            m->state_next = W1_WEIGHT_CAL;
        break;

    case W1_WEIGHT_CAL:
        if (m->out_ch_sel == m->c_out - 1)
        {
            m->mac_done_next   = 1;
            m->out_ch_sel_next = 0;
            m->state_next      = W1_IDLE;
        }
        else
        {
            m->out_ch_sel_next = m->out_ch_sel + 1;
        }
        break;
    }
}

/* always @(posedge clk) */
void weight_addr_ctrl_l1_seq(weight_addr_ctrl_l1_t *m)
{
    m->state      = m->state_next;
    m->out_ch_sel = m->out_ch_sel_next;
    m->mac_done   = m->mac_done_next;
}
