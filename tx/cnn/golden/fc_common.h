/* Fully Connected golden model: one shared engine (P multipliers) runs FC1 -> FC2 -> FC3 in groups of P neurons. */
#ifndef FC_COMMON_H
#define FC_COMMON_H

#include <stdint.h>

#define FC_P         20   /* multipliers = DSPs = neurons per group */
#define FC_ACC_W     40   /* accumulator width (FC1 worst case: 400 x 2^30 + bias < 2^39) */
#define FC_LAYERS    3
#define FC_MAX_N_IN  400
#define FC_MAX_N_OUT 120
#define FC_ROM_ROWS  3168 /* 6*400 + 5*120 + 2*84 rows of P weights */
#define FC_BIAS_ROWS 13   /* 6 + 5 + 2 rows of P biases */
#define FC_FC1_IN 400 /* fc_feature_buf depths: MaxPooling input, FC1 output, FC2 output */
#define FC_FC2_IN 120
#define FC_FC3_IN 84

typedef int64_t fc_acc_t;

typedef struct
{
    uint16_t n_in;
    uint8_t  n_out;
    uint8_t  groups;
    uint16_t rom_base;  /* first weight ROM row of this layer: row = rom_base + g * n_in + i */
    uint8_t  bias_base;
    uint8_t  scale_exp;
    uint8_t  relu;      /* 1 = ReLU then unsigned clamp, 0 = signed clamp (FC3) */
} fc_layer_cfg_t;

static const fc_layer_cfg_t FC_CFG[FC_LAYERS] = {
    {400, 120, 6,    0,  0, 16, 1},
    {120,  84, 5, 2400,  6, 14, 1},
    { 84,  26, 2, 3000, 11, 14, 0},
};

/* neurons in group g: the last group of a layer can be short (FC2 4, FC3 6) */
static inline uint8_t fc_group_len(const fc_layer_cfg_t *c, uint8_t g)
{
    uint16_t left = (uint16_t)(c->n_out - (uint16_t)g * FC_P);
    return (left < FC_P) ? (uint8_t)left : FC_P;
}

/* ROM images: what fc_weight.mem / fc_bias.mem hold, lane 0 in the low bits of a row */
typedef struct
{
    int16_t w[FC_ROM_ROWS][FC_P];
    int32_t b[FC_BIAS_ROWS][FC_P];
} fc_rom_image_t;

#endif
