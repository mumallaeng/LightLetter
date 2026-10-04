/*
 * test_phase_clear.c
 * ---------------------------------------------------------------
 * 병렬 입력 구조(line_buffer_array.h v2)에 맞춰 갱신된 phase_clear 검증.
 *
 * 빌드: gcc -Wall -Wextra -std=c11 -DIMG_WIDTH=6 test_phase_clear.c -o test_phase_clear
 */

#include <stdio.h>
#include "line_buffer_array.h"

int main(void) {
    line_buffer_array_t arr;
    line_buffer_array_reset(&arr);

    int fail = 0;
    int saw_valid_before_clear = 0;

    /* Phase1: 3줄치 픽셀 흘려보냄 (3채널 다 동시에) */
    for (int i = 0; i < IMG_WIDTH * 3; i++) {
        line_buffer_array_step(&arr, (int16_t)(0xAA + i), (int16_t)(0xBB + i), (int16_t)(0xCC + i), 1, 0);
        if (arr.win_valid[0] && arr.win_valid[1] && arr.win_valid[2]) saw_valid_before_clear = 1;
    }

    if (!saw_valid_before_clear) {
        printf("[FAIL] Phase1에서 win_valid가 한 번도 안 떴음\n");
        fail = 1;
    } else {
        printf("[OK] Phase1: win_valid 정상적으로 뜸\n");
    }

    /* Phase 전환: phase_clear 1클록 펄스 (pixel_valid=0이어야 함) */
    line_buffer_array_step(&arr, 0, 0, 0, 0, 1);

    /* Phase2: 새 데이터 2클록만 (아직 warm-up 안 끝났어야 함) */
    for (int i = 0; i < 2; i++) {
        line_buffer_array_step(&arr, (int16_t)(0x55 + i), (int16_t)(0x66 + i), (int16_t)(0x77 + i), 1, 0);
        if (arr.win_valid[0] || arr.win_valid[1] || arr.win_valid[2]) {
            printf("[FAIL] phase_clear 직후 warm-up도 안 끝났는데 win_valid=1 떠버림! (클록 %d)\n", i);
            fail = 1;
        }
    }

    printf(fail ? "=> FAIL\n" : "=> PASS: phase_clear 이후 오염 없이 정상적으로 재시작됨\n");
    return fail;
}
