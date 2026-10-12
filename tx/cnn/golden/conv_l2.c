#include <string.h>
#include "conv_l2.h"

void conv_l2_init(conv_l2_t *m, const wgt_t *weight, const int32_t *bias, uint8_t scale_exp)
{
    memset(m, 0, sizeof(*m));

    total_ctrl_fsm_l2_reset(&m->fsm, CONV_L2_PASSES);
    weight_addr_ctrl_l2_reset(&m->wac, CONV_L2_C_OUT);
    weight_rom_reset(&m->rom, weight, CONV_L2_C_OUT, CONV_L2_C_IN);
    line_buffer_array_reset(&m->lb);
    mac_array_reset(&m->mac);

    ob_param_t op = {2, CONV_L2_N, CONV_L2_C_OUT, CONV_L2_PASSES};
    output_buffer_init(&m->ob, &op, bias, CONV_L2_C_OUT);

    rq_param_t rp = {CONV_L2_N, CONV_L2_C_OUT, CONV_L2_PACK, scale_exp};
    relu_quant_init(&m->rq, &rp);
}

/*
 * 배선. 모듈 간 신호는 앞 모듈의 출력 wire 를 쓰므로 아래 순서대로 평가한다.
 * (FSM 이 쓰는 win_valid, mac_done, Output Buffer 가 쓰는 MAC 출력은 레지스터 출력)
 */
void conv_l2_comb(conv_l2_t *m, const conv_l2_in_t *in, conv_l2_out_t *out)
{
    total_ctrl_fsm_l2_in_t   fsm_i;
    weight_addr_ctrl_l2_in_t wac_i;
    weight_rom_in_t       rom_i;
    relu_quant_in_t       rq_i;

    for (int lane = 0; lane < CE_LANES; lane++)
        m->pixel_in[lane] = in->pixel_in[lane];

    /* Total Control FSM (win_valid[0..2] 는 broadcast 라 항상 같음) */
    fsm_i.out_valid = in->in_valid;
    fsm_i.ch_done   = in->ch_done;
    fsm_i.win_valid = m->lb.win_valid[0];
    fsm_i.mac_done  = m->wac.mac_done;
    total_ctrl_fsm_l2_comb(&m->fsm, &fsm_i, &m->fsm_o);

    /* Weight Addr Ctrl */
    wac_i.mac_start = m->fsm_o.mac_start;
    wac_i.is_ch35   = m->fsm_o.is_ch35;
    weight_addr_ctrl_l2_comb(&m->wac, &wac_i, &m->wac_o);

    /* Weight ROM -> MAC weight_in */
    rom_i.is_ch35    = m->wac_o.is_ch35;
    rom_i.out_ch_sel = m->wac_o.out_ch_sel;
    weight_rom_comb(&m->rom, &rom_i, &m->rom_o);
    for (int lane = 0; lane < CE_LANES; lane++)
        for (int k = 0; k < CE_KK; k++)
            m->mac_weight[lane][k] = m->rom_o.weight[lane * CE_KK + k];

    /* MAC Array (레지스터 출력) -> Output Buffer */
    m->ob_i.ch_result0 = mac_array_ch_result(&m->mac, 0);
    m->ob_i.ch_result1 = mac_array_ch_result(&m->mac, 1);
    m->ob_i.ch_result2 = mac_array_ch_result(&m->mac, 2);
    m->ob_i.mac_valid  = mac_array_valid(&m->mac);
    output_buffer_comb(&m->ob, &m->ob_i, &m->ob_o);

    /* ReLU & Quantization */
    rq_i.sum_data  = m->ob_o.sum_data;
    rq_i.sum_valid = m->ob_o.sum_valid;
    rq_i.out_ready = in->out_ready;
    relu_quant_comb(&m->rq, &rq_i, &m->rq_o);

    /* top outputs */
    out->in_ready    = m->fsm_o.out_ready;
    out->out_data    = m->rq_o.out_data0;
    out->out_ch_done = m->rq_o.out_ch_done;
    out->out_valid   = m->rq_o.out_valid;
}

void conv_l2_seq(conv_l2_t *m)
{
    /* MAC 은 line buffer 가 이번 엣지에 갱신되기 전의 win_out 을 본다 */
    uint8_t v = m->wac_o.cal_valid;
    uint8_t valid[CE_LANES] = {v, v, v};

    mac_array_step(&m->mac, m->lb.win_out, m->mac_weight, valid, CE_LANES);
    line_buffer_array_step(&m->lb, m->pixel_in[0], m->pixel_in[1], m->pixel_in[2],
                           m->fsm_o.pixel_valid, m->fsm_o.phase_clear);

    total_ctrl_fsm_l2_seq(&m->fsm);
    weight_addr_ctrl_l2_seq(&m->wac);
    weight_rom_seq(&m->rom);
    output_buffer_seq(&m->ob);
    relu_quant_seq(&m->rq);
}

int conv_l2_idle(const conv_l2_t *m)
{
    int busy = m->fsm.state != T2_IDLE || m->wac.state != W2_IDLE || m->mac.mac_valid_reg ||
               m->ob.sum_valid || m->rq.u_out_reorder.wr_cnt != 0;   /* reorder 가 프레임을 다 내보내면 wr_cnt = 0 */

    for (int ch = 0; ch < CE_LANES; ch++)
    {
        busy |= m->mac.c_valid_reg[ch];
        for (int row = 0; row < CE_K; row++)
            busy |= m->mac.mu[ch][row].valid_reg;
    }
    return !busy;
}
