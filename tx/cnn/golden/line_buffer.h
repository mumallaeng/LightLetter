/*
 * line_buffer.h
 * ---------------------------------------------------------------
 * line_buffer 단위 모듈 (담당: 조성재)
 *
 * 설계 원칙:
 *   - 3줄(row0~row2) 전체 저장 (k-1 최적화 대신 단순 구조로 확정)
 *   - 16bit 고정소수점 (float 미사용)
 *   - RTL 매핑을 명확히 하기 위해 두 단계로 분리:
 *       1) compute_fresh : always_comb 대응 - "이번 클록에 쓴다면 나올 값"을
 *          wr_en과 무관하게 항상 계산 (RTL의 win_out_fresh와 동일하게,
 *          조합 로직은 wr_en을 안 봄 - wr_en 게이팅은 순차 로직에서만)
 *       2) update_regs   : always_ff 대응 - 3갈래 분기 (RTL과 동일)
 *            - !rst_n || phase_clear : win_out/win_valid/카운터 전부 0
 *            - wr_en                 : fresh 값을 그대로 커밋 + 카운터 진행
 *            - else(wr_en=0)         : win_valid만 0 (win_out은 그대로 유지)
 *              -> MAC_unit이 무상태(매 클록 row_valid 보고 즉시 0/계산)라서,
 *                 line_buffer도 "새 윈도우 없음"을 즉시 알려야 파이프라인
 *                 전체에서 같은 결과가 중복 전파되는 걸 막을 수 있음
 *                 (실제 RTL 시뮬레이션으로 검증된 이유)
 *
 * 인터페이스 명세서 매핑:
 *   in : pixel_in[15:0], wr_en, phase_clear
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

    /* ---- 출력 (이제 진짜 레지스터, always_ff에서만 갱신됨) ---- */
    int16_t win_out[KSIZE * KSIZE];    /* 3x3 윈도우, row-major flatten */
    uint8_t win_valid;
} line_buffer_t;

/* WBS 2.2: rst_n 처리 */
static inline void line_buffer_reset(line_buffer_t *lb) {
    memset(lb, 0, sizeof(*lb));
}

/*
 * always_comb에 대응 - "이번 클록에 쓴다면 나올 값"을 계산만 함, 레지스터 불변.
 * RTL의 win_out_fresh 블록과 동일하게, wr_en을 아예 보지 않는다
 * (wr_en 게이팅은 이 함수를 부르는 쪽이 아니라 update_regs 안에서만 함).
 */
static inline void line_buffer_compute_fresh(const line_buffer_t *lb, int16_t pixel_in,
                                              int16_t win_out_fresh[KSIZE * KSIZE],
                                              uint8_t *win_valid_fresh) {
    *win_valid_fresh = 0;
    memset(win_out_fresh, 0, KSIZE * KSIZE * sizeof(int16_t));

    if (lb->write_row_num >= (KSIZE - 1) && lb->write_col >= (KSIZE - 1)) {
        int32_t r0 = (lb->write_row_num - 2) % KSIZE;
        int32_t r1 = (lb->write_row_num - 1) % KSIZE;
        int32_t r2 = (lb->write_row_num - 0) % KSIZE;
        int32_t c0 = lb->write_col - 2;
        int32_t c1 = lb->write_col - 1;
        int32_t c2 = lb->write_col;

        win_out_fresh[0] = lb->row_buf[r0][c0];
        win_out_fresh[1] = lb->row_buf[r0][c1];
        win_out_fresh[2] = lb->row_buf[r0][c2];
        win_out_fresh[3] = lb->row_buf[r1][c0];
        win_out_fresh[4] = lb->row_buf[r1][c1];
        win_out_fresh[5] = lb->row_buf[r1][c2];
        win_out_fresh[6] = lb->row_buf[r2][c0];
        win_out_fresh[7] = lb->row_buf[r2][c1];
        /* row2,c2 자리는 아직 row_buf에 안 쓰여있는 "이번 클록 픽셀" 그 자체이므로
         * 레지스터를 안 읽고 pixel_in을 직접 우회(bypass)시켜 씀 */
        win_out_fresh[8] = pixel_in;

        *win_valid_fresh = 1;
    }
}

/*
 * always_ff에 대응 - RTL의 3갈래 분기를 그대로 재현.
 * 반드시 compute_fresh로 이번 클록 fresh 값을 먼저 구한 다음 호출해야 함.
 */
static inline void line_buffer_update_regs(line_buffer_t *lb, int16_t pixel_in, uint8_t wr_en,
                                            uint8_t phase_clear,
                                            const int16_t win_out_fresh[KSIZE * KSIZE],
                                            uint8_t win_valid_fresh) {
    if (phase_clear) {
        lb->write_row_num = 0;
        lb->write_col = 0;
        memset(lb->win_out, 0, sizeof(lb->win_out));
        lb->win_valid = 0;
        return;
    }

    if (wr_en) {
        memcpy(lb->win_out, win_out_fresh, sizeof(lb->win_out));
        lb->win_valid = win_valid_fresh;

        lb->row_buf[lb->write_row_num % KSIZE][lb->write_col] = pixel_in;

        lb->write_col++;
        if (lb->write_col == IMG_WIDTH) {
            lb->write_col = 0;
            lb->write_row_num++;
        }
    } else {
        /* 새로운 window가 없으므로 valid를 0으로 만듦 (win_out은 그대로 유지) */
        lb->win_valid = 0;
    }
}

/* WBS 2.3: 한 클록 전체 동작. 위 두 단계를 올바른 순서로 묶어서 호출한다. */
static inline void line_buffer_step(line_buffer_t *lb, int16_t pixel_in, uint8_t wr_en, uint8_t phase_clear) {
    int16_t win_out_fresh[KSIZE * KSIZE];
    uint8_t win_valid_fresh;
    line_buffer_compute_fresh(lb, pixel_in, win_out_fresh, &win_valid_fresh); /* always_comb */
    line_buffer_update_regs(lb, pixel_in, wr_en, phase_clear, win_out_fresh, win_valid_fresh); /* always_ff */
}

#endif /* LINE_BUFFER_H */
