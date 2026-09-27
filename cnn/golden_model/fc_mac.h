/* fc_mac: P multipliers and P accumulators. Every `mac_en` adds x * w[lane]; the group's
 * first input (`first`) starts from the bias instead of the running sum (DSP opmode C + M,
 * then P + M). Three pipeline stages after the issue cycle: operands (DSP A/B registers) ->
 * products (M register) -> accumulate (P register). The flags ride along, so the sum of the
 * group's last input is announced on sum_valid in the cycle the accumulate stage sees it. */
#ifndef FC_MAC_ACC_H
#define FC_MAC_ACC_H

#include "fc_common.h"

typedef struct
{
    uint16_t x;          /* one activation, broadcast to every lane */
    int16_t  w[FC_P];    /* this input's weights for the group's P neurons */
    int32_t  b[FC_P];    /* the group's biases (used with first) */
    uint8_t  first;      /* acc <= bias + x * w */
    uint8_t  mac_en;     /* acc <= acc + x * w */
    uint8_t  last;       /* mac_en of the group's last input */
    uint8_t  layer;      /* 1..3, tag carried to the drain */
    uint8_t  group;
} fc_mac_in_t;

typedef struct
{
    uint8_t  sum_valid;  /* the group's sums are on sum[] this cycle */
    fc_acc_t sum[FC_P];
    uint8_t  layer, group;
    uint8_t  busy;       /* something is still in the pipeline */
} fc_mac_out_t;

typedef struct
{
    uint16_t x;
    int16_t  w[FC_P];
    int32_t  b[FC_P];
    uint8_t  first, mac, last, layer, group, valid;
} fc_mac_s1_t;

typedef struct
{
    int32_t  prod[FC_P];
    int32_t  b[FC_P];
    uint8_t  first, mac, last, layer, group, valid;
} fc_mac_s2_t;

typedef struct
{
    /* registers: reg / reg_next */
    fc_mac_s1_t s1, s1_next;
    fc_mac_s2_t s2, s2_next;
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
