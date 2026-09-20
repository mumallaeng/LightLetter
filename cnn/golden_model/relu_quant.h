/*
 * ReLU & Quantization (top) : Output Buffer -> ReLU -> Quantizer -> Lane Packer -> Output FIFO -> MaxPooling
 */
#ifndef RELU_QUANT_H
#define RELU_QUANT_H

#include "ob_common.h"
#include "lane_packer.h"
#include "output_fifo.h"

#define RQ_OUT_MAX 32767

/* parameter */
typedef struct
{
    uint8_t pack;      /* values per output entry: conv1 = 3, conv2 = 1 */
    uint8_t scale_exp; /* quantizer right shift (0..31) */
} rq_param_t;

/* input ports */
typedef struct
{
    ob_acc_t sum_data;
    uint8_t  sum_valid;
    uint8_t  ch_done;
    uint8_t  out_ready; /* <- MaxPooling */
} relu_quant_in_t;

/* output ports */
typedef struct
{
    uint16_t out_data0;
    uint16_t out_data1; /* 0 if PACK < 2 */
    uint16_t out_data2; /* 0 if PACK < 3 */
    uint8_t  out_ch_done;
    uint8_t  out_valid;
} relu_quant_out_t;

typedef struct
{
    rq_param_t p;

    /* submodule instances */
    lane_packer_t u_lane_packer;
    output_fifo_t u_output_fifo;

    /* debug: values clipped at RQ_OUT_MAX */
    uint8_t  w_dbg_sat;
    uint32_t dbg_sat_cnt;
} relu_quant_t;

/* combinational submodules */
ob_acc_t relu_comb(ob_acc_t x_in);
uint16_t quantizer_comb(ob_acc_t x_in, uint8_t scale_exp, uint8_t *dbg_sat);

void relu_quant_init(relu_quant_t *m, const rq_param_t *p);
void relu_quant_reset(relu_quant_t *m);
void relu_quant_comb(relu_quant_t *m, const relu_quant_in_t *in, relu_quant_out_t *out);
void relu_quant_seq(relu_quant_t *m);

#endif
