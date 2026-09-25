/*
 * fc_mac: LANES multiplies per clock, summed to one partial sum.
 * Three pipeline stages: the products register, the first half of the balanced adder tree,
 * then the second half and the result register, so ch_result follows its mac_en by three clocks.
 */
#ifndef FC_MAC_H
#define FC_MAC_H

#include "fc_common.h"

/* input ports */
typedef struct
{
    uint16_t x[FC_MAX_LANES];
    int16_t  w[FC_MAX_LANES];
    uint8_t  mac_en;
} fc_mac_in_t;

/* output ports */
typedef struct
{
    ob_ch_t ch_result; /* signed [CH_W-1:0] */
    uint8_t mac_valid;
} fc_mac_out_t;

typedef struct
{
    fc_param_t p;

    uint8_t levels; /* adder tree depth, ceil(log2(L)) */
    uint8_t split;  /* level where the pipeline register sits */
    uint8_t midn;   /* nodes at the split */

    /* registers: reg / reg_next */
    int64_t prod[FC_MAX_LANES],  prod_next[FC_MAX_LANES];
    uint8_t prod_valid,          prod_valid_next;
    ob_ch_t mid[FC_MAX_LANES],   mid_next[FC_MAX_LANES];
    uint8_t mid_valid,           mid_valid_next;
    ob_ch_t sum,                 sum_next;
    uint8_t sum_valid,           sum_valid_next;

    /* debug: a tree node wider than CH_W (must stay 0) */
    uint8_t  w_dbg_ch_ovf;
    uint32_t dbg_ch_ovf_cnt;
} fc_mac_t;

void fc_mac_init(fc_mac_t *m, const fc_param_t *p);
void fc_mac_reset(fc_mac_t *m);
void fc_mac_comb(fc_mac_t *m, const fc_mac_in_t *in, fc_mac_out_t *out);
void fc_mac_seq(fc_mac_t *m);

#endif
