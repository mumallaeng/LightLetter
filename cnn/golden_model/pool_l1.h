/*
 * pool_l1 : 2x2 stride 2 max pooling, conv_l1 -> conv_l2 (26 x 26 x 6 -> 13 x 13 x 6)
 *
 *   conv_l1 FIFO --out_data0..2 / out_valid / out_ch_done--> pool_l1 --pool_data0..2 / pool_valid / pool_ch_done--> conv_l2
 *   conv_l1 FIFO <--out_ready-- pool_l1 <--pool_ready (= conv_l2 in_ready)-- conv_l2
 *
 *   입력 순서 (팀원 FIFO 변경 후): pass 0 (och0~2) 26x26 raster -> pass 1 (och3~5) 26x26 raster
 *   출력 순서: pass 0 13x13 raster -> pass 1 13x13 raster, pass 마지막 (12,12) 에 pool_ch_done
 *
 *   pool_l1_ctrl     : row_cnt / col_cnt, handshake, prev_we / mem_we / pool_mem_addr
 *   pool_l1_datapath : lane LANES 개, prev_reg + pool_mem (반쪽 행 버퍼) + max 2단
 *
 *   parameter (IN_H, IN_W, LANES) 로 pool_l2 에도 그대로 쓴다: pool_l1_init(m, POOL_L2_IN_H, POOL_L2_IN_W, POOL_L2_LANES)
 *   pool_l2 : conv_l2 -> 다음 단 (11 x 11 x 16 -> 5 x 5 x 16, floor), och0 raster -> ... -> och15 raster,
 *             채널마다 마지막 행 / 열 (row 10, col 10) 은 출력 없이 흘려보내고 (9,9) 출력에 pool_ch_done
 */
#ifndef POOL_L1_H
#define POOL_L1_H

#include "pool_l1_ctrl.h"
#include "pool_l1_datapath.h"

#define POOL_L1_OUT_H   (POOL_L1_IN_H / 2)
#define POOL_L1_OUT_W   (POOL_L1_IN_W / 2)
#define POOL_L2_OUT_H   (POOL_L2_IN_H / 2)
#define POOL_L2_OUT_W   (POOL_L2_IN_W / 2)

/* top input ports */
typedef struct
{
    uint8_t  out_valid;                     /* conv_l1 FIFO */
    uint16_t out_data[POOL_L1_LANES];       /* out_data0..2 */
    uint8_t  ch_done;                       /* conv_l1 FIFO out_ch_done (확인용) */
    uint8_t  pool_ready;                    /* conv_l2 in_ready */
} pool_l1_in_t;

/* top output ports */
typedef struct
{
    uint8_t  out_ready;                     /* -> conv_l1 FIFO */
    uint8_t  pool_valid;                    /* -> conv_l2 in_valid */
    uint16_t pool_data[POOL_L1_LANES];      /* -> conv_l2 pixel_in0..2 */
    uint8_t  pool_ch_done;                  /* -> conv_l2 ch_done */
} pool_l1_out_t;

typedef struct
{
    /* module instances */
    pool_l1_ctrl_t     ctrl;
    pool_l1_datapath_t dp;

    /* module output wires (pool_l1_comb 에서 갱신, 모니터 / 로그용) */
    pool_l1_ctrl_out_t     ctrl_o;
    pool_l1_datapath_out_t dp_o;
} pool_l1_t;

void pool_l1_init(pool_l1_t *m, uint8_t in_h, uint8_t in_w, uint8_t lanes);   /* parameter 설정 + reset */
void pool_l1_reset(pool_l1_t *m);                                               /* parameter 유지 */
void pool_l1_comb(pool_l1_t *m, const pool_l1_in_t *in, pool_l1_out_t *out);   /* always @(*) */
void pool_l1_seq(pool_l1_t *m);                                               /* posedge clk */

#endif
