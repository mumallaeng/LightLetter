/* Fully Connected golden model: common definitions (stage 1, reuses the Output Buffer path) */
#ifndef FC_COMMON_H
#define FC_COMMON_H

#include "ob_common.h"

#define FC_MAX_LANES  25   /* FC1 */
#define FC_MAX_N_IN   400  /* FC1 */
#define FC_MAX_N_OUT  120  /* FC1 */
#define FC_MAX_CHUNK  17   /* FC3 */
#define FC_MAX_ROM    (FC_MAX_CHUNK * FC_MAX_N_OUT)

#define FC_OUT_MAX 32767
#define FC_OUT_MIN (-32768)

/* parameter */
typedef struct
{
    uint8_t  layer;     /* 1..3 */
    uint16_t n_in;      /* inputs per frame */
    uint8_t  n_out;     /* neurons -> Output Buffer C_OUT */
    uint8_t  lanes;     /* multipliers = chunk size */
    uint8_t  num_chunk; /* ceil(n_in / lanes) -> Output Buffer NUM_GROUPS */
    uint8_t  acc_w;     /* Output Buffer ACC_W */
    uint8_t  scale_exp; /* quantizer right shift */
    uint8_t  relu;      /* 1 = reuse ReLU&Quant, 0 = signed quantizer (FC3) */
} fc_param_t;

static const fc_param_t FC_PARAM_FC1 = {1, 400, 120, 25, 16, 40, 15, 1};
static const fc_param_t FC_PARAM_FC2 = {2, 120, 84, 10, 12, 38, 14, 1};
static const fc_param_t FC_PARAM_FC3 = {3, 84, 36, 5, 17, 38, 13, 0};

/* inputs of this chunk: the last chunk of a frame is shorter, the rest of the lanes read 0 */
static inline uint8_t fc_chunk_len(const fc_param_t *p, uint8_t g)
{
    uint16_t done = (uint16_t)g * p->lanes;
    uint16_t left = (uint16_t)(p->n_in - done);
    return (left < p->lanes) ? (uint8_t)left : p->lanes;
}

#endif
