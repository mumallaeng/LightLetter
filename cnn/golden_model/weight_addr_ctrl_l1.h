/*
 * Weight Addr Ctrl - conv_l1 (weight_addr_controller_l1-v2)
 *
 *   IDLE (out_ch_sel = 0, mac_done = 0) -(mac_start)-> WEIGHT_CAL (cal_valid = 1, Moore)
 *   WEIGHT_CAL: out_ch_sel == n-1 ? (mac_done = 1, out_ch_sel = 0 -> IDLE) : out_ch_sel++
 *
 *   out_ch_sel -> Weight ROM MUX (OUT_CH 별 144bit x 3, INT16), cal_valid -> MAC Array
 *   conv_l1 은 입력 그룹이 1개라 is_ch35 가 없다.
 */
#ifndef WEIGHT_ADDR_CTRL_L1_H
#define WEIGHT_ADDR_CTRL_L1_H

#include "common.h"

typedef enum
{
    W1_IDLE = 0,
    W1_WEIGHT_CAL
} weight_addr_ctrl_l1_state_t;

/* input ports */
typedef struct
{
    uint8_t mac_start;      /* Total Control FSM */
} weight_addr_ctrl_l1_in_t;

/* output ports */
typedef struct
{
    uint8_t mac_done;       /* -> Total Control FSM */
    uint8_t out_ch_sel;     /* -> Weight ROM MUX */
    uint8_t cal_valid;      /* -> MAC Array (state == WEIGHT_CAL, Moore) */
} weight_addr_ctrl_l1_out_t;

/* registers: reg / reg_next */
typedef struct
{
    uint8_t                     c_out;      /* parameter n: 6 */

    weight_addr_ctrl_l1_state_t state,      state_next;
    uint8_t                     out_ch_sel, out_ch_sel_next;
    uint8_t                     mac_done,   mac_done_next;
} weight_addr_ctrl_l1_t;

void weight_addr_ctrl_l1_reset(weight_addr_ctrl_l1_t *m, uint8_t c_out);
void weight_addr_ctrl_l1_comb(weight_addr_ctrl_l1_t *m, const weight_addr_ctrl_l1_in_t *in,
                              weight_addr_ctrl_l1_out_t *out);
void weight_addr_ctrl_l1_seq(weight_addr_ctrl_l1_t *m);

const char *weight_addr_ctrl_l1_state_name(weight_addr_ctrl_l1_state_t s);

#endif
