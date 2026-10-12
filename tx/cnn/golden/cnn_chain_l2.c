/* conv_l2 wrapper (IMG_WIDTH 13 번역 단위) */
#include "conv_l2.h"
#include "cnn_chain.h"

static conv_l2_t g_l2;

void chain_l2_init(const int16_t *weight, const int32_t *bias, uint8_t scale_exp)
{
    conv_l2_init(&g_l2, weight, bias, scale_exp);
}

void chain_l2_comb(const chain_l2_in_t *in, chain_l2_out_t *out)
{
    conv_l2_in_t  i;
    conv_l2_out_t o;

    i.in_valid  = in->in_valid;
    i.ch_done   = in->ch_done;
    i.out_ready = in->out_ready;
    for (int j = 0; j < 3; j++)
        i.pixel_in[j] = in->pixel_in[j];
    conv_l2_comb(&g_l2, &i, &o);

    out->in_ready    = o.in_ready;
    out->out_data    = o.out_data;
    out->out_ch_done = o.out_ch_done;
    out->out_valid   = o.out_valid;
}

void chain_l2_seq(void)
{
    conv_l2_seq(&g_l2);
}

int chain_l2_idle(void)
{
    return conv_l2_idle(&g_l2);
}

void chain_l2_probe(chain_probe_t *p)
{
    p->fsm        = total_ctrl_fsm_l2_state_name(g_l2.fsm.state);
    p->win_valid  = g_l2.lb.win_valid[0];
    p->mac_valid  = g_l2.ob_i.mac_valid;
    p->sum_valid  = g_l2.ob_o.sum_valid;
    p->rb_fill = g_l2.rq.u_out_reorder.wr_cnt;
    p->rb_peak  = g_l2.rq.u_out_reorder.dbg_max_fill;
    p->rb_overrun   = g_l2.rq.u_out_reorder.dbg_overrun_cnt;
}
