/*
 * CE (Convolution Engine) 공통 정의: 데이터 타입, 최대 크기
 *   레이어별 크기는 ce_param_t (ce_top.h) 로 넘긴다.
 */
#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>

#define CE_LANES        3                           /* line buffer / MAC 채널 수 */
#define CE_K            3
#define CE_KK           (CE_K * CE_K)               /* 9 = 144bit */
#define CE_TAPS         (CE_LANES * CE_KK)          /* 27 = 144bit x 3 */

#define CE_MAX_C_OUT    16
#define CE_MAX_GROUPS   2                           /* 입력 채널 그룹 = ceil(C_IN / 3) */

/* ---------------- 양자화: INT16 ---------------- */
typedef int16_t act_t;      /* activation */
typedef int16_t wgt_t;      /* weight */

#endif
