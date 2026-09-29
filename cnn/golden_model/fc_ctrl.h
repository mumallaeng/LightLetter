/* fc_ctrl: layer / group / input counters of the shared engine.
 *   IDLE  -> RUN when the first input of a frame has arrived
 *   RUN   : one input per clock (FC1 group 0 only as far as inputs have arrived); the
 *           group's first input carries `first` (acc = bias + x*w instead of acc + x*w) and
 *           its last input waits until the drain register is free
 *   FLUSH : after a layer's last group, until the pipeline and the drain are empty
 * weight_addr is the row the NEXT cycle needs (BRAM prefetch); the same row is re-addressed
 * while RUN stalls. fill_cnt counts this frame's inputs in fc1_in and clears after FC1. */
#ifndef FC_CTRL_H
#define FC_CTRL_H

#include "fc_common.h"

typedef enum
{
    FC_IDLE = 0,
    FC_RUN,
    FC_FLUSH
} fc_state_t;

typedef struct
{
    uint8_t fc_in_valid;
    uint8_t mac_busy;   /* <- fc_mac */
    uint8_t hold_free;  /* <- fc_drain */
} fc_ctrl_in_t;

typedef struct
{
    uint8_t  fc_in_ready;
    uint8_t  fc1_in_we;      /* fc1_in write of fc_in_data at fc1_in_waddr */
    uint16_t fc1_in_waddr;
    uint8_t  layer;      /* 1..3 (0 in IDLE) */
    uint8_t  group;
    uint16_t feature_raddr; /* input index i, read from this layer's input buffer */
    uint16_t weight_addr; /* prefetch for the next cycle */
    uint8_t  bias_addr;   /* the group's bias row, read with the first input */
    uint8_t  first, mac_en, last;
} fc_ctrl_out_t;

typedef struct
{
    /* registers: reg / reg_next */
    fc_state_t state, state_next;
    uint8_t    layer, layer_next;
    uint8_t    group, group_next;
    uint16_t   i, i_next;
    uint16_t   fill_cnt, fill_cnt_next;
} fc_ctrl_t;

void fc_ctrl_init(fc_ctrl_t *m);
void fc_ctrl_reset(fc_ctrl_t *m);
void fc_ctrl_comb(fc_ctrl_t *m, const fc_ctrl_in_t *in, fc_ctrl_out_t *out);
void fc_ctrl_seq(fc_ctrl_t *m);
const char *fc_state_name(fc_state_t s);

#endif
