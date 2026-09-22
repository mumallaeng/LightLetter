/* conv_l1 wrapper (IMG_WIDTH 28 번역 단위) */
#include "conv_l1.h"
#include "cnn_chain.h"

static conv_l1_t g_l1;

void chain_l1_init(const int16_t *weight, const int32_t *bias, uint8_t scale_exp)
{
    conv_l1_init(&g_l1, weight, bias, scale_exp);
}

void chain_l1_comb(const chain_l1_in_t *in, chain_l1_out_t *out)
{
    conv_l1_in_t  i;
    conv_l1_out_t o;

    i.in_valid  = in->in_valid;
    i.pixel_in  = in->pixel_in;
    i.ch_done   = in->ch_done;
    i.out_ready = in->out_ready;
    conv_l1_comb(&g_l1, &i, &o);

    out->in_ready    = o.in_ready;
    out->out_valid   = o.out_valid;
    out->out_ch_done = o.out_ch_done;
    for (int j = 0; j < 3; j++)
        out->out_data[j] = o.out_data[j];
}

void chain_l1_seq(void)
{
    conv_l1_seq(&g_l1);
}

int chain_l1_idle(void)
{
    return conv_l1_idle(&g_l1);
}

void chain_l1_probe(chain_probe_t *p)
{
    p->fsm        = total_ctrl_fsm_l1_state_name(g_l1.fsm.state);
    p->win_valid  = g_l1.lb.win_valid;
    p->mac_valid  = g_l1.ob_i.mac_valid;
    p->sum_valid  = g_l1.ob_o.sum_valid;
    p->rb_fill = g_l1.rq.u_out_reorder.wr_cnt;
    p->rb_peak  = g_l1.rq.u_out_reorder.dbg_max_fill;
    p->rb_overrun   = g_l1.rq.u_out_reorder.dbg_overrun_cnt;
}
