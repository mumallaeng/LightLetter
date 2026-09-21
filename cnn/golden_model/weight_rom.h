/*
 * Weight ROM
 *
 *   OUT_CH 별 ROM (주소 = is_ch35, sync read) C_OUT 개 + out_ch_sel MUX
 *   한 entry = IN_CH 3개 x 3x3 x INT16 = 432bit (144bit x 3)
 *   conv1 (C_IN = 1) 은 lane 0 만 쓰고 lane 1, 2 는 0
 */
#ifndef WEIGHT_ROM_H
#define WEIGHT_ROM_H

#include "common.h"

/* input ports */
typedef struct
{
    uint8_t is_ch35;        /* Weight Addr Ctrl -> ROM 주소 */
    uint8_t out_ch_sel;     /* Weight Addr Ctrl -> MUX select */
} weight_rom_in_t;

/* output ports */
typedef struct
{
    wgt_t   weight[CE_TAPS];    /* -> MAC Array, [lane*9 + ky*3 + kx] */
    uint8_t grp;                /* 이번에 읽힌 그룹 (0: ch0~2, 1: ch3~5) */
} weight_rom_out_t;

/* registers: reg / reg_next */
typedef struct
{
    uint8_t grp_q, grp_q_next;                          /* ROM 주소 레지스터 */
    wgt_t   data[CE_MAX_C_OUT][CE_MAX_GROUPS][CE_TAPS]; /* ROM 내용 (상수) */
} weight_rom_t;

/* weight: [c_out][c_in][3][3] 를 flat 으로 */
void weight_rom_reset(weight_rom_t *m, const wgt_t *weight, int c_out, int c_in);
void weight_rom_comb(weight_rom_t *m, const weight_rom_in_t *in,
                     weight_rom_out_t *out);
void weight_rom_seq(weight_rom_t *m);

#endif
