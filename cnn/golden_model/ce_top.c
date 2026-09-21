#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ce_top.h"

int ce_top_init(ce_top_t *t, const ce_param_t *p, const wgt_t *weight, const int32_t *bias)
{
    int groups = (p->c_in + CE_LANES - 1) / CE_LANES;
    int n      = (p->in_h - CE_K + 1) * (p->in_w - CE_K + 1);

    /* 부분합 BRAM 은 group 이 2개 이상일 때만 쓴다 */
    if (groups > CE_MAX_GROUPS || p->c_out > CE_MAX_C_OUT ||
        (groups > 1 && n * p->c_out > OB_MAX_BUF_DEPTH))
    {
        printf("ce_top_init: layer %d does not fit (groups %d, C_OUT %d, N %d)\n",
               p->layer, groups, p->c_out, n);
        return 1;
    }

    memset(t, 0, sizeof(*t));
    t->p = *p;

    total_ctrl_fsm_reset(&t->fsm, (uint8_t)groups);
    weight_addr_ctrl_reset(&t->wac, p->c_out);
    weight_rom_reset(&t->rom, weight, p->c_out, p->c_in);
    mac_array_reset(&t->mac);

    t->lb = (p->layer == 1) ? ce_lb_l1_new(p->in_w) : ce_lb_l2_new(p->in_w);
    if (!t->lb.inst)
        return 1;

    ob_param_t op = {p->layer, (uint16_t)n, p->c_out, (uint8_t)groups};
    output_buffer_init(&t->ob, &op, bias, p->c_out);

    rq_param_t rp = {p->pack, p->scale_exp};
    relu_quant_init(&t->rq, &rp);
    return 0;
}

void ce_top_free(ce_top_t *t)
{
    free(t->lb.inst);
    t->lb.inst = NULL;
}

/*
 * 배선. 모듈 간 신호는 앞 모듈의 출력 wire 를 쓰므로 아래 순서대로 평가한다.
 * (FSM 이 쓰는 win_valid, mac_done, Output Buffer 가 쓰는 MAC 출력은 레지스터 출력)
 */
void ce_top_comb(ce_top_t *t, const ce_in_t *in, ce_out_t *out)
{
    total_ctrl_fsm_in_t   fsm_i;
    weight_addr_ctrl_in_t wac_i;
    weight_rom_in_t       rom_i;
    relu_quant_in_t       rq_i;

    t->lb_win_valid = t->lb.win_valid(t->lb.inst);
    for (int lane = 0; lane < CE_LANES; lane++)
        t->pixel_in[lane] = in->pixel_in[lane];

    /* Total Control FSM */
    fsm_i.out_valid = in->in_valid;
    fsm_i.ch_done   = in->ch_done;
    fsm_i.win_valid = t->lb_win_valid;
    fsm_i.mac_done  = t->wac.mac_done;
    total_ctrl_fsm_comb(&t->fsm, &fsm_i, &t->fsm_o);

    /* Weight Addr Ctrl */
    wac_i.mac_start = t->fsm_o.mac_start;
    wac_i.is_ch35   = t->fsm_o.is_ch35;
    weight_addr_ctrl_comb(&t->wac, &wac_i, &t->wac_o);

    /* Weight ROM -> MAC weight_in[431:0] */
    rom_i.is_ch35    = t->wac_o.is_ch35;
    rom_i.out_ch_sel = t->wac_o.out_ch_sel;
    weight_rom_comb(&t->rom, &rom_i, &t->rom_o);
    for (int lane = 0; lane < CE_LANES; lane++)
        for (int k = 0; k < CE_KK; k++)
            t->mac_weight[lane][k] = t->rom_o.weight[lane * CE_KK + k];

    /* MAC Array (레지스터 출력) -> Output Buffer */
    t->ob_i.ch_result0 = mac_array_ch_result(&t->mac, 0);
    t->ob_i.ch_result1 = mac_array_ch_result(&t->mac, 1);
    t->ob_i.ch_result2 = mac_array_ch_result(&t->mac, 2);
    t->ob_i.mac_valid  = mac_array_valid(&t->mac);
    output_buffer_comb(&t->ob, &t->ob_i, &t->ob_o);

    /* ReLU & Quantization */
    rq_i.sum_data  = t->ob_o.sum_data;
    rq_i.sum_valid = t->ob_o.sum_valid;
    rq_i.ch_done   = t->ob_o.ch_done;
    rq_i.out_ready = in->out_ready;
    relu_quant_comb(&t->rq, &rq_i, &t->rq_o);

    /* top outputs */
    out->in_ready    = t->fsm_o.out_ready;
    out->out_data0   = t->rq_o.out_data0;
    out->out_data1   = t->rq_o.out_data1;
    out->out_data2   = t->rq_o.out_data2;
    out->out_ch_done = t->rq_o.out_ch_done;
    out->out_valid   = t->rq_o.out_valid;
}

void ce_top_seq(ce_top_t *t)
{
    /* MAC 은 line buffer 가 이번 엣지에 갱신되기 전의 win_out 을 본다 */
    int16_t win[CE_LANES][CE_KK];
    uint8_t v = t->wac_o.weight_valid;
    uint8_t valid[CE_LANES] = {v, v, v};
    int     active = t->p.c_in < CE_LANES ? t->p.c_in : CE_LANES;

    t->lb.win_out(t->lb.inst, win);
    mac_array_step(&t->mac, win, t->mac_weight, valid, active);
    t->lb.step(t->lb.inst, t->pixel_in, t->fsm_o.pixel_valid, t->fsm_o.phase_clear);

    total_ctrl_fsm_seq(&t->fsm);
    weight_addr_ctrl_seq(&t->wac);
    weight_rom_seq(&t->rom);
    output_buffer_seq(&t->ob);
    relu_quant_seq(&t->rq);
}

int ce_top_idle(const ce_top_t *t)
{
    int busy = t->fsm.state != T_IDLE || t->wac.state != W_IDLE || t->mac.mac_valid_reg ||
               t->rq.u_output_fifo.count != 0;

    for (int ch = 0; ch < CE_LANES; ch++)
    {
        busy |= t->mac.c_valid_reg[ch];
        for (int row = 0; row < CE_K; row++)
            busy |= t->mac.mu[ch][row].valid_reg;
    }
    return !busy;
}
