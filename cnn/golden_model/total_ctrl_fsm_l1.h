/*
 * Total Control FSM - conv_l1 (frce_controller_fsm_l1-v2)
 *
 *   IDLE -(out_valid)-> IMG_IN -(win_valid: mac_start = 1)-> WAIT_MAC
 *   WAIT_MAC -(mac_done, ch_count != 1)-> IMG_IN
 *   WAIT_MAC -(mac_done, ch_count == 1: phase_clear = 1)-> STOP -> IDLE
 *
 *   out_ready   = (state == IDLE | state == IMG_IN) & ~win_valid
 *   pixel_valid = out_valid & out_ready
 *   ch_count   += ch_done & pixel_valid   (STOP 에서 0)
 */
#ifndef TOTAL_CTRL_FSM_L1_H
#define TOTAL_CTRL_FSM_L1_H

#include "common.h"

typedef enum
{
    T1_IDLE = 0,
    T1_IMG_IN,
    T1_WAIT_MAC,
    T1_STOP
} total_ctrl_fsm_l1_state_t;

/* input ports */
typedef struct
{
    uint8_t out_valid;      /* 이전 단 */
    uint8_t ch_done;        /* 이전 단: 마지막 픽셀 */
    uint8_t win_valid;      /* Line Buffer */
    uint8_t mac_done;       /* Weight Addr Ctrl */
} total_ctrl_fsm_l1_in_t;

/* output ports */
typedef struct
{
    uint8_t out_ready;      /* -> 이전 단 */
    uint8_t pixel_valid;    /* -> Line Buffer */
    uint8_t phase_clear;    /* -> Line Buffer */
    uint8_t mac_start;      /* -> Weight Addr Ctrl */
} total_ctrl_fsm_l1_out_t;

/* registers: reg / reg_next */
typedef struct
{
    total_ctrl_fsm_l1_state_t state,       state_next;
    uint8_t                   mac_start,   mac_start_next;
    uint8_t                   phase_clear, phase_clear_next;
    uint8_t                   ch_count,    ch_count_next;
} total_ctrl_fsm_l1_t;

void total_ctrl_fsm_l1_reset(total_ctrl_fsm_l1_t *m);
void total_ctrl_fsm_l1_comb(total_ctrl_fsm_l1_t *m, const total_ctrl_fsm_l1_in_t *in,
                            total_ctrl_fsm_l1_out_t *out);
void total_ctrl_fsm_l1_seq(total_ctrl_fsm_l1_t *m);

const char *total_ctrl_fsm_l1_state_name(total_ctrl_fsm_l1_state_t s);

#endif
