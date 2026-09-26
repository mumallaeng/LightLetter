/*
 * pool_l2 : 2x2 stride 2 max pooling, conv_l2 -> 다음 단 (11 x 11 x 16 -> 5 x 5 x 16, floor)
 *   RTL: pool_l2.v = pool_ctrl_l2 + pool_datapath_l2 (max_logic 1 개 = lane 1 개, 안에 pool_buf)
 *
 *   conv_l2 --out_data / out_valid / out_ch_done--> pool_l2 --pool_data / pool_valid / pool_ch_done--> 다음 단
 *   conv_l2 <--out_ready-- pool_l2 <--pool_ready-- 다음 단
 *
 *   입력 순서: och0 11x11 raster -> och1 -> ... -> och15 (클럭당 픽셀 1 개)
 *   출력 순서: och0 5x5 raster -> ... -> och15, 채널마다 (9,9) 입력에서 나오는 (4,4) 출력에 pool_ch_done
 *   채널마다 마지막 행 / 열 (row 10, col 10) 은 출력 없이 흘려보낸다 (floor)
 *
 *   pool_ctrl_l2 : pool_l1_ctrl 과 식이 같아 그대로 재사용 (IN_H = IN_W = 11)
 *                  pool_addr = col_cnt >> 1, prev_we = pixel_valid & ~col_odd, pool_we = pixel_valid & col_odd & ~row_odd
 *   max_logic    : prev_reg (16b) + pool_buf (IF_W/2 = 5 x 16b, 비동기 읽기 = LUTRAM)
 *     pair       = (pool_in >= prev_reg) ? pool_in : prev_reg
 *     pool_rdata = pool_buf[pool_addr]
 *     pool_data  = (pool_rdata >= pair) ? pool_rdata : pair
 *     prev_we : prev_reg <- pool_in          (짝수 열)
 *     pool_we : pool_buf[pool_addr] <- pair  (짝수 행, 홀수 열)
 *
 *   pool_buf 깊이는 RTL 그대로 IF_W/2 = 5. 마지막 열 (col 10) 에서 pool_addr = 5 로 범위 밖을 읽는다
 *   (RTL 시뮬레이션에서는 X). 그때는 win_valid = 0 이라 pool_valid = 0 이고 값은 안 쓰인다.
 *   모델은 0 을 돌려주고 dbg_oob_rd 로 센다. pool_valid 와 겹치면 dbg_oob_used 가 올라간다 (0 이어야 함).
 *   값은 ReLU 뒤라 0 이상 -> 16b unsigned 비교.
 */
#ifndef POOL_L2_H
#define POOL_L2_H

#include <stdint.h>
#include "pool_l1_ctrl.h"

#ifndef POOL_L2_OUT_H
#define POOL_L2_OUT_H   (POOL_L2_IN_H / 2)
#define POOL_L2_OUT_W   (POOL_L2_IN_W / 2)
#endif
#define POOL_L2_MEM_SIZE    (POOL_L2_IN_W / 2)      /* 5, RTL pool_buf MEM_SIZE */

/* ================= datapath (pool_datapath_l2 / max_logic) ================= */

/* input ports */
typedef struct
{
    uint8_t  prev_we;           /* ctrl */
    uint16_t pool_in;           /* conv_l2 out_data */
    uint8_t  pool_addr;         /* ctrl */
    uint8_t  pool_we;           /* ctrl */
} pool_l2_datapath_in_t;

/* output ports */
typedef struct
{
    uint16_t pair;              /* max(prev_reg, pool_in) */
    uint16_t pool_rdata;        /* pool_buf[pool_addr] */
    uint16_t pool_data;         /* max(pool_rdata, pair) -> 다음 단 */
    uint8_t  rd_oob;            /* pool_addr >= MEM_SIZE (RTL 에서는 X 읽기) */
} pool_l2_datapath_out_t;

typedef struct
{
    /* registers: reg / reg_next */
    uint16_t prev_reg, prev_next;

    /* memory (pool_buf) */
    uint16_t ram[POOL_L2_MEM_SIZE];

    /* write request carried from comb to seq */
    uint8_t  w_pool_we;
    uint8_t  w_pool_addr;
    uint16_t w_pool_wdata;
} pool_l2_datapath_t;

/* ================= top ================= */

/* top input ports */
typedef struct
{
    uint8_t  out_valid;         /* conv_l2 */
    uint16_t out_data;          /* conv_l2 out_data */
    uint8_t  ch_done;           /* conv_l2 out_ch_done (확인용, RTL 에서는 안 씀) */
    uint8_t  pool_ready;        /* 다음 단 in_ready */
} pool_l2_in_t;

/* top output ports */
typedef struct
{
    uint8_t  out_ready;         /* -> conv_l2 */
    uint8_t  pool_valid;        /* -> 다음 단 in_valid */
    uint16_t pool_data;         /* -> 다음 단 */
    uint8_t  pool_ch_done;      /* -> 다음 단 ch_done */
} pool_l2_out_t;

typedef struct
{
    /* module instances */
    pool_l1_ctrl_t     ctrl;    /* = pool_ctrl_l2 */
    pool_l2_datapath_t dp;

    /* module output wires (pool_l2_comb 에서 갱신, 모니터 / 로그용) */
    pool_l1_ctrl_out_t     ctrl_o;
    pool_l2_datapath_out_t dp_o;

    /* debug */
    uint8_t  w_oob_rd, w_oob_used;
    uint32_t dbg_oob_rd;        /* 범위 밖 읽기 횟수 (col 10 마다, 정상) */
    uint32_t dbg_oob_used;      /* 범위 밖 읽기가 pool_valid 와 겹친 횟수 (0 이어야 함) */
} pool_l2_t;

void pool_l2_datapath_reset(pool_l2_datapath_t *m);
void pool_l2_datapath_comb(pool_l2_datapath_t *m, const pool_l2_datapath_in_t *in,
                           pool_l2_datapath_out_t *out);
void pool_l2_datapath_seq(pool_l2_datapath_t *m);

void pool_l2_init(pool_l2_t *m);                                                /* parameter 설정 + reset */
void pool_l2_reset(pool_l2_t *m);
void pool_l2_comb(pool_l2_t *m, const pool_l2_in_t *in, pool_l2_out_t *out);    /* always @(*) */
void pool_l2_seq(pool_l2_t *m);                                                 /* posedge clk */

#endif
