#include "l2_top.h"

void l2_top_reset(l2_top_t *t, const wgt_t weight[L2_OUT_CH][L2_IN_CH][L2_K][L2_K])
{
    total_ctrl_fsm_reset(&t->fsm);
    weight_addr_ctrl_reset(&t->wac);
    line_buffer_reset(&t->lb);
    weight_rom_reset(&t->rom, weight);
    mac_array_reset(&t->mac);
}

/*
 * 배선. 모듈 간 신호는 앞 모듈의 출력 wire 를 쓰므로 아래 순서대로 평가한다.
 * (FSM 이 쓰는 win_valid, mac_done 은 레지스터 출력이라 먼저 읽을 수 있다)
 */
void l2_top_comb(l2_top_t *t, const l2_in_t *in)
{
    total_ctrl_fsm_in_t    fsm_i;
    weight_addr_ctrl_in_t  wac_i;
    weight_rom_in_t        rom_i;
    mac_array_in_t         mac_i;
    line_buffer_in_t       lb_i;

    /* Total Control FSM */
    fsm_i.out_valid = in->out_valid;
    fsm_i.ch_done   = in->ch_done;
    fsm_i.win_valid = t->lb.win_valid;
    fsm_i.mac_done  = t->wac.mac_done;
    total_ctrl_fsm_comb(&t->fsm, &fsm_i, &t->fsm_o);

    /* Weight Addr Ctrl */
    wac_i.mac_start = t->fsm_o.mac_start;
    wac_i.is_ch35   = t->fsm_o.is_ch35;
    weight_addr_ctrl_comb(&t->wac, &wac_i, &t->wac_o);

    /* Weight ROM */
    rom_i.is_ch35    = t->wac_o.is_ch35;
    rom_i.out_ch_sel = t->wac_o.out_ch_sel;
    weight_rom_comb(&t->rom, &rom_i, &t->rom_o);

    /* Linebuffer Array */
    lb_i.pixel_valid = t->fsm_o.pixel_valid;
    lb_i.phase_clear = t->fsm_o.phase_clear;
    for (int lane = 0; lane < L2_LANES; lane++)
        lb_i.pixel_in[lane] = in->pixel_in[lane];
    line_buffer_comb(&t->lb, &lb_i, &t->lb_o);

    /* MAC Array */
    mac_i.valid = t->wac_o.weight_valid;
    mac_i.och   = t->wac_o.out_ch_sel;
    mac_i.pass  = t->rom_o.grp;
    for (int i = 0; i < L2_TAPS; i++)
        mac_i.weight[i] = t->rom_o.weight[i];
    for (int lane = 0; lane < L2_LANES; lane++)
        for (int ky = 0; ky < L2_K; ky++)
            for (int kx = 0; kx < L2_K; kx++)
                mac_i.win[lane][ky][kx] = t->lb_o.win[lane][ky][kx];
    mac_array_comb(&t->mac, &mac_i, &t->mac_o);
}

void l2_top_seq(l2_top_t *t)
{
    total_ctrl_fsm_seq(&t->fsm);
    weight_addr_ctrl_seq(&t->wac);
    line_buffer_seq(&t->lb);
    weight_rom_seq(&t->rom);
    mac_array_seq(&t->mac);
}
