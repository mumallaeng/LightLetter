/*
 * pool_l1 Datapath : parameter LANES (1 .. 3)
 *   pool_l1 : LANES 3 (lane0 = och0/3, lane1 = och1/4, lane2 = och2/5)
 *   pool_l2 : LANES 1 (lane0 = och0 .. och15), lane1 / lane2 출력은 0
 *
 *   lane 마다 prev_reg (16b) + pool_mem ((IN_W+1)/2 x 16b, 비동기 읽기 = LUTRAM)
 *   깊이가 IN_W/2 가 아닌 (IN_W+1)/2 인 이유: IN_W 가 홀수면 마지막 짝수 열 (11 -> col 10) 에서
 *   pool_mem_addr = IN_W/2 로 읽는다 (값은 win_valid = 0 이라 안 쓰이지만 주소는 범위 안이어야 함)
 *   pair      = max(prev_reg, data)
 *   mem_rd    = pool_mem[pool_mem_addr]
 *   pool_data = max(mem_rd, pair)
 *   prev_we : prev_reg <- data          (짝수 열)
 *   mem_we  : pool_mem[addr] <- pair    (짝수 행, 홀수 열)
 *
 *   값은 ReLU 뒤라 0 이상 -> 16b unsigned 비교. prev_reg / pool_mem 은 읽기 전에 항상 새로 쓰여 초기화 불필요.
 */
#ifndef POOL_L1_DATAPATH_H
#define POOL_L1_DATAPATH_H

#include <stdint.h>
#include "pool_l1_ctrl.h"

#define POOL_L1_LANES       3                           /* 포트 폭 (최대 lane 수) */
#define POOL_L2_LANES       1
#define POOL_L1_MEM_DEPTH   ((POOL_MAX_IN_W + 1) / 2)   /* 최대 깊이 13, 실제 깊이 (IN_W+1)/2 */

/* input ports */
typedef struct
{
    uint16_t data[POOL_L1_LANES];   /* conv_l1 FIFO out_data0..2 */
    uint8_t  pool_mem_addr;         /* ctrl */
    uint8_t  prev_we;               /* ctrl */
    uint8_t  mem_we;                /* ctrl */
} pool_l1_datapath_in_t;

/* output ports */
typedef struct
{
    uint16_t pair[POOL_L1_LANES];       /* max(prev_reg, data) */
    uint16_t mem_rd[POOL_L1_LANES];     /* pool_mem[addr] */
    uint16_t pool_data[POOL_L1_LANES];  /* -> conv_l2 pixel_in0..2 */
} pool_l1_datapath_out_t;

typedef struct
{
    /* parameter (reset 에도 유지) */
    uint8_t  lanes;
    uint8_t  mem_depth;

    /* registers: reg / reg_next */
    uint16_t prev_reg[POOL_L1_LANES], prev_reg_next[POOL_L1_LANES];

    /* memory */
    uint16_t pool_mem[POOL_L1_LANES][POOL_L1_MEM_DEPTH];

    /* write request carried from comb to seq */
    uint8_t  w_mem_we;
    uint8_t  w_mem_addr;
    uint16_t w_mem_wdata[POOL_L1_LANES];
} pool_l1_datapath_t;

void pool_l1_datapath_init(pool_l1_datapath_t *m, uint8_t lanes, uint8_t in_w);   /* parameter 설정 + reset */
void pool_l1_datapath_reset(pool_l1_datapath_t *m);
void pool_l1_datapath_comb(pool_l1_datapath_t *m, const pool_l1_datapath_in_t *in,
                           pool_l1_datapath_out_t *out);
void pool_l1_datapath_seq(pool_l1_datapath_t *m);

#endif
