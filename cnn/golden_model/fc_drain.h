/* fc_drain: holds one group's P sums and quantizes them one per clock in lane order.
 * FC1/FC2 values go to the next layer's activation buffer (feature_we / feature_waddr), FC3 values
 * leave on the logit stream and wait for logit_ready. hold_free tells fc_ctrl the register
 * is empty, so a group's last input is only issued when its sums have somewhere to land. */
#ifndef FC_DRAIN_H
#define FC_DRAIN_H

#include "fc_common.h"

typedef struct
{
    uint8_t  sum_valid;  /* <- fc_mac */
    fc_acc_t sum[FC_P];
    uint8_t  layer, group;
    uint8_t  logit_ready;
} fc_drain_in_t;

typedef struct
{
    uint8_t  hold_free;
    uint8_t  feature_we;     /* write into fc2_in (layer 1) or fc3_in (layer 2) */
    uint8_t  feature_layer;  /* 1 or 2 */
    uint8_t  feature_waddr;  /* neuron index = group * P + lane */
    uint16_t feature_wdata;
    int16_t  logit_data; /* signed */
    uint8_t  logit_valid;
} fc_drain_out_t;

typedef struct
{
    /* registers: reg / reg_next */
    fc_acc_t hold[FC_P], hold_next[FC_P];
    uint8_t  valid, valid_next;
    uint8_t  idx, idx_next;
    uint8_t  len, len_next;
    uint8_t  layer, layer_next;
    uint8_t  group, group_next;

    /* debug: clamped values, and sums that arrived while the register was still full */
    uint32_t dbg_sat_cnt;
    uint32_t dbg_overrun_cnt;
    uint8_t  w_dbg_sat, w_dbg_overrun;
} fc_drain_t;

/* y = clip(round_half_even(x / 2^scale_exp)), ReLU first when relu; sets *dbg_sat on clamp */
int16_t fc_quant_value(fc_acc_t x, uint8_t scale_exp, uint8_t relu, uint8_t *dbg_sat);

void fc_drain_init(fc_drain_t *m);
void fc_drain_reset(fc_drain_t *m);
void fc_drain_comb(fc_drain_t *m, const fc_drain_in_t *in, fc_drain_out_t *out);
void fc_drain_seq(fc_drain_t *m);

#endif
