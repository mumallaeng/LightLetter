/*
 * fc_mac: LANES multiplies per clock, summed to one CH_W partial sum.
 * Two pipeline stages: products register -> adder tree and result register.
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

    /* registers: reg / reg_next */
    int64_t prod[FC_MAX_LANES], prod_next[FC_MAX_LANES];
    uint8_t prod_valid,         prod_valid_next;
    ob_ch_t sum,                sum_next;
    uint8_t sum_valid,          sum_valid_next;

    /* debug: partial sum wider than CH_W (must stay 0) */
    uint8_t  w_dbg_ch_ovf;
    uint32_t dbg_ch_ovf_cnt;
} fc_mac_t;

void fc_mac_init(fc_mac_t *m, const fc_param_t *p);
void fc_mac_reset(fc_mac_t *m);
void fc_mac_comb(fc_mac_t *m, const fc_mac_in_t *in, fc_mac_out_t *out);
void fc_mac_seq(fc_mac_t *m);

#endif
