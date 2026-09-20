/*
 * mac_unit.h
 * ---------------------------------------------------------------
 * mac_unit 단위 모듈 (담당: 조성재)
 *
 * ** RTL 구조 변경 반영 (psum_in 체이닝 제거됨) **
 *   - 예전: row0->row1->row2가 psum_in으로 세로 체이닝
 *   - 지금: 각 행이 완전히 독립적으로 자기 몫(3개 곱)만 계산
 *     (열 안에서의 누적/합산은 이제 mac_array가 FF 파이프라인으로 담당)
 *   - 레지스터 없는 순수 조합 로직인 건 동일
 *
 * 인터페이스 명세서 매핑:
 *   in : win_row[47:0], weight_row[47:0], row_valid
 *   out: psum_out[35:0] (이 행 자신의 3개 곱 합, psum_in 없음), valid_out
 */

#ifndef MAC_UNIT_H
#define MAC_UNIT_H

#include <stdint.h>

typedef struct {
    int64_t psum_out;   /* RTL은 36bit, C는 여유롭게 int64_t로 (비교 시 36bit로 마스킹) */
    uint8_t valid_out;
} mac_unit_result_t;

static inline mac_unit_result_t mac_unit_eval(const int16_t win_row[3],
                                               const int16_t weight_row[3],
                                               uint8_t row_valid) {
    mac_unit_result_t r;
    if (!row_valid) {
        r.psum_out = 0;
        r.valid_out = 0;
        return r;
    }
    r.psum_out = (int64_t)win_row[0] * weight_row[0]
               + (int64_t)win_row[1] * weight_row[1]
               + (int64_t)win_row[2] * weight_row[2];
    r.valid_out = 1;
    return r;
}

#endif /* MAC_UNIT_H */
