/*
 * fc_layer: one Fully Connected layer.
 *   staging -> fc_mac -> Output Buffer (reused) -> ReLU&Quant (reused) / signed quantizer
 * Order: chunk (outer) -> neuron (inner); neurons finish during the last chunk.
 */
#ifndef FC_LAYER_H
#define FC_LAYER_H

#include "fc_common.h"
#include "fc_staging.h"
#include "fc_ctrl.h"
#include "fc_weight_rom.h"
#include "fc_mac.h"
#include "fc_quant_signed.h"
#include "output_buffer.h"
#include "relu_quant.h"

/* input ports */
typedef struct
{
    uint16_t in_data;
    uint8_t  in_valid;
    uint8_t  out_ready;
} fc_layer_in_t;

/* output ports */
typedef struct
{
    uint8_t  in_ready;
    int16_t  out_data; /* unsigned code when RELU = 1 */
    uint8_t  out_valid;
} fc_layer_out_t;

typedef struct
{
    fc_param_t p;

    /* submodule instances */
    fc_staging_t      u_staging;
    fc_ctrl_t         u_ctrl;
    fc_weight_rom_t   u_weight_rom;
    fc_mac_t          u_mac;
    output_buffer_t   u_output_buffer; /* reused as-is: N = 1, C_OUT = n_out, NUM_GROUPS = chunks */
    relu_quant_t      u_relu_quant;    /* RELU = 1 */
    fc_quant_signed_t u_quant_signed;  /* RELU = 0 (FC3) */
} fc_layer_t;

void fc_layer_init(fc_layer_t *m, const fc_param_t *p, const int16_t *rom_rows,
                   const int32_t *bias);
void fc_layer_reset(fc_layer_t *m);
void fc_layer_comb(fc_layer_t *m, const fc_layer_in_t *in, fc_layer_out_t *out);
void fc_layer_seq(fc_layer_t *m);

#endif
