#include <string.h>
#include "total_ctrl_fsm_l1.h"

const char *total_ctrl_fsm_l1_state_name(total_ctrl_fsm_l1_state_t s)
{
    static const char *name[] = {"IDLE", "IMG_IN", "WAIT_MAC", "STOP"};
    return name[s];
}

void total_ctrl_fsm_l1_reset(total_ctrl_fsm_l1_t *m)
{
    memset(m, 0, sizeof(*m));
}

/* always @(*) */
void total_ctrl_fsm_l1_comb(total_ctrl_fsm_l1_t *m, const total_ctrl_fsm_l1_in_t *in,
                            total_ctrl_fsm_l1_out_t *out)
{
    // ========== Output Logic ==========
    // ----- out_ready -----
    uint8_t in_state = (m->state == T1_IDLE) || (m->state == T1_IMG_IN);
    out->out_ready = in_state && !in->win_valid;

    // ----- pixel_valid -----
    out->pixel_valid = in->out_valid && out->out_ready;

    out->phase_clear = m->phase_clear;
    out->mac_start   = m->mac_start;

    // ========== Next State Logic ==========
    m->state_next       = m->state;
    m->mac_start_next   = m->mac_start;
    m->phase_clear_next = m->phase_clear;
    m->ch_count_next    = m->ch_count;

    switch (m->state)
    {
    case T1_IDLE:
        m->mac_start_next   = 0;
        m->phase_clear_next = 0;
        if (in->out_valid)
            m->state_next = T1_IMG_IN;
        break;

    case T1_IMG_IN:
        if (in->win_valid)
        {
            m->mac_start_next = 1;
            m->state_next     = T1_WAIT_MAC;
        }
        break;

    case T1_WAIT_MAC:
        m->mac_start_next = 0;
        if (in->mac_done)
        {
            if (m->ch_count == 1)
            {
                m->phase_clear_next = 1;
                m->state_next       = T1_STOP;
            }
            else
            {
                m->state_next = T1_IMG_IN;
            }
        }
        break;

    case T1_STOP:
        m->phase_clear_next = 0;
        m->state_next       = T1_IDLE;
        break;
    }

    /* ch_count += ch_done & pixel_valid, STOP 에서 clear */
    if (m->state == T1_STOP)
        m->ch_count_next = 0;
    else if (in->ch_done && out->pixel_valid)
        m->ch_count_next = m->ch_count + 1;
}

/* always @(posedge clk) */
void total_ctrl_fsm_l1_seq(total_ctrl_fsm_l1_t *m)
{
    m->state       = m->state_next;
    m->mac_start   = m->mac_start_next;
    m->phase_clear = m->phase_clear_next;
    m->ch_count    = m->ch_count_next;
}
