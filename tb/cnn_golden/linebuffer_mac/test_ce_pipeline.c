/*
 * test_ce_pipeline.c
 * ---------------------------------------------------------------
 * line_buffer_array -> mac_array 전체 흐름을 실제로 돌려보고,
 * 단순 3중 for문으로 계산한 conv 결과와 비교해서 골든모델이 맞는지 검증한다.
 *
 * 빌드: gcc -Wall -Wextra -std=c11 -DIMG_WIDTH=6 test_ce_pipeline.c -o test_ce_pipeline
 * 실행: ./test_ce_pipeline
 */

#include <stdio.h>
#include "line_buffer_array.h"
#include "mac_array.h"

#define IMG_HEIGHT 28
#define NUM_CH 3

static int16_t image[NUM_CH][IMG_HEIGHT][IMG_WIDTH];
static int16_t weight[NUM_CH][3][3];

/* 검증용 기준값: 그냥 3중 for문으로 직접 conv 계산 (3채널 합산) */
static int32_t naive_conv_ref(int out_r, int out_c) {
    int32_t sum = 0;
    for (int ch = 0; ch < NUM_CH; ch++)
        for (int r = 0; r < 3; r++)
            for (int c = 0; c < 3; c++)
                sum += (int32_t)image[ch][out_r + r][out_c + c] * weight[ch][r][c];
    return sum;
}

/* 채널 하나만 떼어서 계산한 기준값 (ch_result0/1/2 개별 검증용) */
static int32_t naive_conv_ref_ch(int ch, int out_r, int out_c) {
    int32_t sum = 0;
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++)
            sum += (int32_t)image[ch][out_r + r][out_c + c] * weight[ch][r][c];
    return sum;
}

int main(void) {
    /* 테스트용 데이터: 채널마다 다른 패턴 + weight도 채널마다 다르게 */
    for (int ch = 0; ch < NUM_CH; ch++)
        for (int r = 0; r < IMG_HEIGHT; r++)
            for (int c = 0; c < IMG_WIDTH; c++)
                image[ch][r][c] = (int16_t)(((ch + 1) * 7 + r * IMG_WIDTH + c) % 40);

    for (int ch = 0; ch < NUM_CH; ch++)
        for (int r = 0; r < 3; r++)
            for (int c = 0; c < 3; c++)
                weight[ch][r][c] = (int16_t)(ch + r - c); /* 음수도 섞이게 */

    int16_t weight_in[3][9];
    for (int ch = 0; ch < 3; ch++)
        for (int r = 0; r < 3; r++)
            for (int c = 0; c < 3; c++)
                weight_in[ch][r * 3 + c] = weight[ch][r][c];

    line_buffer_array_t arr;
    line_buffer_array_reset(&arr, 3); /* conv2 시나리오: NUM_BUFFERS=3 */

    int out_count = 0, fail_count = 0;

    /* channel-first: 매 (row,col) 위치마다 채널 0,1,2 순서로 한 클록씩 스트리밍 */
    for (int row = 0; row < IMG_HEIGHT; row++) {
        for (int col = 0; col < IMG_WIDTH; col++) {
            for (int ch = 0; ch < NUM_CH; ch++) {
                line_buffer_array_step(&arr, image[ch][row][col], 1, ch);
            }

            /* 3채널 다 갱신된 뒤(한 사이클 = 채널 3개 클록) 확인 */
            if (arr.win_valid[0] && arr.win_valid[1] && arr.win_valid[2]) {
                mac_array_result_t mr = mac_array_eval(arr.win_out, weight_in, arr.win_valid, 3);
                if (!mr.mac_valid) {
                    fail_count++;
                    printf("[FAIL] mac_valid=0인데 win_valid 3개 다 1 (row=%d col=%d)\n", row, col);
                }
                if (mr.mac_valid) {
                    int32_t got = mr.ch_result0 + mr.ch_result1 + mr.ch_result2;
                    int out_r = row - 2, out_c = col - 2;
                    int32_t want = naive_conv_ref(out_r, out_c);
                    out_count++;
                    if (got != want) {
                        fail_count++;
                        printf("[FAIL] out(%d,%d): got=%d want=%d "
                               "(ch0=%d ch1=%d ch2=%d)\n",
                               out_r, out_c, got, want,
                               mr.ch_result0, mr.ch_result1, mr.ch_result2);
                    }

                    /* 채널별 개별 검증 - 총합만 맞고 채널끼리 뒤바뀐 경우까지 잡아냄 */
                    int32_t want0 = naive_conv_ref_ch(0, out_r, out_c);
                    int32_t want1 = naive_conv_ref_ch(1, out_r, out_c);
                    int32_t want2 = naive_conv_ref_ch(2, out_r, out_c);
                    if (mr.ch_result0 != want0) {
                        fail_count++;
                        printf("[FAIL-ch0] out(%d,%d): got=%d want=%d\n", out_r, out_c, mr.ch_result0, want0);
                    }
                    if (mr.ch_result1 != want1) {
                        fail_count++;
                        printf("[FAIL-ch1] out(%d,%d): got=%d want=%d\n", out_r, out_c, mr.ch_result1, want1);
                    }
                    if (mr.ch_result2 != want2) {
                        fail_count++;
                        printf("[FAIL-ch2] out(%d,%d): got=%d want=%d\n", out_r, out_c, mr.ch_result2, want2);
                    }
                }
            }
        }
    }

    printf("검증한 출력 픽셀 수: %d, 실패: %d\n", out_count, fail_count);
    printf("기대되는 출력 픽셀 수: %d (IMG_WIDTH=%d, IMG_HEIGHT=%d 기준)\n",
           (IMG_WIDTH - 2) * (IMG_HEIGHT - 2), IMG_WIDTH, IMG_HEIGHT);

    if (fail_count == 0 && out_count == (IMG_WIDTH - 2) * (IMG_HEIGHT - 2)) {
        printf("=> PASS: 골든모델이 순수 conv 계산과 정확히 일치\n");
        return 0;
    } else {
        printf("=> FAIL\n");
        return 1;
    }
}
