/*
 * 공통 정의: 레이어 크기, 데이터 타입, 설정 옵션
 */
#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>

/* ---------------- Layer 2 크기 ---------------- */
#define L2_IN_H         26
#define L2_IN_W         26
#define L2_IN_CH        6
#define L2_OUT_CH       16
#define L2_LANES        3                       /* line buffer 개수 */
#define L2_NUM_PASSES   (L2_IN_CH / L2_LANES)   /* 2 */
#define L2_K            3
#define L2_OUT_H        (L2_IN_H - L2_K + 1)
#define L2_OUT_W        (L2_IN_W - L2_K + 1)
#define L2_NUM_WIN      (L2_OUT_H * L2_OUT_W)
#define L2_TAPS         (L2_LANES * L2_K * L2_K) /* 27 = 144bit x 3 */

/* ---------------- 양자화: INT16 ---------------- */
typedef int16_t act_t;      /* activation */
typedef int16_t wgt_t;      /* weight */
typedef int64_t psum_t;     /* 16x16=32bit 곱 27개 합 -> 최대 36bit */

#define ACT_BITS        16
#define WGT_BITS        16

/* ---------------- 설정 옵션 (gcc -D 로 변경) ---------------- */

/* 0: 그림 그대로 - out_ready = (IDLE|CH02_IMG_IN|CH35_IMG_IN) & ~win_valid
 * 1: out_ready 에 & ~phase_clear 추가                                        */
#ifndef CFG_READY_BLOCK_ON_CLEAR
#define CFG_READY_BLOCK_ON_CLEAR    0
#endif

/* Weight MUX 출력 ~ MAC 입력 사이 레지스터 단수 (그림 기준 0) */
#ifndef CFG_WEIGHT_PIPE
#define CFG_WEIGHT_PIPE             0
#endif

/* MAC Array 내부 단수: 곱셈 레지스터 + adder tree 레지스터 (>= 1) */
#ifndef CFG_MAC_PIPE
#define CFG_MAC_PIPE                2
#endif

#if CFG_MAC_PIPE < 1
#error "CFG_MAC_PIPE must be >= 1"
#endif

#endif
