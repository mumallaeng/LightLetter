#include "fc_quant_signed.h"

/* always @(*)  y = clip(round_half_even(x / 2^scale_exp), -32768, 32767) */
int16_t fc_quant_signed_comb_value(ob_acc_t x_in, uint8_t scale_exp, uint8_t *dbg_sat)
{
    ob_acc_t q = x_in >> scale_exp; /* arithmetic shift: floor */

    if (scale_exp > 0)
    {
        ob_acc_t rem  = x_in & (((ob_acc_t)1 << scale_exp) - 1); /* >= 0 */
        ob_acc_t half = (ob_acc_t)1 << (scale_exp - 1);

        if (rem > half || (rem == half && (q & 1)))
            q = q + 1;
    }

    uint8_t sat = (q > FC_OUT_MAX) || (q < FC_OUT_MIN);
    if (dbg_sat)
        *dbg_sat = sat;

    if (q > FC_OUT_MAX)
        return (int16_t)FC_OUT_MAX;
    if (q < FC_OUT_MIN)
        return (int16_t)FC_OUT_MIN;
    return (int16_t)q;
}

void fc_quant_signed_init(fc_quant_signed_t *m, const fc_param_t *p)
{
    m->p = *p;
    out_reorder_init(&m->u_out_reorder, 1, p->n_out);
    fc_quant_signed_reset(m);
}

void fc_quant_signed_reset(fc_quant_signed_t *m)
{
    out_reorder_reset(&m->u_out_reorder);

    m->w_dbg_sat   = 0;
    m->dbg_sat_cnt = 0;
}

/* always @(*) */
void fc_quant_signed_comb(fc_quant_signed_t *m, const fc_quant_signed_in_t *in,
                          fc_quant_signed_out_t *out)
{
    // ========== Signed Quantizer ==========
    uint8_t sat;
    int16_t q = fc_quant_signed_comb_value(in->sum_data, m->p.scale_exp, &sat);

    // ========== Reorder Buffer (output buffer) ==========
    out_reorder_in_t  rb_in;
    out_reorder_out_t rb_out;
    rb_in.push  = in->sum_valid;
    rb_in.din   = (uint64_t)(uint16_t)q;
    rb_in.rd_en = in->out_ready;
    out_reorder_comb(&m->u_out_reorder, &rb_in, &rb_out);

    // ========== Output Logic ==========
    out->out_data0 = (int16_t)(uint16_t)(rb_out.dout & 0xFFFF);
    out->out_valid = rb_out.avail;

    m->w_dbg_sat = in->sum_valid && sat;
}

/* always @(posedge clk) */
void fc_quant_signed_seq(fc_quant_signed_t *m)
{
    out_reorder_seq(&m->u_out_reorder);

    m->dbg_sat_cnt += m->w_dbg_sat;
}
