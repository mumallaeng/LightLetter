/* fc_mac: P multipliers and P accumulators, 3-cycle latency; the group's first input starts from the bias. */
#ifndef FC_MAC_ACC_H
#define FC_MAC_ACC_H

#include "fc_common.h"

typedef struct
{
    uint16_t feature;
    int16_t  weight[FC_P];
    int32_t  bias[FC_P];
    uint8_t  first;      /* acc <= bias + feature * weight */
    uint8_t  mac_en;     /* acc <= acc + feature * weight */
    uint8_t  last;
    uint8_t  layer;
    uint8_t  group;
} fc_mac_in_t;

typedef struct
{
    uint8_t  sum_valid;
    fc_acc_t sum[FC_P];
    uint8_t  layer, group;
    uint8_t  busy;
} fc_mac_out_t;

typedef struct
{
    uint16_t feature;
    int16_t  weight[FC_P];
    uint8_t  first, mac, last, layer, group, valid;
} fc_mac_s1_t;

typedef struct
{
    int32_t  prod[FC_P];
    int32_t  bias[FC_P];
    uint8_t  first, mac, last, layer, group, valid;
} fc_mac_s2_t;

typedef struct
{
    uint8_t  valid, layer, group;
} fc_mac_s3_t;

typedef struct
{
    fc_mac_s1_t s1, s1_next;
    fc_mac_s2_t s2, s2_next;
    fc_mac_s3_t s3, s3_next;
    fc_acc_t    acc[FC_P], acc_next[FC_P];

    /* debug: sums that do not fit FC_ACC_W */
    uint32_t dbg_acc_ovf_cnt;
    uint8_t  w_dbg_ovf;
} fc_mac_t;

void fc_mac_init(fc_mac_t *m);
void fc_mac_reset(fc_mac_t *m);
void fc_mac_comb(fc_mac_t *m, const fc_mac_in_t *in, fc_mac_out_t *out);
void fc_mac_seq(fc_mac_t *m);

#endif
