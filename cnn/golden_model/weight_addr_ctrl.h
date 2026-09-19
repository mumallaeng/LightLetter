/*
 * Weight Addr Ctrl
 *
 *   IDLE -(mac_start)-> WEIGHT_CAL (out_ch_sel 0 .. N-1) -> mac_done pulse -> IDLE
 */
#ifndef WEIGHT_ADDR_CTRL_H
#define WEIGHT_ADDR_CTRL_H

#include "common.h"

typedef enum
{
    W_IDLE = 0,
    W_WEIGHT_CAL
} wac_state_t;

/* input ports */
typedef struct
{
    uint8_t mac_start;      /* Total Control FSM */
    uint8_t is_ch35;        /* Total Control FSM */
} weight_addr_ctrl_in_t;

/* output ports */
typedef struct
{
    uint8_t mac_done;       /* -> Total Control FSM */
    uint8_t out_ch_sel;     /* -> Weight ROM MUX */
    uint8_t is_ch35;        /* -> Weight ROM */
    uint8_t weight_valid;   /* -> MAC Array (state == WEIGHT_CAL) */
} weight_addr_ctrl_out_t;

/* registers: reg / reg_next */
typedef struct
{
    wac_state_t state,      state_next;
    uint8_t     out_ch_sel, out_ch_sel_next;
    uint8_t     mac_done,   mac_done_next;
} weight_addr_ctrl_t;

void weight_addr_ctrl_reset(weight_addr_ctrl_t *m);
void weight_addr_ctrl_comb(weight_addr_ctrl_t *m, const weight_addr_ctrl_in_t *in,
                           weight_addr_ctrl_out_t *out);
void weight_addr_ctrl_seq(weight_addr_ctrl_t *m);

const char *wac_state_name(wac_state_t s);

#endif
