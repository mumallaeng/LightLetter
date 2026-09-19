/*
 * Linebuffer Array (3 lane 병렬)
 *
 *   pixel_valid 때 3 lane 픽셀을 push -> 3x3 window 3개 갱신
 *   window 가 완성되면 다음 사이클 win_valid 1clk pulse
 *   phase_clear 가 pixel_valid 보다 우선 (카운터/valid 만 리셋, 메모리 유지)
 */
#ifndef LINE_BUFFER_H
#define LINE_BUFFER_H

#include "common.h"

/* input ports */
typedef struct
{
    uint8_t pixel_valid;            /* Total Control FSM */
    uint8_t phase_clear;            /* Total Control FSM */
    act_t   pixel_in[L2_LANES];     /* 이전 레이어 */
} line_buffer_in_t;

/* output ports */
typedef struct
{
    uint8_t win_valid;                      /* -> Total Control FSM */
    act_t   win[L2_LANES][L2_K][L2_K];      /* -> MAC Array */
} line_buffer_out_t;

/* registers: reg / reg_next */
typedef struct
{
    /* line memory: [lane][0] = row-1, [lane][1] = row-2 (async read) */
    act_t    mem[L2_LANES][2][L2_IN_W];
    uint8_t  mem_we;                        /* write port (comb 에서 생성) */
    uint16_t mem_waddr;
    act_t    mem_wdata[L2_LANES][2];

    act_t    win[L2_LANES][L2_K][L2_K];
    act_t    win_next[L2_LANES][L2_K][L2_K];
    uint16_t col,       col_next;
    uint16_t row,       row_next;
    uint8_t  win_valid, win_valid_next;
} line_buffer_t;

void line_buffer_reset(line_buffer_t *m);
void line_buffer_comb(line_buffer_t *m, const line_buffer_in_t *in,
                      line_buffer_out_t *out);
void line_buffer_seq(line_buffer_t *m);

#endif
