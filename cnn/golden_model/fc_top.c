#include "fc_top.h"
#include <string.h>

void fc_top_init(fc_top_t *m, const fc_rom_image_t *img)
{
    fc_act_buf_init(&m->u_act_in, FC_ACT_IN);
    fc_act_buf_init(&m->u_act_a, FC_ACT_A);
    fc_act_buf_init(&m->u_act_b, FC_ACT_B);
    fc_weight_rom_init(&m->u_wrom, img->w);
    fc_bias_rom_init(&m->u_brom, img->b);
    fc_mac_acc_init(&m->u_mac);
    fc_drain_init(&m->u_drain);
    fc_ctrl_init(&m->u_ctrl);
    fc_top_reset(m);
}

void fc_top_reset(fc_top_t *m)
{
    fc_act_buf_reset(&m->u_act_in);
    fc_act_buf_reset(&m->u_act_a);
    fc_act_buf_reset(&m->u_act_b);
    fc_mac_acc_reset(&m->u_mac);
    fc_drain_reset(&m->u_drain);
    fc_ctrl_reset(&m->u_ctrl);
    m->w_l1_fire = m->w_l2_fire = 0;
    m->w_l1_data = m->w_l2_data = 0;
}

/* always @(*) : the wiring, in dependency order */
void fc_top_comb(fc_top_t *m, const fc_top_in_t *in, fc_top_out_t *out)
{
    /* busy / hold_free come from registers only, so the controller can go first */
    fc_ctrl_in_t  ci = {in->fc_in_valid, (uint8_t)(m->u_mac.s1.valid || m->u_mac.s2.valid),
                        (uint8_t)!m->u_drain.valid};
    fc_ctrl_out_t co;
    fc_ctrl_comb(&m->u_ctrl, &ci, &co);

    /* this layer's input value (asynchronous reads; writes are applied below) */
    fc_act_buf_in_t  rd_in = {0, 0, 0, co.x_raddr};
    fc_act_buf_out_t rd_a, rd_b, rd_in_o;
    fc_act_buf_comb(&m->u_act_in, &rd_in, &rd_in_o);
    fc_act_buf_comb(&m->u_act_a, &rd_in, &rd_a);
    fc_act_buf_comb(&m->u_act_b, &rd_in, &rd_b);
    uint16_t x = (co.layer == 1) ? rd_in_o.rdata : (co.layer == 2) ? rd_a.rdata : rd_b.rdata;

    fc_weight_rom_in_t  wi = {co.rom_addr};
    fc_weight_rom_out_t wo;
    fc_weight_rom_comb(&m->u_wrom, &wi, &wo);

    fc_bias_rom_in_t  bi = {co.bias_addr};
    fc_bias_rom_out_t bo;
    fc_bias_rom_comb(&m->u_brom, &bi, &bo);

    fc_mac_acc_in_t  mi;
    fc_mac_acc_out_t mo;
    mi.x = x;
    memcpy(mi.w, wo.w, sizeof mi.w);
    memcpy(mi.b, bo.b, sizeof mi.b);
    mi.first = co.first; mi.mac_en = co.mac_en; mi.last = co.last;
    mi.layer = co.layer; mi.group = co.group;
    fc_mac_acc_comb(&m->u_mac, &mi, &mo);

    fc_drain_in_t  di;
    fc_drain_out_t dro;
    di.sum_valid = mo.sum_valid;
    memcpy(di.sum, mo.sum, sizeof di.sum);
    di.layer = mo.layer; di.group = mo.group; di.logit_ready = in->logit_ready;
    fc_drain_comb(&m->u_drain, &di, &dro);

    /* writes: the MaxPooling stream into act_in, the drain into act_a / act_b */
    fc_act_buf_in_t  w_in = {co.in_we, co.in_waddr, in->fc_in_data, co.x_raddr};
    fc_act_buf_in_t  w_a  = {(uint8_t)(dro.act_we && dro.act_layer == 1), dro.act_waddr, dro.act_wdata, co.x_raddr};
    fc_act_buf_in_t  w_b  = {(uint8_t)(dro.act_we && dro.act_layer == 2), dro.act_waddr, dro.act_wdata, co.x_raddr};
    fc_act_buf_comb(&m->u_act_in, &w_in, &rd_in_o);
    fc_act_buf_comb(&m->u_act_a, &w_a, &rd_a);
    fc_act_buf_comb(&m->u_act_b, &w_b, &rd_b);

    // ========== Output Logic ==========
    out->fc_in_ready = co.fc_in_ready;
    out->logit_data  = dro.logit_data;
    out->logit_valid = dro.logit_valid;

    m->w_l1_fire = w_a.we; m->w_l1_data = (int16_t)dro.act_wdata;
    m->w_l2_fire = w_b.we; m->w_l2_data = (int16_t)dro.act_wdata;
}

/* always @(posedge clk) */
void fc_top_seq(fc_top_t *m)
{
    fc_ctrl_seq(&m->u_ctrl);
    fc_act_buf_seq(&m->u_act_in);
    fc_act_buf_seq(&m->u_act_a);
    fc_act_buf_seq(&m->u_act_b);
    fc_weight_rom_seq(&m->u_wrom);
    fc_mac_acc_seq(&m->u_mac);
    fc_drain_seq(&m->u_drain);
}
