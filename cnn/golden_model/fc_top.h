/*
 * fc_top: MaxPooling -> FC1 -> FC2 -> FC3 -> logit stream (to Argmax)
 * Layers are chained as value-at-a-time valid/ready streams.
 */
#ifndef FC_TOP_H
#define FC_TOP_H

#include "fc_layer.h"

/* input ports */
typedef struct
{
    uint16_t fc_in_data;
    uint8_t  fc_in_valid;
    uint8_t  logit_ready;
} fc_top_in_t;

/* output ports */
typedef struct
{
    uint8_t fc_in_ready;
    int16_t logit_data; /* signed */
    uint8_t logit_valid;
} fc_top_out_t;

typedef struct
{
    fc_layer_t l[3];

    /* boundary transfers of the last comb call, for the harness to check */
    uint8_t  w_l1_fire, w_l2_fire;
    int16_t  w_l1_data, w_l2_data;
} fc_top_t;

void fc_top_init(fc_top_t *m, const fc_param_t p[3], const int16_t *const rom[3],
                 const int32_t *const bias[3]);
void fc_top_reset(fc_top_t *m);
void fc_top_comb(fc_top_t *m, const fc_top_in_t *in, fc_top_out_t *out);
void fc_top_seq(fc_top_t *m);

#endif
