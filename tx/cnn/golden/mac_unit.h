/*
 * mac_unit.h
 * ---------------------------------------------------------------
 * mac_unit 단위 모듈 (담당: 조성재)
 *
 * ** RTL 구조 변경 반영 (setup timing 문제로 곱셈-덧셈 사이에 파이프라인
 *    레지스터 추가됨) **
 *   - 예전: psum_in도 없고, 레지스터도 없는 완전 무상태 조합 로직
 *   - 지금: mul0/1/2를 먼저 레지스터(mul*_reg)에 저장한 "다음 클록"에
 *     그 레지스터 값들을 더해서 psum_out을 냄 -> mac_unit 자체가
 *     1클록 레이턴시를 갖는 상태 모듈이 됨 (Vivado 타이밍 클로징 때문)
 *
 * 인터페이스 명세서 매핑:
 *   in : win_row[47:0], weight_row[47:0], row_valid
 *   out: psum_out[35:0] (1클록 지연), valid_out (1클록 지연)
 */

#ifndef MAC_UNIT_H
#define MAC_UNIT_H

#include <stdint.h>
#include <string.h>

typedef struct {
    /* ---- 레지스터 (RTL의 mul0_reg/mul1_reg/mul2_reg/valid_reg) ---- */
    int64_t mul0_reg;
    int64_t mul1_reg;
    int64_t mul2_reg;
    uint8_t valid_reg;
} mac_unit_t;

static inline void mac_unit_reset(mac_unit_t *m) {
    memset(m, 0, sizeof(*m));
}

/*
 * 한 클록의 동작. RTL과 동일한 "논블로킹 대입" 순서 재현:
 * 1) 먼저 "현재(갱신 전) mul_reg/valid_reg" 기준으로 psum_out/valid_out을 계산
 *    (RTL의 always @(*) Add 블록과 동일 - 레지스터를 읽기만 함)
 * 2) 그다음 이번 클록의 새 곱셈값으로 mul_reg/valid_reg를 갱신
 *    (RTL의 always @(posedge clk) 블록과 동일)
 */
static inline void mac_unit_step(mac_unit_t *m,
                                  const int16_t win_row[3],
                                  const int16_t weight_row[3],
                                  uint8_t row_valid,
                                  int64_t *psum_out,
                                  uint8_t *valid_out) {
    /* 1) 현재(갱신 전) 레지스터 값으로 출력 계산 - RTL의 Add 블록 */
    if (!m->valid_reg) {
        *psum_out = 0;
        *valid_out = 0;
    } else {
        *psum_out = m->mul0_reg + m->mul1_reg + m->mul2_reg;
        *valid_out = 1;
    }

    /* 2) 이번 클록의 새 곱셈값 계산 후 레지스터 갱신 - RTL의 always_ff 블록 */
    if (row_valid) {
        m->mul0_reg = (int64_t)win_row[0] * weight_row[0];
        m->mul1_reg = (int64_t)win_row[1] * weight_row[1];
        m->mul2_reg = (int64_t)win_row[2] * weight_row[2];
    } else {
        m->mul0_reg = 0;
        m->mul1_reg = 0;
        m->mul2_reg = 0;
    }
    m->valid_reg = row_valid;
}

#endif /* MAC_UNIT_H */
