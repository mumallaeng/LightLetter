#include <string.h>
#include "total_ctrl_fsm_l2.h"

const char *total_ctrl_fsm_l2_state_name(total_ctrl_fsm_l2_state_t s)
{
    static const char *name[] = {
        "IDLE", "CH02_IMG_IN", "WAIT_MAC_02", "WAIT_LB_RST",
        "CH35_IMG_IN", "WAIT_MAC_35", "STOP"
    };
    return name[s];
}

void total_ctrl_fsm_l2_reset(total_ctrl_fsm_l2_t *m, uint8_t num_passes)
{
    memset(m, 0, sizeof(*m));
    m->num_passes = num_passes;
}

/* always @(*) */
void total_ctrl_fsm_l2_comb(total_ctrl_fsm_l2_t *m, const total_ctrl_fsm_l2_in_t *in,
                         total_ctrl_fsm_l2_out_t *out)
{
    // ========== Output Logic ==========
    // ----- out_ready -----
    uint8_t in_state = (m->state == T2_IDLE) ||
                       (m->state == T2_CH02_IMG_IN) ||
                       (m->state == T2_CH35_IMG_IN);
    out->out_ready = in_state && !in->win_valid;

    // ----- pixel_valid -----
    out->pixel_valid = in->out_valid && out->out_ready;

    out->phase_clear = m->phase_clear;
    out->mac_start   = m->mac_start;

    /* is_ch35 : 지금 MAC 이 도는 pass 를 state 에서 직접 만든다.
     * 이전 레이어 신호를 그대로 쓰면 pass 경계에서 윈도우(과거)와
     * 입력 버스(현재)가 어긋나 마지막 윈도우가 반대 그룹 weight 를 읽는다. */
    out->is_ch35  = (m->state == T2_CH35_IMG_IN) || (m->state == T2_WAIT_MAC_35);

    // ========== Next State Logic ==========
    m->state_next       = m->state;
    m->mac_start_next   = m->mac_start;
    m->phase_clear_next = m->phase_clear;
    m->ch_count_next    = m->ch_count;

    switch (m->state)
    {
    case T2_IDLE:
        m->mac_start_next   = 0;
        m->phase_clear_next = 0;
        if (in->out_valid)
            m->state_next = T2_CH02_IMG_IN;
        break;

    case T2_CH02_IMG_IN:
        if (in->win_valid)
        {
            m->mac_start_next = 1;
            m->state_next     = T2_WAIT_MAC_02;
        }
        break;

    case T2_WAIT_MAC_02:
        m->mac_start_next = 0;
        if (in->mac_done)
        {
            if (m->ch_count == m->num_passes)
            {
                m->phase_clear_next = 1;
                m->state_next       = T2_STOP;
            }
            else if (m->ch_count == 1)
            {
                m->phase_clear_next = 1;
                m->state_next       = T2_WAIT_LB_RST;
            }
            else
            {
                m->state_next = T2_CH02_IMG_IN;
            }
        }
        break;

    case T2_WAIT_LB_RST:
        m->phase_clear_next = 0;
        m->state_next       = T2_CH35_IMG_IN;
        break;

    case T2_CH35_IMG_IN:
        if (in->win_valid)
        {
            m->mac_start_next = 1;
            m->state_next     = T2_WAIT_MAC_35;
        }
        break;

    case T2_WAIT_MAC_35:
        m->mac_start_next = 0;
        if (in->mac_done)
        {
            if (m->ch_count == 2)
            {
                m->phase_clear_next = 1;
                m->state_next       = T2_STOP;
            }
            else
            {
                m->state_next = T2_CH35_IMG_IN;
            }
        }
        break;

    case T2_STOP:
        m->phase_clear_next = 0;
        m->state_next       = T2_IDLE;
        break;
    }

    /* ch_count += ch_done & pixel_valid, STOP 에서 clear */
    if (m->state == T2_STOP)
        m->ch_count_next = 0;
    else if (in->ch_done && out->pixel_valid)
        m->ch_count_next = m->ch_count + 1;
}

/* always @(posedge clk) */
void total_ctrl_fsm_l2_seq(total_ctrl_fsm_l2_t *m)
{
    m->state       = m->state_next;
    m->mac_start   = m->mac_start_next;
    m->phase_clear = m->phase_clear_next;
    m->ch_count    = m->ch_count_next;
}
