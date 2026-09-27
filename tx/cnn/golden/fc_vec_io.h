/* Reads tx/cnn/golden/vectors/fc{1,2,3}.txt (export_fc_vectors.py) into dense layers and
 * builds the shared ROM images from them. Shared by test_fc.c and gen_fc_rtl_vectors.c. */
#ifndef FC_VEC_IO_H
#define FC_VEC_IO_H

#include "fc_common.h"

typedef struct
{
    uint16_t n_in;
    uint8_t  n_out;
    uint8_t  scale_exp;
    uint8_t  relu;
    int32_t  bias[FC_MAX_N_OUT];
    uint16_t x[FC_MAX_N_IN];              /* the layer's input in the Python run */
    int16_t  w[FC_MAX_N_OUT][FC_MAX_N_IN];
    int16_t  expected[FC_MAX_N_OUT];      /* the layer's quantized output in the Python run */
} fc_vec_t;

int  fc_read_vec(const char *dir, int layer, fc_vec_t *out);
int  fc_rom_build(fc_rom_image_t *img, const fc_vec_t v[FC_LAYERS]); /* 0 on success */

/* plain integer reference: sum[n] = bias[n] + sum_i x[i] * w[n][i] */
void fc_ref_layer(const fc_vec_t *v, const uint16_t *x, fc_acc_t *sum);

#endif
