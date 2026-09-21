/*
 * Weight Addr Ctrl - conv_l2
 *
 *   IDLE -(mac_start)-> WEIGHT_CAL (out_ch_sel 0 .. N-1) -> mac_done pulse -> IDLE
 */
#ifndef WEIGHT_ADDR_CTRL_L2_H
#define WEIGHT_ADDR_CTRL_L2_H

#include "common.h"

typedef enum
{
    W2_IDLE = 0,
    W2_WEIGHT_CAL
} weight_addr_ctrl_l2_state_t;

/* input ports */
typedef struct
{
    uint8_t mac_start;      /* Total Control FSM */
    uint8_t is_ch35;        /* Total Control FSM */
} weight_addr_ctrl_l2_in_t;

/* output ports */
typedef struct
{
    uint8_t mac_done;       /* -> Total Control FSM */
    uint8_t out_ch_sel;     /* -> Weight ROM MUX */
    uint8_t is_ch35;        /* -> Weight ROM */
    uint8_t cal_valid;      /* -> MAC Array (state == WEIGHT_CAL, Moore) */
} weight_addr_ctrl_l2_out_t;

/* registers: reg / reg_next */
typedef struct
{
    uint8_t     c_out;                              /* parameter: 6 (conv1), 16 (conv2) */

    weight_addr_ctrl_l2_state_t state,      state_next;
    uint8_t     out_ch_sel, out_ch_sel_next;
    uint8_t     mac_done,   mac_done_next;
} weight_addr_ctrl_l2_t;

void weight_addr_ctrl_l2_reset(weight_addr_ctrl_l2_t *m, uint8_t c_out);
void weight_addr_ctrl_l2_comb(weight_addr_ctrl_l2_t *m, const weight_addr_ctrl_l2_in_t *in,
                           weight_addr_ctrl_l2_out_t *out);
void weight_addr_ctrl_l2_seq(weight_addr_ctrl_l2_t *m);

const char *weight_addr_ctrl_l2_state_name(weight_addr_ctrl_l2_state_t s);

#endif
