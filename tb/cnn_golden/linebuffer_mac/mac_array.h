/*
 * mac_array.h
 * ---------------------------------------------------------------
 * mac_array 상위 모듈 (담당: 조성재)
 *
 * 설계 원칙:
 *   - mac_unit 9개(3열 x 3행)를 손으로 인스턴스화 (generate 문 안 씀)
 *   - 열(채널) 안에서만 세로로 psum 체이닝 (row0->row1->row2)
 *   - 열 간 최종 덧셈기 없음 -> 채널별 결과를 개별로 Output Buffer Controller에 전달
 *   - num_active_ch 파라미터: 지금 실제로 몇 개 열(채널)이 쓰이는지
 *       (line_buffer_array의 NUM_BUFFERS와 항상 같은 값으로 맞춰서 호출)
 *       conv1처럼 1개만 쓰는 경우, 안 쓰는 열의 valid_out은 영원히 0이라
 *       mac_valid 계산에서 그 열들을 아예 빼야 함 (이번에 고친 부분)
 *
 * 인터페이스 명세서 매핑:
 *   in : win_in[431:0] (win_in[3][9]), weight_in[431:0] (weight_in[3][9]), win_valid[2:0]
 *        + num_active_ch (파라미터, 포트 아님 - line_buffer_array의 NUM_BUFFERS와 동일하게 맞춤)
 *   out: ch_result0/1/2[31:0], mac_valid
 */

#ifndef MAC_ARRAY_H
#define MAC_ARRAY_H

#include "mac_unit.h"

typedef struct {
    int32_t ch_result0;
    int32_t ch_result1;
    int32_t ch_result2;
    uint8_t mac_valid;
} mac_array_result_t;

static inline mac_array_result_t mac_array_eval(const int16_t win_in[3][9],
                                                  const int16_t weight_in[3][9],
                                                  const uint8_t win_valid[3],
                                                  int32_t num_active_ch) {
    mac_array_result_t out;
    int32_t ch_sum[3] = {0, 0, 0};
    uint8_t all_valid = 1;

    for (int col = 0; col < 3; col++) {
        int32_t psum = 0;
        uint8_t v = 0;
        /* num_active_ch 밖의 열은 계산에서 빠질 뿐 아니라, 애초에
         * win_in/weight_in을 읽지도 않는다 (호출자가 그 슬롯을 채워뒀다는
         * 보장이 없으므로 - 안 쓰는 데이터를 읽는 것 자체를 원천 차단) */
        if (col < num_active_ch) {
            for (int row = 0; row < 3; row++) {
                int16_t wr[3] = { win_in[col][row*3 + 0], win_in[col][row*3 + 1], win_in[col][row*3 + 2] };
                int16_t gr[3] = { weight_in[col][row*3 + 0], weight_in[col][row*3 + 1], weight_in[col][row*3 + 2] };
                mac_unit_result_t r = mac_unit_eval(wr, gr, win_valid[col], psum);
                psum = r.psum_out;
                v = r.valid_out;
            }
            all_valid = (uint8_t)(all_valid && v);
        }
        ch_sum[col] = psum; /* 안 쓰는 열은 항상 0 */
    }

    out.ch_result0 = ch_sum[0];
    out.ch_result1 = ch_sum[1];
    out.ch_result2 = ch_sum[2];
    out.mac_valid = all_valid;
    return out;
}

#endif /* MAC_ARRAY_H */
