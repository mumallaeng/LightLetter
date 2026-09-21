/*
 * CE (Convolution Engine) Top: 모듈 인스턴스 + 배선
 *
 *   이전 단 --pixel_in/in_valid/ch_done--> Total Control FSM --pixel_valid/phase_clear--> Line Buffer
 *   Line Buffer       --win_valid--> Total Control FSM --mac_start/is_ch35--> Weight Addr Ctrl
 *   Weight Addr Ctrl  --mac_done--> Total Control FSM
 *   Weight Addr Ctrl  --is_ch35/out_ch_sel--> Weight ROM --weight--> MAC Array
 *   Weight Addr Ctrl  --cal_valid--> MAC Array (win_valid[2:0] 포트에 연결)
 *   Line Buffer       --win_out--> MAC Array --ch_result0/1/2, mac_valid--> Output Buffer
 *   Output Buffer     --sum_data/sum_valid/ch_done--> ReLU & Quant --out_data/out_valid--> 다음 단 (MaxPooling)
 *
 * Line Buffer 는 레이어마다 다르다 (IMG_WIDTH 가 컴파일 상수라 파일을 나눔)
 *   conv1 : line_buffer 1개,        28 x 28 x 1ch  (ce_lb_l1.c)
 *   conv2 : line_buffer_array (3개), 13 x 13 x 6ch, 2 pass (ce_lb_l2.c)
 *
 * MAC 은 window 하나를 잡아둔 채 out_ch_sel 0..C_OUT-1 동안 cal_valid 로 돈다.
 * (line buffer 의 win_valid 는 1clk pulse 라 MAC valid 로는 쓸 수 없음)
 */
#ifndef CE_TOP_H
#define CE_TOP_H

#include "common.h"
#include "total_ctrl_fsm_l2.h"
#include "weight_addr_ctrl_l2.h"
#include "weight_rom.h"
#include "mac_array.h"
#include "output_buffer.h"
#include "relu_quant.h"

/* parameter */
typedef struct
{
    uint8_t layer;          /* 1 = conv1, 2 = conv2 */
    uint8_t in_h, in_w;
    uint8_t c_in, c_out;
    uint8_t pack;           /* ReLU & Quant: conv1 = 3, conv2 = 1 */
    uint8_t scale_exp;
} ce_param_t;

/* top input ports */
typedef struct
{
    uint8_t in_valid;               /* 이전 단 out_valid */
    act_t   pixel_in[CE_LANES];     /* 이전 단 (conv1 은 lane 0 만) */
    uint8_t ch_done;                /* 이전 단: pass 마지막 픽셀 */
    uint8_t out_ready;              /* 다음 단 (MaxPooling) */
} ce_in_t;

/* top output ports */
typedef struct
{
    uint8_t  in_ready;              /* -> 이전 단 out_ready */
    uint16_t out_data0;
    uint16_t out_data1;             /* 0 if PACK < 2 */
    uint16_t out_data2;             /* 0 if PACK < 3 */
    uint8_t  out_ch_done;
    uint8_t  out_valid;
} ce_out_t;

/* 레이어별 Line Buffer 연결 */
typedef struct
{
    void   *inst;
    uint8_t (*win_valid)(const void *inst);
    void    (*win_out)(const void *inst, int16_t win[CE_LANES][CE_KK]);
    void    (*step)(void *inst, const act_t pixel_in[CE_LANES],
                    uint8_t pixel_valid, uint8_t phase_clear);   /* always_ff */
} ce_lb_port_t;

ce_lb_port_t ce_lb_l1_new(int in_w);
ce_lb_port_t ce_lb_l2_new(int in_w);

typedef struct
{
    ce_param_t p;

    /* module instances */
    total_ctrl_fsm_l2_t   fsm;
    weight_addr_ctrl_l2_t wac;
    weight_rom_t       rom;
    ce_lb_port_t       lb;
    mac_array_t        mac;
    output_buffer_t    ob;
    relu_quant_t       rq;

    /* module output wires (ce_top_comb 에서 갱신, seq / trace 용) */
    total_ctrl_fsm_l2_out_t   fsm_o;
    weight_addr_ctrl_l2_out_t wac_o;
    weight_rom_out_t       rom_o;
    output_buffer_in_t     ob_i;
    output_buffer_out_t    ob_o;
    relu_quant_out_t       rq_o;
    uint8_t                lb_win_valid;
    act_t                  pixel_in[CE_LANES];
    int16_t                mac_weight[CE_LANES][CE_KK];
} ce_top_t;

/* weight: [c_out][c_in][3][3], bias: [c_out] */
int  ce_top_init(ce_top_t *t, const ce_param_t *p, const wgt_t *weight, const int32_t *bias);
void ce_top_free(ce_top_t *t);
void ce_top_comb(ce_top_t *t, const ce_in_t *in, ce_out_t *out);   /* 모든 always @(*) */
void ce_top_seq(ce_top_t *t);                                      /* posedge clk      */

/* 한 프레임이 끝나고 파이프라인이 모두 비었는지 (testbench 종료 판단용) */
int  ce_top_idle(const ce_top_t *t);

#endif
