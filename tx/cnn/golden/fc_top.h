/* fc_top: MaxPooling -> [shared engine: FC1 -> FC2 -> FC3] -> logit stream (to Argmax). */
#ifndef FC_TOP_H
#define FC_TOP_H

#include "fc_feature_buf.h"
#include "fc_bias_rom.h"
#include "fc_ctrl.h"
#include "fc_quant_out.h"
#include "fc_mac.h"
#include "fc_weight_rom.h"

typedef struct
{
    uint16_t fc_in_data;
    uint8_t  fc_in_valid;
    uint8_t  logit_ready;
} fc_top_in_t;

typedef struct
{
    uint8_t fc_in_ready;
    int16_t logit_data; /* signed */
    uint8_t logit_valid;
} fc_top_out_t;

typedef struct
{
    fc_feature_buf_t    u_fc1_in, u_fc2_in, u_fc3_in;
    fc_weight_rom_t u_wrom;
    fc_bias_rom_t   u_brom;
    fc_mac_t    u_mac;
    fc_quant_out_t  u_quant_out;
    fc_ctrl_t       u_ctrl;

    /* layer boundary transfers of the last comb call (FC1 / FC2 values as they are written) */
    uint8_t w_l1_fire, w_l2_fire;
    int16_t w_l1_data, w_l2_data;
} fc_top_t;

void fc_top_init(fc_top_t *m, const fc_rom_image_t *img);
void fc_top_reset(fc_top_t *m);
void fc_top_comb(fc_top_t *m, const fc_top_in_t *in, fc_top_out_t *out);
void fc_top_seq(fc_top_t *m);

#endif
