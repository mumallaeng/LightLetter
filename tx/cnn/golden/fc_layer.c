#include <string.h>
#include "fc_layer.h"

void fc_layer_init(fc_layer_t *m, const fc_param_t *p, const int16_t *rom_rows,
                   const int32_t *bias)
{
    m->p = *p;

    fc_staging_init(&m->u_staging, p);
    fc_ctrl_init(&m->u_ctrl, p);
    fc_weight_rom_init(&m->u_weight_rom, p, rom_rows);
    fc_mac_init(&m->u_mac, p);

    ob_param_t ob_p = {p->layer, 1, p->n_out, p->num_chunk, p->acc_w}; /* one "pixel" per frame */
    output_buffer_init(&m->u_output_buffer, &ob_p, bias, p->n_out);

    if (p->relu)
    {
        rq_param_t rq_p = {1, p->n_out, 1, p->scale_exp};
        relu_quant_init(&m->u_relu_quant, &rq_p);
    }
    else
    {
        fc_quant_signed_init(&m->u_quant_signed, p);
    }
}

void fc_layer_reset(fc_layer_t *m)
{
    fc_staging_reset(&m->u_staging);
    fc_ctrl_reset(&m->u_ctrl);
    fc_weight_rom_reset(&m->u_weight_rom);
    fc_mac_reset(&m->u_mac);
    output_buffer_reset(&m->u_output_buffer);

    if (m->p.relu)
        relu_quant_reset(&m->u_relu_quant);
    else
        fc_quant_signed_reset(&m->u_quant_signed);
}

/* always @(*) */
void fc_layer_comb(fc_layer_t *m, const fc_layer_in_t *in, fc_layer_out_t *out)
{
    // ========== fc_staging / fc_ctrl ==========
    fc_staging_out_t stg_out;
    fc_staging_in_t  stg_in;
    stg_in.in_data  = in->in_data;
    stg_in.in_valid = in->in_valid;
    stg_in.chunk_done  = 0;
    fc_staging_comb(&m->u_staging, &stg_in, &stg_out);

    fc_ctrl_in_t  ctrl_in;
    fc_ctrl_out_t ctrl_out;
    ctrl_in.calc_full = stg_out.calc_full;
    ctrl_in.next_full = stg_out.next_full;
    fc_ctrl_comb(&m->u_ctrl, &ctrl_in, &ctrl_out);

    /* re-evaluate staging with chunk_done from fc_ctrl */
    stg_in.chunk_done = ctrl_out.chunk_done;
    fc_staging_comb(&m->u_staging, &stg_in, &stg_out);

    // ========== fc_weight_rom (address of the next mac_en) ==========
    fc_weight_rom_in_t  rom_in;
    fc_weight_rom_out_t rom_out;
    rom_in.addr = ctrl_out.rom_addr;
    fc_weight_rom_comb(&m->u_weight_rom, &rom_in, &rom_out);

    // ========== fc_mac ==========
    fc_mac_in_t  mac_in;
    fc_mac_out_t mac_out;
    memcpy(mac_in.x, stg_out.x, sizeof(mac_in.x));
    memcpy(mac_in.w, rom_out.w, sizeof(mac_in.w));
    mac_in.mac_en = ctrl_out.mac_en;
    fc_mac_comb(&m->u_mac, &mac_in, &mac_out);

    // ========== output_buffer (reused) ==========
    output_buffer_in_t  ob_in;
    output_buffer_out_t ob_out;
    ob_in.ch_result0 = mac_out.ch_result;
    ob_in.ch_result1 = 0;
    ob_in.ch_result2 = 0;
    ob_in.mac_valid  = mac_out.mac_valid;
    output_buffer_comb(&m->u_output_buffer, &ob_in, &ob_out);

    // ========== output stage: relu_quant or fc_quant_signed ==========
    if (m->p.relu)
    {
        relu_quant_in_t  rq_in;
        relu_quant_out_t rq_out;
        rq_in.sum_data  = ob_out.sum_data;
        rq_in.sum_valid = ob_out.sum_valid;
        rq_in.out_ready = in->out_ready;
        relu_quant_comb(&m->u_relu_quant, &rq_in, &rq_out);

        out->out_data  = (int16_t)rq_out.out_data0;
        out->out_valid = rq_out.out_valid;
    }
    else
    {
        fc_quant_signed_in_t  qs_in;
        fc_quant_signed_out_t qs_out;
        qs_in.sum_data  = ob_out.sum_data;
        qs_in.sum_valid = ob_out.sum_valid;
        qs_in.out_ready = in->out_ready;
        fc_quant_signed_comb(&m->u_quant_signed, &qs_in, &qs_out);

        out->out_data  = qs_out.out_data0;
        out->out_valid = qs_out.out_valid;
    }

    out->in_ready = stg_out.in_ready;
}

/* always @(posedge clk) */
void fc_layer_seq(fc_layer_t *m)
{
    fc_staging_seq(&m->u_staging);
    fc_ctrl_seq(&m->u_ctrl);
    fc_weight_rom_seq(&m->u_weight_rom);
    fc_mac_seq(&m->u_mac);
    output_buffer_seq(&m->u_output_buffer);

    if (m->p.relu)
        relu_quant_seq(&m->u_relu_quant);
    else
        fc_quant_signed_seq(&m->u_quant_signed);
}
