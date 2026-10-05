/*
 * line_buffer_rtl_1to1.h
 * ---------------------------------------------------------------
 * RTL line_buffer.v와 내부 상태/갱신 순서를 1:1로 대응시킨 C 골든모델.
 *
 * RTL 대응:
 *   row0_buf/row1_buf/row2_buf <-> row_buf[0]/row_buf[1]/row_buf[2]
 *   cur_row                    <-> cur_row
 *   rows_started               <-> rows_started
 *   write_col                  <-> write_col
 *   win_out                    <-> win_out
 *   win_valid                  <-> win_valid
 *
 * 핵심:
 *   - write_row_num을 제거하고 RTL과 동일한 cur_row(0->1->2->0) 사용
 *   - rows_started는 RTL처럼 0->1->2에서 포화
 *   - write_col은 0..IMG_WIDTH-1까지만 상태로 존재하고 마지막 열 뒤 즉시 0
 *   - phase_clear 시 RTL처럼 row buffer까지 모두 0으로 초기화
 *   - compute_fresh는 RTL always @(*)의 row*_sel 및 win_out_fresh를 그대로 재현
 */

#ifndef LINE_BUFFER_H
#define LINE_BUFFER_H

#include <stdint.h>
#include <string.h>

#ifndef IMG_WIDTH
#define IMG_WIDTH 28
#endif

#define KSIZE 3

typedef struct {
    /* RTL row0_buf/row1_buf/row2_buf */
    int16_t row_buf[KSIZE][IMG_WIDTH];

    /* RTL 카운터/상태와 1:1 대응 */
    uint8_t cur_row;       /* 0 -> 1 -> 2 -> 0 ... */
    uint8_t rows_started;  /* 0 -> 1 -> 2, 이후 2에서 포화 */
    int32_t write_col;     /* 0 .. IMG_WIDTH-1 */

    /* RTL 출력 레지스터 */
    int16_t win_out[KSIZE * KSIZE];
    uint8_t win_valid;
} line_buffer_t;

/* RTL rst_n=0 상태와 동일하게 전체 상태를 0으로 초기화 */
static inline void line_buffer_reset(line_buffer_t *lb) {
    memset(lb, 0, sizeof(*lb));
}

/*
 * RTL의 row0_sel/row1_sel/row2_sel + win_out_fresh always @(*) 대응.
 * 레지스터는 변경하지 않고 이번 클록에서 커밋될 fresh 값만 계산한다.
 */
static inline void line_buffer_compute_fresh(const line_buffer_t *lb,
                                              int16_t pixel_in,
                                              int16_t win_out_fresh[KSIZE * KSIZE],
                                              uint8_t *win_valid_fresh) {
    *win_valid_fresh = 0;
    memset(win_out_fresh, 0, KSIZE * KSIZE * sizeof(int16_t));

    /* RTL row*_sel case(cur_row)와 동일한 물리 buffer 선택 */
    int r0_sel;
    int r1_sel;
    int r2_sel;

    switch (lb->cur_row) {
        case 0:
            r0_sel = 1;
            r1_sel = 2;
            r2_sel = 0;
            break;

        case 1:
            r0_sel = 2;
            r1_sel = 0;
            r2_sel = 1;
            break;

        default: /* cur_row == 2 */
            r0_sel = 0;
            r1_sel = 1;
            r2_sel = 2;
            break;
    }

    /* RTL: if (rows_started == 2'd2 && write_col >= 2) */
    if (lb->rows_started == 2 && lb->write_col >= 2) {
        const int32_t c0 = lb->write_col - 2;
        const int32_t c1 = lb->write_col - 1;
        const int32_t c2 = lb->write_col;

        win_out_fresh[0] = lb->row_buf[r0_sel][c0];
        win_out_fresh[1] = lb->row_buf[r0_sel][c1];
        win_out_fresh[2] = lb->row_buf[r0_sel][c2];

        win_out_fresh[3] = lb->row_buf[r1_sel][c0];
        win_out_fresh[4] = lb->row_buf[r1_sel][c1];
        win_out_fresh[5] = lb->row_buf[r1_sel][c2];

        win_out_fresh[6] = lb->row_buf[r2_sel][c0];
        win_out_fresh[7] = lb->row_buf[r2_sel][c1];

        /* RTL과 동일: 현재 픽셀은 아직 row_buf에 쓰이기 전이므로 직접 bypass */
        win_out_fresh[8] = pixel_in;

        *win_valid_fresh = 1;
    }
}

/*
 * RTL always @(posedge clk or negedge rst_n) 대응.
 * C에서는 reset을 line_buffer_reset()으로 별도 처리하고,
 * 여기서는 phase_clear / wr_en / else 세 분기를 RTL과 같은 순서로 재현한다.
 */
static inline void line_buffer_update_regs(line_buffer_t *lb,
                                            int16_t pixel_in,
                                            uint8_t wr_en,
                                            uint8_t phase_clear,
                                            const int16_t win_out_fresh[KSIZE * KSIZE],
                                            uint8_t win_valid_fresh) {
    if (phase_clear) {
        /* RTL phase_clear 분기와 동일: 카운터 + 3개 row buffer + 출력 초기화 */
        lb->cur_row = 0;
        lb->rows_started = 0;
        lb->write_col = 0;

        memset(lb->row_buf, 0, sizeof(lb->row_buf));
        memset(lb->win_out, 0, sizeof(lb->win_out));
        lb->win_valid = 0;
        return;
    }

    if (wr_en) {
        /* RTL nonblocking assignment의 커밋 결과 */
        memcpy(lb->win_out, win_out_fresh, sizeof(lb->win_out));
        lb->win_valid = win_valid_fresh;

        /* case(cur_row): 현재 물리 row buffer에 pixel 저장 */
        lb->row_buf[lb->cur_row][lb->write_col] = pixel_in;

        /* RTL과 동일: 마지막 열이면 0으로 복귀하고 cur_row 진행 */
        if (lb->write_col == IMG_WIDTH - 1) {
            lb->write_col = 0;
            lb->cur_row = (lb->cur_row == 2) ? 0 : (uint8_t)(lb->cur_row + 1);

            /* RTL과 동일하게 2에서 포화 */
            if (lb->rows_started != 2)
                lb->rows_started++;
        }
        else {
            lb->write_col++;
        }
    }
    else {
        /* RTL else: win_out은 유지하고 win_valid만 0 */
        lb->win_valid = 0;
    }
}

/* 한 클록 전체 동작: always_comb 계산 후 always_ff 커밋 */
static inline void line_buffer_step(line_buffer_t *lb,
                                    int16_t pixel_in,
                                    uint8_t wr_en,
                                    uint8_t phase_clear) {
    int16_t win_out_fresh[KSIZE * KSIZE];
    uint8_t win_valid_fresh;

    line_buffer_compute_fresh(lb, pixel_in, win_out_fresh, &win_valid_fresh);
    line_buffer_update_regs(lb, pixel_in, wr_en, phase_clear,
                            win_out_fresh, win_valid_fresh);
}

#endif /* LINE_BUFFER_H */
