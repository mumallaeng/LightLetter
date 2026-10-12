#include "fc_top.h"
#include <string.h>

void fc_top_init(fc_top_t *m, const fc_rom_image_t *img)
{
    fc_feature_buf_init(&m->u_fc1_in, FC_FC1_IN);
    fc_feature_buf_init(&m->u_fc2_in, FC_FC2_IN);
    fc_feature_buf_init(&m->u_fc3_in, FC_FC3_IN);
    fc_weight_rom_init(&m->u_wrom, img->w);
    fc_bias_rom_init(&m->u_brom, img->b);
    fc_mac_init(&m->u_mac);
    fc_quant_out_init(&m->u_quant_out);
    fc_ctrl_init(&m->u_ctrl);
    fc_top_reset(m);
}

void fc_top_reset(fc_top_t *m)
{
    fc_feature_buf_reset(&m->u_fc1_in);
    fc_feature_buf_reset(&m->u_fc2_in);
    fc_feature_buf_reset(&m->u_fc3_in);
    fc_mac_reset(&m->u_mac);
    fc_quant_out_reset(&m->u_quant_out);
    fc_ctrl_reset(&m->u_ctrl);
    m->w_l1_fire = m->w_l2_fire = 0;
    m->w_l1_data = m->w_l2_data = 0;
}

/* always @(*) : the wiring, in dependency order */
void fc_top_comb(fc_top_t *m, const fc_top_in_t *in, fc_top_out_t *out)
{
    /* busy / hold_free come from registers only, so the controller can go first */
    fc_ctrl_in_t  ci = {in->fc_in_valid, (uint8_t)(m->u_mac.s1.valid || m->u_mac.s2.valid || m->u_mac.s3.valid),
                        (uint8_t)fc_quant_out_empty(&m->u_quant_out)};
    fc_ctrl_out_t co;
    fc_ctrl_comb(&m->u_ctrl, &ci, &co);

    /* this layer's input value (asynchronous reads; writes are applied below) */
    fc_feature_buf_in_t  rd_in = {0, 0, 0, co.feature_raddr};
    fc_feature_buf_out_t rd_fc2, rd_fc3, rd_fc1;
    fc_feature_buf_comb(&m->u_fc1_in, &rd_in, &rd_fc1);
    fc_feature_buf_comb(&m->u_fc2_in, &rd_in, &rd_fc2);
    fc_feature_buf_comb(&m->u_fc3_in, &rd_in, &rd_fc3);
    uint16_t feature = (co.layer == 1) ? rd_fc1.rdata : (co.layer == 2) ? rd_fc2.rdata : rd_fc3.rdata;

    fc_weight_rom_in_t  wi = {co.weight_addr};
    fc_weight_rom_out_t wo;
    fc_weight_rom_comb(&m->u_wrom, &wi, &wo);

    fc_bias_rom_in_t  bi = {co.bias_addr};
    fc_bias_rom_out_t bo;
    fc_bias_rom_comb(&m->u_brom, &bi, &bo);

    fc_mac_in_t  mi;
    fc_mac_out_t mo;
    mi.feature = feature;
    memcpy(mi.weight, wo.w, sizeof mi.weight);
    memcpy(mi.bias, bo.b, sizeof mi.bias);
    mi.first = co.first; mi.mac_en = co.mac_en; mi.last = co.last;
    mi.layer = co.layer; mi.group = co.group;
    fc_mac_comb(&m->u_mac, &mi, &mo);

    fc_quant_out_in_t  di;
    fc_quant_out_out_t dro;
    di.sum_valid = mo.sum_valid;
    memcpy(di.sum, mo.sum, sizeof di.sum);
    di.layer = mo.layer; di.group = mo.group; di.logit_ready = in->logit_ready;
    fc_quant_out_comb(&m->u_quant_out, &di, &dro);

    /* writes: the MaxPooling stream into fc1_in, fc_quant_out into fc2_in / fc3_in */
    fc_feature_buf_in_t  w_fc1 = {co.fc1_in_we, co.fc1_in_waddr, in->fc_in_data, co.feature_raddr};
    fc_feature_buf_in_t  w_fc2 = {(uint8_t)(dro.feature_we && dro.feature_layer == 1), dro.feature_waddr, dro.feature_wdata, co.feature_raddr};
    fc_feature_buf_in_t  w_fc3 = {(uint8_t)(dro.feature_we && dro.feature_layer == 2), dro.feature_waddr, dro.feature_wdata, co.feature_raddr};
    fc_feature_buf_comb(&m->u_fc1_in, &w_fc1, &rd_fc1);
    fc_feature_buf_comb(&m->u_fc2_in, &w_fc2, &rd_fc2);
    fc_feature_buf_comb(&m->u_fc3_in, &w_fc3, &rd_fc3);

    // ========== Output Logic ==========
    out->fc_in_ready = co.fc_in_ready;
    out->logit_data  = dro.logit_data;
    out->logit_valid = dro.logit_valid;

    m->w_l1_fire = w_fc2.we; m->w_l1_data = (int16_t)dro.feature_wdata;
    m->w_l2_fire = w_fc3.we; m->w_l2_data = (int16_t)dro.feature_wdata;
}

/* always @(posedge clk) */
void fc_top_seq(fc_top_t *m)
{
    fc_ctrl_seq(&m->u_ctrl);
    fc_feature_buf_seq(&m->u_fc1_in);
    fc_feature_buf_seq(&m->u_fc2_in);
    fc_feature_buf_seq(&m->u_fc3_in);
    fc_weight_rom_seq(&m->u_wrom);
    fc_mac_seq(&m->u_mac);
    fc_quant_out_seq(&m->u_quant_out);
}
