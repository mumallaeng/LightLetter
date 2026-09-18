/*
 * line_buffer.h
 * ---------------------------------------------------------------
 * line_buffer 단위 모듈 (담당: 조성재)
 *
 * 설계 원칙:
 *   - 3줄(row0~row2) 전체 저장 (k-1 최적화 대신 단순 구조로 확정)
 *   - 16bit 고정소수점 (float 미사용)
 *   - RTL 매핑을 명확히 하기 위해 두 단계로 분리:
 *       1) compute_output : always_comb 대응 - 레지스터를 "읽기만" 함,
 *          단 방금 들어온 pixel_in은 레지스터를 거치지 않고 직접 우회(bypass)
 *       2) update_regs    : always_ff 대응 - 레지스터에 "쓰기만" 함
 *     이렇게 나눠두면, RTL로 옮길 때 순서를 고민할 필요 없이
 *     compute_output 안의 조합식을 always_comb에, update_regs 안의
 *     대입을 always_ff(<=)에 그대로 옮기기만 하면 됨.
 *
 * 인터페이스 명세서 매핑:
 *   in : pixel_in[15:0], wr_en
 *   out: win_out[143:0] (9 x 16bit), win_valid
 */

#ifndef LINE_BUFFER_H
#define LINE_BUFFER_H

#include <stdint.h>
#include <string.h>

#ifndef IMG_WIDTH
#define IMG_WIDTH 28   /* 대상 레이어의 입력 가로 크기로 교체해서 쓰면 됨 */
#endif

#define KSIZE 3        /* 3x3 커널 고정 */

typedef struct {
    /* ---- 레지스터 (RTL 레지스터에 1:1 대응, always_ff에서만 갱신됨) ---- */
    int16_t row_buf[KSIZE][IMG_WIDTH]; /* row0~row2 전체 저장 (원형 재사용) */
    int32_t write_row_num;             /* 지금까지 시작된 행의 개수(0-index) */
    int32_t write_col;                 /* 현재 채우고 있는 행에서의 열 위치 */

    /* ---- 출력 (always_comb에서만 계산됨, 레지스터 아님) ---- */
    int16_t win_out[KSIZE * KSIZE];    /* 3x3 윈도우, row-major flatten */
    uint8_t win_valid;
} line_buffer_t;

/* WBS 2.2: rst_n 처리 */
static inline void line_buffer_reset(line_buffer_t *lb) {
    memset(lb, 0, sizeof(*lb));
}

/*
 * always_comb에 대응 - 순수 조합 로직, 레지스터를 바꾸지 않는다.
 * 이번 클록에 들어온 pixel_in을 "쓰기 전"에 미리 윈도우 계산에 반영한다
 * (row_buf에 썼다가 같은 클록에 다시 읽는 게 아니라, pixel_in을 직접 우회시킴).
 */
static inline void line_buffer_compute_output(line_buffer_t *lb, int16_t pixel_in, uint8_t wr_en) {
    if (!wr_en) return; /* wr_en=0이면 출력도 그대로 유지 (레지스터 안 바뀌니 당연히) */

    lb->win_valid = 0;
    if (lb->write_row_num >= (KSIZE - 1) && lb->write_col >= (KSIZE - 1)) {
        int32_t r0 = (lb->write_row_num - 2) % KSIZE;
        int32_t r1 = (lb->write_row_num - 1) % KSIZE;
        int32_t r2 = (lb->write_row_num - 0) % KSIZE;
        int32_t c0 = lb->write_col - 2;
        int32_t c1 = lb->write_col - 1;
        int32_t c2 = lb->write_col; /* <- 이 자리가 "방금 들어온 픽셀"의 위치 */

        lb->win_out[0] = lb->row_buf[r0][c0];
        lb->win_out[1] = lb->row_buf[r0][c1];
        lb->win_out[2] = lb->row_buf[r0][c2];
        lb->win_out[3] = lb->row_buf[r1][c0];
        lb->win_out[4] = lb->row_buf[r1][c1];
        lb->win_out[5] = lb->row_buf[r1][c2];
        lb->win_out[6] = lb->row_buf[r2][c0];
        lb->win_out[7] = lb->row_buf[r2][c1];
        /* row2, c2 자리는 아직 row_buf에 안 쓰여있는 "이번 클록 픽셀" 그 자체이므로
         * 레지스터를 안 읽고 pixel_in을 직접 우회(bypass)시켜 씀 */
        lb->win_out[8] = pixel_in;

        lb->win_valid = 1;
    }
}

/*
 * always_ff에 대응 - 레지스터 쓰기만 담당, 출력 계산은 절대 안 함.
 * 반드시 line_buffer_compute_output을 먼저 호출한 "다음"에 불러야
 * (같은 클록 기준으로) RTL과 동일한 순서가 됨.
 */
static inline void line_buffer_update_regs(line_buffer_t *lb, int16_t pixel_in, uint8_t wr_en) {
    if (!wr_en) return;

    lb->row_buf[lb->write_row_num % KSIZE][lb->write_col] = pixel_in;

    lb->write_col++;
    if (lb->write_col == IMG_WIDTH) {
        lb->write_col = 0;
        lb->write_row_num++;
    }
}

/* WBS 2.3: 한 클록 전체 동작. 위 두 단계를 올바른 순서로 묶어서 호출한다. */
static inline void line_buffer_step(line_buffer_t *lb, int16_t pixel_in, uint8_t wr_en) {
    line_buffer_compute_output(lb, pixel_in, wr_en); /* always_comb: 이번 클록 결과 계산 */
    line_buffer_update_regs(lb, pixel_in, wr_en);    /* always_ff  : 다음 클록을 위한 레지스터 갱신 */
}

#endif /* LINE_BUFFER_H */
