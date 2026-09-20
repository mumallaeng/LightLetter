/*
 * mac_array.h
 * ---------------------------------------------------------------
 * mac_array 상위 모듈 (담당: 조성재)
 *
 * ** RTL 구조 변경 반영 (2단 FF 파이프라인으로 재설계됨) **
 *   - 예전: mac_unit 9개를 psum_in으로 체이닝, 순수 조합 로직 (0클록 지연)
 *   - 지금: 각 행을 독립 계산 -> FF #1(행별 합+valid 저장)
 *           -> 3개 조합 합산 -> FF #2(최종 ch_result/mac_valid 저장)
 *           => line_buffer_array 대비 mac_valid/ch_result가 2클록 늦게 나옴
 *
 * 중요: RTL의 always @(posedge clk) 두 블록은 "같은 엣지에서 서로의
 * 갱신 전(pre-edge) 값을 읽는" 논블로킹 대입 의미를 갖는다. 그래서
 * 이 C 모델도 "이번 클록 계산에 쓸 이전 레지스터 값을 전부 먼저
 * 읽어서 계산한 뒤에, 마지막에 한꺼번에 레지스터를 갱신"하는 순서로 짬
 * (실제로 순서를 바꾸면 RTL과 다른 결과가 나옴 - 아래 mac_array_step 참고).
 *
 * 인터페이스 명세서 매핑:
 *   in : win_in[431:0], win_valid[2:0], weight_in[431:0]
 *   out: ch_result0/1/2[35:0] (2클록 지연), mac_valid (2클록 지연)
 */

#ifndef MAC_ARRAY_H
#define MAC_ARRAY_H

#include <string.h>
#include "mac_unit.h"

typedef struct {
    /* ---- FF #1: 행별(row0/1/2) 원시 합 + 그 채널의 3행 다 valid인지 ---- */
    int64_t c_row_reg[3][3];   /* [channel][row] */
    uint8_t c_valid_reg[3];    /* [channel] */

    /* ---- FF #2: 최종 채널별 합산 결과 + 전체 mac_valid ---- */
    int64_t ch_result_reg[3];
    uint8_t mac_valid_reg;
} mac_array_t;

static inline void mac_array_reset(mac_array_t *m) {
    memset(m, 0, sizeof(*m));
}

static inline void mac_array_step(mac_array_t *m,
                                   const int16_t win_in[3][9],
                                   const int16_t weight_in[3][9],
                                   const uint8_t win_valid[3],
                                   int32_t num_active_ch) {
    /* ---- 조합 로직: 9개 mac_unit 독립 계산 (이번 클록의 새 입력 기준) ---- */
    int64_t row_sum[3][3];
    uint8_t row_valid_out[3][3];
    for (int ch = 0; ch < 3; ch++) {
        for (int row = 0; row < 3; row++) {
            int16_t wr[3] = { win_in[ch][row*3 + 0], win_in[ch][row*3 + 1], win_in[ch][row*3 + 2] };
            int16_t gr[3] = { weight_in[ch][row*3 + 0], weight_in[ch][row*3 + 1], weight_in[ch][row*3 + 2] };
            mac_unit_result_t r = mac_unit_eval(wr, gr, win_valid[ch]);
            row_sum[ch][row]       = r.psum_out;
            row_valid_out[ch][row] = r.valid_out;
        }
    }

    /* ---- FF #2가 쓸 값: "현재(갱신 전) FF #1 레지스터" 기준으로 먼저 계산 ---- */
    int64_t final_sum[3];
    for (int ch = 0; ch < 3; ch++)
        final_sum[ch] = m->c_row_reg[ch][0] + m->c_row_reg[ch][1] + m->c_row_reg[ch][2];

    uint8_t next_mac_valid;
    if (num_active_ch >= 3)      next_mac_valid = (uint8_t)(m->c_valid_reg[0] & m->c_valid_reg[1] & m->c_valid_reg[2]);
    else if (num_active_ch == 2) next_mac_valid = (uint8_t)(m->c_valid_reg[0] & m->c_valid_reg[1]);
    else                          next_mac_valid = m->c_valid_reg[0];

    /* ---- 이제 "동시에" 갱신 (논블로킹 대입 순서 재현: 위에서 이미 OLD 값 다 읽었음) ---- */
    for (int ch = 0; ch < 3; ch++) m->ch_result_reg[ch] = final_sum[ch];
    m->mac_valid_reg = next_mac_valid;

    for (int ch = 0; ch < 3; ch++) {
        for (int row = 0; row < 3; row++) m->c_row_reg[ch][row] = row_sum[ch][row];
        m->c_valid_reg[ch] = (uint8_t)(row_valid_out[ch][0] & row_valid_out[ch][1] & row_valid_out[ch][2]);
    }
}

static inline int64_t mac_array_ch_result(const mac_array_t *m, int ch) { return m->ch_result_reg[ch]; }
static inline uint8_t mac_array_valid(const mac_array_t *m) { return m->mac_valid_reg; }

#endif /* MAC_ARRAY_H */
