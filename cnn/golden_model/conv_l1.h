/*
 * conv_l1 : Layer 1 convolution (28 x 28 x 1ch -> 26 x 26 x 6ch)
 *
 *   이전 단 --pixel_in/in_valid/ch_done--> Total Control FSM (l1) --pixel_valid/phase_clear--> Line Buffer (1개)
 *   Line Buffer       --win_valid--> Total Control FSM --mac_start--> Weight Addr Ctrl (l1)
 *   Weight Addr Ctrl  --mac_done--> Total Control FSM
 *   Weight Addr Ctrl  --out_ch_sel--> Weight ROM (OUT_CH 별 144bit x 3, INT16) --weight_in--> MAC Array
 *   Weight Addr Ctrl  --cal_valid--> MAC Array (win_valid[2:0] 포트)
 *   Line Buffer       --win_out--> MAC Array (lane 0, lane 1/2 = 0) --ch_result0, mac_valid--> Output Buffer
 *   Output Buffer     --sum_data/sum_valid/ch_done--> ReLU & Quant (PACK 3) --out_data0..2/out_valid--> 다음 단
 *
 * 입력 스트림: 28 x 28 raster, 마지막 픽셀에 ch_done.
 * 출력: 픽셀마다 FIFO entry 2개 {och0, och1, och2}, {och3, och4, och5}, 마지막 픽셀에 out_ch_done.
 * MAC 은 window 하나를 잡아둔 채 out_ch_sel 0..5 동안 cal_valid 로 6번 계산한다.
 */
#ifndef CONV_L1_H
#define CONV_L1_H

#define CONV_L1_IN_H    28
#define CONV_L1_IN_W    28
#define CONV_L1_C_IN    1
#define CONV_L1_C_OUT   6
#define CONV_L1_OUT_H   (CONV_L1_IN_H - 2)
#define CONV_L1_OUT_W   (CONV_L1_IN_W - 2)
#define CONV_L1_N       (CONV_L1_OUT_H * CONV_L1_OUT_W)     /* 676 */
#define CONV_L1_PACK    3

/* line_buffer.h 의 IMG_WIDTH 는 컴파일 상수 */
#if defined(IMG_WIDTH) && IMG_WIDTH != CONV_L1_IN_W
#error "conv_l1 needs IMG_WIDTH == 28"
#endif
#ifndef IMG_WIDTH
#define IMG_WIDTH CONV_L1_IN_W
#endif

#include "common.h"
#include "total_ctrl_fsm_l1.h"
#include "weight_addr_ctrl_l1.h"
#include "weight_rom.h"
#include "line_buffer.h"
#include "mac_array.h"
#include "output_buffer.h"
#include "relu_quant.h"

/* top input ports */
typedef struct
{
    uint8_t in_valid;               /* 이전 단 out_valid */
    act_t   pixel_in;
    uint8_t ch_done;                /* 이전 단: 마지막 픽셀 */
    uint8_t out_ready;              /* 다음 단 */
} conv_l1_in_t;

/* top output ports */
typedef struct
{
    uint8_t  in_ready;              /* -> 이전 단 out_ready */
    uint16_t out_data[CONV_L1_PACK];
    uint8_t  out_ch_done;
    uint8_t  out_valid;
} conv_l1_out_t;

typedef struct
{
    /* module instances */
    total_ctrl_fsm_l1_t   fsm;
    weight_addr_ctrl_l1_t wac;
    weight_rom_t          rom;
    line_buffer_t         lb;
    mac_array_t           mac;
    output_buffer_t       ob;
    relu_quant_t          rq;

    /* module output wires (conv_l1_comb 에서 갱신, seq / 모니터용) */
    total_ctrl_fsm_l1_out_t   fsm_o;
    weight_addr_ctrl_l1_out_t wac_o;
    weight_rom_out_t          rom_o;
    int16_t                   mac_weight[CE_LANES][CE_KK];   /* weight_in[431:0] */
    output_buffer_in_t        ob_i;                          /* MAC 출력 레지스터 */
    output_buffer_out_t       ob_o;
    relu_quant_out_t          rq_o;
    act_t                     pixel_in;
} conv_l1_t;

/* weight: [6][1][3][3], bias: [6], scale_exp: quantizer shift */
void conv_l1_init(conv_l1_t *m, const wgt_t *weight, const int32_t *bias, uint8_t scale_exp);
void conv_l1_comb(conv_l1_t *m, const conv_l1_in_t *in, conv_l1_out_t *out);   /* always @(*) */
void conv_l1_seq(conv_l1_t *m);                                               /* posedge clk */

/* FSM / WAC IDLE, MAC 파이프라인과 FIFO 가 비었는지 */
int  conv_l1_idle(const conv_l1_t *m);

#endif
