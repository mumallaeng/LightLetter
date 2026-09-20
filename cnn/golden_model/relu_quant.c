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
    relu_quant_reset(m);
}

void relu_quant_reset(relu_quant_t *m)
{
    lane_packer_reset(&m->u_lane_packer);
    output_fifo_reset(&m->u_output_fifo);

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
    lp_in.q_done  = in->ch_done;
    lane_packer_comb(&m->u_lane_packer, &lp_in, &lp_out);

    // ========== Output FIFO ==========
    output_fifo_in_t  ff_in;
    output_fifo_out_t ff_out;
    ff_in.push  = lp_out.pack_valid;
    ff_in.din   = lp_out.pack_data;
    ff_in.rd_en = in->out_ready;
    output_fifo_comb(&m->u_output_fifo, &ff_in, &ff_out);

    // ========== Output Logic ==========
    out->out_data0   = (uint16_t)(ff_out.dout & 0xFFFF);
    out->out_data1   = (m->p.pack > 1) ? (uint16_t)((ff_out.dout >> 16) & 0xFFFF) : 0;
    out->out_data2   = (m->p.pack > 2) ? (uint16_t)((ff_out.dout >> 32) & 0xFFFF) : 0;
    out->out_ch_done = (uint8_t)((ff_out.dout >> (16 * m->p.pack)) & 1);
    out->out_valid   = !ff_out.empty;

    m->w_dbg_sat = in->sum_valid && sat;
}

/* always @(posedge clk) */
void relu_quant_seq(relu_quant_t *m)
{
    lane_packer_seq(&m->u_lane_packer);
    output_fifo_seq(&m->u_output_fifo);

    m->dbg_sat_cnt += m->w_dbg_sat;
}
