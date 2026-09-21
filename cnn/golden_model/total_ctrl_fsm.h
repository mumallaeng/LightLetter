/*
 * Total Control FSM
 *
 *   IDLE -> CH02_IMG_IN <-> WAIT_MAC_02 -> WAIT_LB_RST
 *        -> CH35_IMG_IN <-> WAIT_MAC_35 -> STOP -> IDLE
 *
 *   num_passes = 1 (conv1, 입력 1ch) 이면 WAIT_MAC_02 -> STOP 으로 바로 끝난다.
 */
#ifndef TOTAL_CTRL_FSM_H
#define TOTAL_CTRL_FSM_H

#include "common.h"

typedef enum
{
    T_IDLE = 0,
    T_CH02_IMG_IN,
    T_WAIT_MAC_02,
    T_WAIT_LB_RST,
    T_CH35_IMG_IN,
    T_WAIT_MAC_35,
    T_STOP
} total_state_t;

/* input ports */
typedef struct
{
    uint8_t out_valid;      /* 이전 레이어 */
    uint8_t ch_done;        /* 이전 레이어 */
    uint8_t win_valid;      /* Linebuffer Array */
    uint8_t mac_done;       /* Weight Addr Ctrl */
} total_ctrl_fsm_in_t;

/* output ports */
typedef struct
{
    uint8_t out_ready;      /* -> 이전 레이어 */
    uint8_t pixel_valid;    /* -> Linebuffer Array */
    uint8_t phase_clear;    /* -> Linebuffer Array */
    uint8_t mac_start;      /* -> Weight Addr Ctrl */
    uint8_t is_ch35;        /* -> Weight Addr Ctrl (state 에서 생성) */
} total_ctrl_fsm_out_t;

/* registers: reg / reg_next */
typedef struct
{
    uint8_t       num_passes;                       /* parameter: 1 (conv1), 2 (conv2) */

    total_state_t state,       state_next;
    uint8_t       mac_start,   mac_start_next;
    uint8_t       phase_clear, phase_clear_next;
    uint8_t       ch_count,    ch_count_next;
} total_ctrl_fsm_t;

void total_ctrl_fsm_reset(total_ctrl_fsm_t *m, uint8_t num_passes);
void total_ctrl_fsm_comb(total_ctrl_fsm_t *m, const total_ctrl_fsm_in_t *in,
                         total_ctrl_fsm_out_t *out);
void total_ctrl_fsm_seq(total_ctrl_fsm_t *m);

const char *total_state_name(total_state_t s);

#endif
