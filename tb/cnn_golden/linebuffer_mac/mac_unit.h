/*
 * mac_unit.h
 * ---------------------------------------------------------------
 * mac_unit 단위 모듈 (담당: 조성재)
 *
 * 설계 원칙:
 *   - 커널의 한 행(row)만 담당 (MAC 1개 = 곱셈 1개가 아니라, 한 행 처리 단위)
 *   - 레지스터 없는 순수 조합 로직 (한 클록 안에서 곱셈+누산 다 끝남)
 *
 * 인터페이스 명세서 매핑:
 *   in : win_row[47:0], weight_row[47:0], row_valid, psum_in[31:0]
 *   out: psum_out[31:0], valid_out
 */

#ifndef MAC_UNIT_H
#define MAC_UNIT_H

#include <stdint.h>

typedef struct {
    int32_t psum_out;
    uint8_t valid_out;
} mac_unit_result_t;

static inline mac_unit_result_t mac_unit_eval(const int16_t win_row[3],
                                               const int16_t weight_row[3],
                                               uint8_t row_valid,
                                               int32_t psum_in) {
    mac_unit_result_t r;
    if (!row_valid) {
        r.psum_out = 0;
        r.valid_out = 0;
        return r;
    }
    int32_t sum = (int32_t)win_row[0] * weight_row[0]
                 + (int32_t)win_row[1] * weight_row[1]
                 + (int32_t)win_row[2] * weight_row[2];
    r.psum_out = psum_in + sum;
    r.valid_out = 1;
    return r;
}

#endif /* MAC_UNIT_H */
