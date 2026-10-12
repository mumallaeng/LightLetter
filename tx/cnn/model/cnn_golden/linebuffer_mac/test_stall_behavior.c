/*
 * test_stall_behavior.c
 * ---------------------------------------------------------------
 * RTL(line_buffer.v, wr_en=0일 때 win_valid<=0 추가된 버전)에서
 * 직접 시뮬레이션했던 것과 "정확히 같은 입력"으로 C 골든모델을 돌려서
 * 클록별로 대조한다.
 *
 * RTL 실측값(IMG_WIDTH=6, i=15,16에서 wr_en=0으로 stall):
 *   i=14 wr_en=1 win_valid=1 win_out_last16=100e
 *   i=15 wr_en=0 win_valid=0 win_out_last16=100e   <- win_valid만 0, win_out은 유지
 *   i=16 wr_en=0 win_valid=0 win_out_last16=100e
 *   i=17 wr_en=1 win_valid=1 win_out_last16=1011
 *
 * 빌드: gcc -Wall -Wextra -std=c11 -DIMG_WIDTH=6 test_stall_behavior.c -o test_stall_behavior
 */

#include <stdio.h>
#include "line_buffer.h"

int main(void) {
    line_buffer_t lb;
    line_buffer_reset(&lb);

    int fail = 0;

    for (int i = 0; i < 18; i++) {
        uint8_t wr_en = (i == 15 || i == 16) ? 0 : 1;
        int16_t pixel = (int16_t)(0x1000 + i);

        line_buffer_step(&lb, pixel, wr_en, 0);

        printf("i=%2d wr_en=%d win_valid=%d win_out[8]=%04x\n",
               i, wr_en, lb.win_valid, (uint16_t)lb.win_out[8]);

        /* RTL 실측값과 핵심 지점 교차검증 */
        if (i == 14 && !(lb.win_valid == 1 && (uint16_t)lb.win_out[8] == 0x100e)) { fail = 1; printf("  [FAIL] i=14 불일치\n"); }
        if (i == 15 && !(lb.win_valid == 0 && (uint16_t)lb.win_out[8] == 0x100e)) { fail = 1; printf("  [FAIL] i=15 불일치 (win_valid는 0, win_out은 유지돼야 함)\n"); }
        if (i == 16 && !(lb.win_valid == 0 && (uint16_t)lb.win_out[8] == 0x100e)) { fail = 1; printf("  [FAIL] i=16 불일치\n"); }
        if (i == 17 && !(lb.win_valid == 1 && (uint16_t)lb.win_out[8] == 0x1011)) { fail = 1; printf("  [FAIL] i=17 불일치\n"); }
    }

    printf(fail ? "=> FAIL\n" : "=> PASS: RTL stall 동작과 클록 단위로 정확히 일치\n");
    return fail;
}
