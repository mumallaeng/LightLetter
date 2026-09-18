/*
 * line_buffer_array.h
 * ---------------------------------------------------------------
 * line_buffer_array 상위 모듈 (담당: 조성재)
 *
 * 설계 원칙:
 *   - line_buffer를 NUM_BUFFERS개(conv1=1, conv2=3) 인스턴스화
 *   - wr_en 포트를 따로 안 받고, pixel_valid + ch_sel로 내부에서 생성
 *       wr_en[i] = pixel_valid AND (ch_sel == i)
 *   - mux 없음 -> 각 인스턴스의 win_out/win_valid를 그대로 배열로 노출
 *
 * 인터페이스 명세서 매핑:
 *   in : pixel_in[15:0], pixel_valid, ch_sel[1:0]
 *   out: win_out[431:0] (win_out[3][144]로 표현), win_valid[2:0]
 */

#ifndef LINE_BUFFER_ARRAY_H
#define LINE_BUFFER_ARRAY_H

#include "line_buffer.h"

#define MAX_BUFFERS 3   /* 물리 line_buffer 최대 개수 (고정) */

typedef struct {
    int32_t num_buffers;           /* NUM_BUFFERS 파라미터: conv1=1, conv2=3 */
    line_buffer_t lb[MAX_BUFFERS]; /* 단위 모듈 인스턴스들 */

    /* ---- 상위 모듈 출력 (mux 없이 인스턴스별로 그대로) ---- */
    int16_t win_out[MAX_BUFFERS][KSIZE * KSIZE]; /* win_out[431:0]에 대응 */
    uint8_t win_valid[MAX_BUFFERS];              /* win_valid[2:0]에 대응 */
} line_buffer_array_t;

static inline void line_buffer_array_reset(line_buffer_array_t *arr, int32_t num_buffers) {
    arr->num_buffers = num_buffers;
    for (int i = 0; i < MAX_BUFFERS; i++) {
        line_buffer_reset(&arr->lb[i]);
        memset(arr->win_out[i], 0, sizeof(arr->win_out[i]));
        arr->win_valid[i] = 0;
    }
}

/*
 * 클록 1번의 동작. pixel_in/pixel_valid/ch_sel을 받아
 * 해당 인스턴스에만 내부 wr_en을 assert하고, 결과를 그대로 배열에 반영한다.
 */
static inline void line_buffer_array_step(line_buffer_array_t *arr,
                                           int16_t pixel_in, uint8_t pixel_valid,
                                           int32_t ch_sel) {
    for (int i = 0; i < arr->num_buffers; i++) {
        uint8_t wr_en = (uint8_t)(pixel_valid && (ch_sel == i));
        line_buffer_step(&arr->lb[i], pixel_in, wr_en);
        memcpy(arr->win_out[i], arr->lb[i].win_out, sizeof(arr->win_out[i]));
        arr->win_valid[i] = arr->lb[i].win_valid;
    }
}

#endif /* LINE_BUFFER_ARRAY_H */
