/*
 * Layer 2 Top: 모듈 인스턴스 + 배선
 *
 *   이전 레이어 --pixel_in/out_valid/ch_done--> Total Control FSM
 *   Total Control FSM --pixel_valid/phase_clear--> Linebuffer Array
 *   Linebuffer Array  --win_valid--> Total Control FSM
 *   Total Control FSM --mac_start/is_ch35--> Weight Addr Ctrl
 *   Weight Addr Ctrl  --mac_done--> Total Control FSM
 *   Weight Addr Ctrl  --is_ch35/out_ch_sel--> Weight ROM --weight--> MAC Array
 *   Linebuffer Array  --win--> MAC Array --psum--> 부분합 버퍼
 */
#ifndef L2_TOP_H
#define L2_TOP_H

#include "common.h"
#include "total_ctrl_fsm.h"
#include "weight_addr_ctrl.h"
#include "line_buffer.h"
#include "weight_rom.h"
#include "mac_array.h"

/* top input ports (이전 레이어) */
typedef struct
{
    uint8_t out_valid;
    act_t   pixel_in[L2_LANES];
    uint8_t ch_done;
} l2_in_t;

typedef struct
{
    /* module instances */
    total_ctrl_fsm_t   fsm;
    weight_addr_ctrl_t wac;
    line_buffer_t      lb;
    weight_rom_t       rom;
    mac_array_t        mac;

    /* module output wires (l2_top_comb 에서 갱신, trace 용) */
    total_ctrl_fsm_out_t   fsm_o;
    weight_addr_ctrl_out_t wac_o;
    line_buffer_out_t      lb_o;
    weight_rom_out_t       rom_o;
    mac_array_out_t        mac_o;
} l2_top_t;

void l2_top_reset(l2_top_t *t, const wgt_t weight[L2_OUT_CH][L2_IN_CH][L2_K][L2_K]);
void l2_top_comb(l2_top_t *t, const l2_in_t *in);   /* 모든 always @(*) */
void l2_top_seq(l2_top_t *t);                       /* posedge clk      */

#endif
