/*
 * MAC Array
 *
 *   [weight 입력 레지스터 CFG_WEIGHT_PIPE 단] -> 27 MAC 병렬 -> [CFG_MAC_PIPE 단]
 *   MAC 입력 단에서 그 사이클의 window 를 사용한다.
 *   출력 psum 은 부분합 버퍼(별도 담당자)로 간다.
 */
#ifndef MAC_ARRAY_H
#define MAC_ARRAY_H

#include "common.h"

typedef struct
{
    uint8_t valid;
    uint8_t och;
    uint8_t pass;
    wgt_t   weight[L2_TAPS];
} weight_bus_t;

typedef struct
{
    uint8_t valid;
    uint8_t och;
    uint8_t pass;
    psum_t  data;
} psum_bus_t;

/* input ports */
typedef struct
{
    uint8_t valid;                          /* Weight Addr Ctrl (weight_valid) */
    uint8_t och;                            /* Weight Addr Ctrl (out_ch_sel)   */
    uint8_t pass;                           /* Weight ROM (grp)                */
    wgt_t   weight[L2_TAPS];                /* Weight ROM                      */
    act_t   win[L2_LANES][L2_K][L2_K];      /* Linebuffer Array                */
} mac_array_in_t;

/* output ports */
typedef struct
{
    psum_bus_t psum;                        /* -> 부분합 버퍼 */
} mac_array_out_t;

/* registers: reg / reg_next */
typedef struct
{
    weight_bus_t wpipe[CFG_WEIGHT_PIPE + 1], wpipe_next[CFG_WEIGHT_PIPE + 1];
    psum_bus_t   macp[CFG_MAC_PIPE],         macp_next[CFG_MAC_PIPE];
} mac_array_t;

void mac_array_reset(mac_array_t *m);
void mac_array_comb(mac_array_t *m, const mac_array_in_t *in,
                    mac_array_out_t *out);
void mac_array_seq(mac_array_t *m);

/* 파이프라인에 남은 데이터가 있는지 (testbench 종료 판단용) */
int mac_array_busy(const mac_array_t *m);

#endif
