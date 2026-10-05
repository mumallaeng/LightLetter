/* fc_ctrl: layer / group / input counters of the shared engine (IDLE -> RUN -> FLUSH). */
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
    uint8_t mac_busy;
    uint8_t hold_free;
} fc_ctrl_in_t;

typedef struct
{
    uint8_t  fc_in_ready;
    uint8_t  fc1_in_we;
    uint16_t fc1_in_waddr;
    uint8_t  layer;      /* 1..3 (0 in IDLE) */
    uint8_t  group;
    uint16_t feature_raddr;
    uint16_t weight_addr; /* prefetch for the next cycle */
    uint8_t  bias_addr;
    uint8_t  first, mac_en, last;
} fc_ctrl_out_t;

typedef struct
{
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
