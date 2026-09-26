/*
 * FC3 output stage: signed quantizer (no ReLU) + the same reorder buffer as ReLU&Quant.
 * y = clip(round_half_even(x / 2^scale_exp), -32768, 32767)
 */
#ifndef FC_QUANT_SIGNED_H
#define FC_QUANT_SIGNED_H

#include "fc_common.h"
#include "out_reorder.h"

/* input ports */
typedef struct
{
    ob_acc_t sum_data;
    uint8_t  sum_valid;
    uint8_t  out_ready;
} fc_quant_signed_in_t;

/* output ports */
typedef struct
{
    int16_t out_data0; /* name matches relu_quant's out_data0 */
    uint8_t out_valid;
} fc_quant_signed_out_t;

typedef struct
{
    fc_param_t p;

    out_reorder_t u_out_reorder;

    /* debug: values clipped at the INT16 range */
    uint8_t  w_dbg_sat;
    uint32_t dbg_sat_cnt;
} fc_quant_signed_t;

int16_t fc_quant_signed_comb_value(ob_acc_t x_in, uint8_t scale_exp, uint8_t *dbg_sat);

void fc_quant_signed_init(fc_quant_signed_t *m, const fc_param_t *p);
void fc_quant_signed_reset(fc_quant_signed_t *m);
void fc_quant_signed_comb(fc_quant_signed_t *m, const fc_quant_signed_in_t *in,
                          fc_quant_signed_out_t *out);
void fc_quant_signed_seq(fc_quant_signed_t *m);

#endif
