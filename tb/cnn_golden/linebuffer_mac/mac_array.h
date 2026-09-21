/*
 * mac_array.h
 * ---------------------------------------------------------------
 * mac_array 상위 모듈 (담당: 조성재)
 *
 * ** mac_unit 자체에 레지스터가 생기면서, 이제 9개 mac_unit_t를
 *    "상태 있는" 것으로 직접 소유하고 매 클록 step 시킴 **
 *   - 예전: mac_unit_eval()을 매 클록 새로 호출하는 무상태 함수 호출
 *   - 지금: mac_unit_t 9개(3열x3행)를 mac_array_t가 직접 보유,
 *     매 클록 각자의 mac_unit_step() 호출
 *
 * 전체 레이턴시(line_buffer_array의 win_valid 기준):
 *   mac_unit 자신의 1클록 + 기존 FF#1/FF#2 2클록 = mac_array 자체 2클록
 *   (line_buffer의 1클록까지 합치면 전체 파이프라인 총 4클록)
 *
 * 인터페이스 명세서 매핑:
 *   in : win_in[431:0], win_valid[2:0], weight_in[431:0]
 *   out: ch_result0/1/2[35:0], mac_valid
 */

#ifndef MAC_ARRAY_H
#define MAC_ARRAY_H

#include <string.h>
#include "mac_unit.h"

typedef struct {
    /* ---- mac_unit 9개 (3열x3행), 이제 각자 자기 레지스터를 가짐 ---- */
    mac_unit_t mu[3][3];   /* [channel][row] */

    /* ---- FF #1: 행별(row0/1/2) 원시 합 + 그 채널의 3행 다 valid인지 ---- */
    int64_t c_row_reg[3][3];   /* [channel][row] */
    uint8_t c_valid_reg[3];    /* [channel] */

    /* ---- FF #2: 최종 채널별 합산 결과 + 전체 mac_valid ---- */
    int64_t ch_result_reg[3];
    uint8_t mac_valid_reg;
} mac_array_t;

static inline void mac_array_reset(mac_array_t *m) {
    for (int ch = 0; ch < 3; ch++)
        for (int row = 0; row < 3; row++)
            mac_unit_reset(&m->mu[ch][row]);
    memset(m->c_row_reg, 0, sizeof(m->c_row_reg));
    memset(m->c_valid_reg, 0, sizeof(m->c_valid_reg));
    memset(m->ch_result_reg, 0, sizeof(m->ch_result_reg));
    m->mac_valid_reg = 0;
}

static inline void mac_array_step(mac_array_t *m,
                                   const int16_t win_in[3][9],
                                   const int16_t weight_in[3][9],
                                   const uint8_t win_valid[3],
                                   int32_t num_active_ch) {
    /* ---- 9개 mac_unit 각자 step (자기 레지스터 기준 출력 계산 + 갱신) ---- */
    int64_t row_out[3][3];
    uint8_t row_valid_out[3][3];
    for (int ch = 0; ch < 3; ch++) {
        for (int row = 0; row < 3; row++) {
            int16_t wr[3] = { win_in[ch][row*3 + 0], win_in[ch][row*3 + 1], win_in[ch][row*3 + 2] };
            int16_t gr[3] = { weight_in[ch][row*3 + 0], weight_in[ch][row*3 + 1], weight_in[ch][row*3 + 2] };
            mac_unit_step(&m->mu[ch][row], wr, gr, win_valid[ch], &row_out[ch][row], &row_valid_out[ch][row]);
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

    /* ---- "동시에" 갱신 (논블로킹 대입 순서 재현) ---- */
    for (int ch = 0; ch < 3; ch++) m->ch_result_reg[ch] = final_sum[ch];
    m->mac_valid_reg = next_mac_valid;

    for (int ch = 0; ch < 3; ch++) {
        for (int row = 0; row < 3; row++) m->c_row_reg[ch][row] = row_out[ch][row];
        m->c_valid_reg[ch] = (uint8_t)(row_valid_out[ch][0] & row_valid_out[ch][1] & row_valid_out[ch][2]);
    }
}

static inline int64_t mac_array_ch_result(const mac_array_t *m, int ch) { return m->ch_result_reg[ch]; }
static inline uint8_t mac_array_valid(const mac_array_t *m) { return m->mac_valid_reg; }

#endif /* MAC_ARRAY_H */
