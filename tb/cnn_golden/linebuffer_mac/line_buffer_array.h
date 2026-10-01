/*
 * line_buffer_array.h
 * ---------------------------------------------------------------
 * line_buffer_array 상위 모듈 (담당: 조성재)
 *
 * ** RTL 병렬 입력 구조 반영 (기존 ch_sel 라운드로빈 방식에서 변경됨) **
 *   - ch_sel 없음. pixel_in0/1/2 각각 전용 포트로 병렬 입력
 *   - pixel_valid 하나가 3개 인스턴스에 그대로 broadcast (= wr_en)
 *   - 더 이상 "몇 번째 채널 차례인지" 시분할할 필요 없음
 *   - NUM_BUFFERS 파라미터도 제거됨 -> 항상 3개 인스턴스 다 동작,
 *     conv1은 pixel_in1/2에 아무 값이나 넣고 mac_array의
 *     NUM_ACTIVE_CH=1로 그 결과를 무시하는 방식으로 처리
 *
 * 인터페이스 명세서 매핑:
 *   in : pixel_in0/1/2[15:0], pixel_valid, phase_clear
 *   out: win_out[431:0] (win_out[3][144]로 표현), win_valid[2:0]
 */

#ifndef LINE_BUFFER_ARRAY_H
#define LINE_BUFFER_ARRAY_H

#include "line_buffer.h"

#define MAX_BUFFERS 3   /* 물리 line_buffer 개수 (고정 3) */

typedef struct {
    line_buffer_t lb[MAX_BUFFERS];

    int16_t win_out[MAX_BUFFERS][KSIZE * KSIZE]; /* win_out[431:0]에 대응 */
    uint8_t win_valid[MAX_BUFFERS];              /* win_valid[2:0]에 대응 */
} line_buffer_array_t;

static inline void line_buffer_array_reset(line_buffer_array_t *arr) {
    for (int i = 0; i < MAX_BUFFERS; i++) {
        line_buffer_reset(&arr->lb[i]);
        memset(arr->win_out[i], 0, sizeof(arr->win_out[i]));
        arr->win_valid[i] = 0;
    }
}

/*
 * 클록 1번의 동작. pixel_in0/1/2를 각자 전용 인스턴스로 병렬 전달하고,
 * pixel_valid를 그대로 wr_en으로 broadcast. RTL line_buffer_array.v의
 * "wire wr_en = pixel_valid;" 를 그대로 옮긴 것.
 */
static inline void line_buffer_array_step(line_buffer_array_t *arr,
                                           int16_t pixel_in0, int16_t pixel_in1, int16_t pixel_in2,
                                           uint8_t pixel_valid, uint8_t phase_clear) {
    int16_t pixel_in[MAX_BUFFERS] = { pixel_in0, pixel_in1, pixel_in2 };
    for (int i = 0; i < MAX_BUFFERS; i++) {
        line_buffer_step(&arr->lb[i], pixel_in[i], pixel_valid, phase_clear);
        memcpy(arr->win_out[i], arr->lb[i].win_out, sizeof(arr->win_out[i]));
        arr->win_valid[i] = arr->lb[i].win_valid;
    }
}

#endif /* LINE_BUFFER_ARRAY_H */
