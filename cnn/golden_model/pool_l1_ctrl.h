/*
 * pool_l1 Controller : 2x2 stride 2 max pooling (26 x 26 -> 13 x 13), 반쪽 행 버퍼 방식
 *
 *   row_odd = row_cnt[0], col_odd = col_cnt[0], win_valid = row_odd & col_odd
 *   out_ready    = pool_ready | ~win_valid          (출력을 만드는 픽셀만 다음 단 ready 를 본다)
 *   pixel_valid  = out_valid & out_ready
 *   pool_valid   = out_valid & win_valid
 *   pool_ch_done = pool_valid & (row_cnt == 25) & (col_cnt == 25)
 *   pool_mem_addr = col_cnt[4:1]
 *   prev_we = pixel_valid & ~col_odd,  mem_we = pixel_valid & col_odd & ~row_odd
 *
 *   카운터: pixel_valid 마다 col_cnt 25 -> 0 & row_cnt++, row_cnt 25 -> 0 (pass / frame 경계 자동)
 *   입력 ch_done 은 확인용: ch_done & pixel_valid 인데 (25,25) 가 아니면 dbg_ch_err_cnt++
 */
#ifndef POOL_L1_CTRL_H
#define POOL_L1_CTRL_H

#include <stdint.h>

#define POOL_L1_IN_H    26
#define POOL_L1_IN_W    26

/* input ports */
typedef struct
{
    uint8_t out_valid;      /* conv_l1 FIFO */
    uint8_t ch_done;        /* conv_l1 FIFO out_ch_done (확인용) */
    uint8_t pool_ready;     /* conv_l2 in_ready */
} pool_l1_ctrl_in_t;

/* output ports */
typedef struct
{
    uint8_t row_odd, col_odd, win_valid;
    uint8_t out_ready;      /* -> conv_l1 FIFO */
    uint8_t pixel_valid;
    uint8_t pool_valid;     /* -> conv_l2 in_valid */
    uint8_t pool_ch_done;   /* -> conv_l2 ch_done */
    uint8_t pool_mem_addr;  /* -> datapath */
    uint8_t prev_we;        /* -> datapath */
    uint8_t mem_we;         /* -> datapath */
    uint8_t ch_err;         /* debug */
} pool_l1_ctrl_out_t;

/* registers: reg / reg_next */
typedef struct
{
    uint8_t  row_cnt,  row_cnt_next;    /* 0 .. 25 */
    uint8_t  col_cnt,  col_cnt_next;    /* 0 .. 25 */

    /* debug */
    uint32_t pass_cnt, pass_cnt_next;   /* 끝난 pass 수 */
    uint8_t  w_ch_err;
    uint32_t dbg_ch_err_cnt;
} pool_l1_ctrl_t;

void pool_l1_ctrl_reset(pool_l1_ctrl_t *m);
void pool_l1_ctrl_comb(pool_l1_ctrl_t *m, const pool_l1_ctrl_in_t *in, pool_l1_ctrl_out_t *out);
void pool_l1_ctrl_seq(pool_l1_ctrl_t *m);

#endif
