#include "relu_quant.h"

/* always @(*)  y = max(x, 0) */
ob_acc_t relu_comb(ob_acc_t x_in)
{
    return (x_in < 0) ? 0 : x_in;
}

/* always @(*)  y = clip(round_half_even(x / 2^scale_exp), 0, 32767), x >= 0 */
uint16_t quantizer_comb(ob_acc_t x_in, uint8_t scale_exp, uint8_t *dbg_sat)
{
    ob_acc_t q = x_in >> scale_exp;

    if (scale_exp > 0)
    {
        ob_acc_t rem  = x_in & (((ob_acc_t)1 << scale_exp) - 1);
        ob_acc_t half = (ob_acc_t)1 << (scale_exp - 1);

        if (rem > half || (rem == half && (q & 1)))
            q = q + 1;
    }

    uint8_t sat = (q > RQ_OUT_MAX);
    if (dbg_sat)
        *dbg_sat = sat;

    return sat ? RQ_OUT_MAX : (uint16_t)q;
}

void relu_quant_init(relu_quant_t *m, const rq_param_t *p)
{
    m->p = *p;
    lane_packer_init(&m->u_lane_packer, p->pack);
    out_reorder_init(&m->u_out_reorder, p->n, (uint8_t)(p->c_out / p->pack));
    relu_quant_reset(m);
}

void relu_quant_reset(relu_quant_t *m)
{
    lane_packer_reset(&m->u_lane_packer);
    out_reorder_reset(&m->u_out_reorder);

    m->w_dbg_sat   = 0;
    m->dbg_sat_cnt = 0;
}

/* always @(*) */
void relu_quant_comb(relu_quant_t *m, const relu_quant_in_t *in, relu_quant_out_t *out)
{
    // ========== ReLU -> Quantizer ==========
    uint8_t  sat;
    ob_acc_t relu_y = relu_comb(in->sum_data);
    uint16_t quant_y = quantizer_comb(relu_y, m->p.scale_exp, &sat);

    // ========== Lane Packer ==========
    lane_packer_in_t  lp_in;
    lane_packer_out_t lp_out;
    lp_in.q_in    = quant_y;
    lp_in.q_valid = in->sum_valid;
    lane_packer_comb(&m->u_lane_packer, &lp_in, &lp_out);

    // ========== Reorder Buffer ==========
    out_reorder_in_t  rb_in;
    out_reorder_out_t rb_out;
    rb_in.push  = lp_out.pack_valid;
    rb_in.din   = lp_out.pack_data;
    rb_in.rd_en = in->out_ready;
    out_reorder_comb(&m->u_out_reorder, &rb_in, &rb_out);

    // ========== Output Logic ==========
    out->out_data0   = (uint16_t)(rb_out.dout & 0xFFFF);
    out->out_data1   = (m->p.pack > 1) ? (uint16_t)((rb_out.dout >> 16) & 0xFFFF) : 0;
    out->out_data2   = (m->p.pack > 2) ? (uint16_t)((rb_out.dout >> 32) & 0xFFFF) : 0;
    out->out_ch_done = rb_out.avail && rb_out.last_pixel;
    out->out_valid   = rb_out.avail;

    m->w_dbg_sat = in->sum_valid && sat;
}

/* always @(posedge clk) */
void relu_quant_seq(relu_quant_t *m)
{
    lane_packer_seq(&m->u_lane_packer);
    out_reorder_seq(&m->u_out_reorder);

    m->dbg_sat_cnt += m->w_dbg_sat;
}
