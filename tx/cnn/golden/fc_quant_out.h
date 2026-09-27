/* fc_quant_out: holds one group's P sums and quantizes them one per clock: FC1 / FC2 to the next feature buffer, FC3 to the logit stream. */
#ifndef FC_QUANT_OUT_H
#define FC_QUANT_OUT_H

#include "fc_common.h"

typedef struct
{
    uint8_t  sum_valid;
    fc_acc_t sum[FC_P];
    uint8_t  layer, group;
    uint8_t  logit_ready;
} fc_quant_out_in_t;

typedef struct
{
    uint8_t  hold_free;
    uint8_t  feature_we;
    uint8_t  feature_layer;
    uint8_t  feature_waddr;
    uint16_t feature_wdata;
    int16_t  logit_data;
    uint8_t  logit_valid;
} fc_quant_out_out_t;

typedef struct
{
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
} fc_quant_out_t;

/* y = clip(round_half_even(x / 2^scale_exp)), ReLU first when relu; sets *dbg_sat on clamp */
int16_t fc_quant_value(fc_acc_t x, uint8_t scale_exp, uint8_t relu, uint8_t *dbg_sat);

void fc_quant_out_init(fc_quant_out_t *m);
void fc_quant_out_reset(fc_quant_out_t *m);
void fc_quant_out_comb(fc_quant_out_t *m, const fc_quant_out_in_t *in, fc_quant_out_out_t *out);
void fc_quant_out_seq(fc_quant_out_t *m);

#endif
