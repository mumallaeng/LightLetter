/*
 * conv_l2 : Layer 2 convolution (13 x 13 x 6ch -> 11 x 11 x 16ch)
 *
 *   이전 단 --pixel_in0..2/in_valid/ch_done--> Total Control FSM --pixel_valid/phase_clear--> Line Buffer Array
 *   Line Buffer Array --win_valid--> Total Control FSM --mac_start/is_ch35--> Weight Addr Ctrl
 *   Weight Addr Ctrl  --mac_done--> Total Control FSM
 *   Weight Addr Ctrl  --is_ch35/out_ch_sel--> Weight ROM --weight_in--> MAC Array
 *   Weight Addr Ctrl  --weight_valid--> MAC Array (win_valid[2:0] 포트)
 *   Line Buffer Array --win_out--> MAC Array --ch_result0/1/2, mac_valid--> Output Buffer
 *   Output Buffer     --sum_data/sum_valid/ch_done--> ReLU & Quant (PACK 1) --out_data/out_valid--> 다음 단
 *
 * 입력 스트림: pass 0 (ch0~2) raster -> pass 1 (ch3~5) raster, 각 pass 마지막 픽셀에 ch_done.
 * MAC 은 window 하나를 잡아둔 채 out_ch_sel 0..15 동안 weight_valid 로 16번 계산한다.
 */
#ifndef CONV_L2_H
#define CONV_L2_H

#define CONV_L2_IN_H    13
#define CONV_L2_IN_W    13
#define CONV_L2_C_IN    6
#define CONV_L2_C_OUT   16
#define CONV_L2_PASSES  2
#define CONV_L2_OUT_H   (CONV_L2_IN_H - 2)
#define CONV_L2_OUT_W   (CONV_L2_IN_W - 2)
#define CONV_L2_N       (CONV_L2_OUT_H * CONV_L2_OUT_W)     /* 121 */
#define CONV_L2_PACK    1

/* line_buffer.h 의 IMG_WIDTH 는 컴파일 상수 */
#if defined(IMG_WIDTH) && IMG_WIDTH != CONV_L2_IN_W
#error "conv_l2 needs IMG_WIDTH == 13"
#endif
#ifndef IMG_WIDTH
#define IMG_WIDTH CONV_L2_IN_W
#endif

#include "common.h"
#include "total_ctrl_fsm.h"
#include "weight_addr_ctrl.h"
#include "weight_rom.h"
#include "line_buffer_array.h"
#include "mac_array.h"
#include "output_buffer.h"
#include "relu_quant.h"

/* top input ports */
typedef struct
{
    uint8_t in_valid;               /* 이전 단 out_valid */
    act_t   pixel_in[CE_LANES];     /* pixel_in0..2 */
    uint8_t ch_done;                /* 이전 단: pass 마지막 픽셀 */
    uint8_t out_ready;              /* 다음 단 */
} conv_l2_in_t;

/* top output ports */
typedef struct
{
    uint8_t  in_ready;              /* -> 이전 단 out_ready */
    uint16_t out_data;
    uint8_t  out_ch_done;
    uint8_t  out_valid;
} conv_l2_out_t;

typedef struct
{
    /* module instances */
    total_ctrl_fsm_t    fsm;
    weight_addr_ctrl_t  wac;
    weight_rom_t        rom;
    line_buffer_array_t lb;
    mac_array_t         mac;
    output_buffer_t     ob;
    relu_quant_t        rq;

    /* module output wires (conv_l2_comb 에서 갱신, seq / 모니터용) */
    total_ctrl_fsm_out_t   fsm_o;
    weight_addr_ctrl_out_t wac_o;
    weight_rom_out_t       rom_o;
    int16_t                mac_weight[CE_LANES][CE_KK];   /* weight_in[431:0] */
    output_buffer_in_t     ob_i;                          /* MAC 출력 레지스터 */
    output_buffer_out_t    ob_o;
    relu_quant_out_t       rq_o;
    act_t                  pixel_in[CE_LANES];
} conv_l2_t;

/* weight: [16][6][3][3], bias: [16], scale_exp: quantizer shift */
void conv_l2_init(conv_l2_t *m, const wgt_t *weight, const int32_t *bias, uint8_t scale_exp);
void conv_l2_comb(conv_l2_t *m, const conv_l2_in_t *in, conv_l2_out_t *out);   /* always @(*) */
void conv_l2_seq(conv_l2_t *m);                                               /* posedge clk */

/* FSM / WAC IDLE, MAC 파이프라인과 FIFO 가 비었는지 */
int  conv_l2_idle(const conv_l2_t *m);

#endif
