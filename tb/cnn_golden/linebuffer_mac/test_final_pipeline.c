/*
 * test_final_pipeline.c
 * ---------------------------------------------------------------
 * 최종 확정된 RTL 구조에 맞춰 업데이트된 골든모델 전체를 실제 이미지
 * 데이터로 검증한다.
 *
 * ** 전체 파이프라인 레이턴시 (line_buffer_array의 win_valid 기준) **
 *   line_buffer(1클록) + mac_unit 자체(1클록, setup timing 위해 추가됨)
 *   + mac_array FF#1/FF#2(2클록) = 총 4클록
 *   (line_buffer_array의 win_valid=1이 뜬 그 클록부터 mac_valid가 뜨기까지 3클록)
 *
 * ** 중요: mac_array와 line_buffer_array를 연결하는 순서 **
 * RTL에서는 두 모듈이 같은 클록 엣지에서 각자 논블로킹 대입을 하기 때문에,
 * mac_array의 FF#1은 line_buffer_array가 "이번 클록에 갱신하기 직전" 값을
 * 본다. 그래서 매 클록:
 *   1) mac_array_step()을 먼저 호출 (line_buffer_array의 *이전* 상태 사용)
 *   2) 그다음 line_buffer_array_step()을 호출 (이번 클록 픽셀 반영)
 *
 * 빌드: gcc -Wall -Wextra -std=c11 test_final_pipeline.c -o test_final_pipeline
 */

#include <stdio.h>
#include "line_buffer_array.h"
#include "mac_array.h"

#define IMG_HEIGHT 28
#define NUM_CH 3
#define PIPE_DEPTH 4   /* line_buffer(1) + mac_unit(1) + mac_array FF#1/FF#2(2) */

static int16_t image[NUM_CH][IMG_HEIGHT][IMG_WIDTH];
static int16_t weight[NUM_CH][3][3];

static int64_t naive_conv_ref_ch(int ch, int out_r, int out_c) {
    int64_t sum = 0;
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++)
            sum += (int64_t)image[ch][out_r + r][out_c + c] * weight[ch][r][c];
    return sum;
}

/* conv2(3채널) 시나리오: 실제 이미지로 채널별 결과까지 개별 검증 */
static int run_conv2_test(void) {
    for (int ch = 0; ch < NUM_CH; ch++)
        for (int r = 0; r < IMG_HEIGHT; r++)
            for (int c = 0; c < IMG_WIDTH; c++)
                image[ch][r][c] = (int16_t)(((ch + 1) * 7 + r * IMG_WIDTH + c) % 40);

    for (int ch = 0; ch < NUM_CH; ch++)
        for (int r = 0; r < 3; r++)
            for (int c = 0; c < 3; c++)
                weight[ch][r][c] = (int16_t)(ch + r - c);

    int16_t weight_in[3][9];
    for (int ch = 0; ch < 3; ch++)
        for (int r = 0; r < 3; r++)
            for (int c = 0; c < 3; c++)
                weight_in[ch][r * 3 + c] = weight[ch][r][c];

    line_buffer_array_t arr;
    line_buffer_array_reset(&arr);
    mac_array_t mac;
    mac_array_reset(&mac);

    int out_count = 0, fail_count = 0;
    /* 위치 지연 큐: PIPE_DEPTH클록 전 위치를 기억해뒀다가 결과와 매칭 */
    int pos_row[PIPE_DEPTH], pos_col[PIPE_DEPTH];
    int pos_valid_count = 0;

    for (int row = 0; row < IMG_HEIGHT; row++) {
        for (int col = 0; col < IMG_WIDTH; col++) {
            mac_array_step(&mac, arr.win_out, weight_in, arr.win_valid, 3);

            if (mac_array_valid(&mac) && pos_valid_count >= PIPE_DEPTH - 1) {
                int out_r = pos_row[PIPE_DEPTH - 2] - 2, out_c = pos_col[PIPE_DEPTH - 2] - 2;
                if (out_r >= 0 && out_c >= 0) {
                    int64_t got0 = mac_array_ch_result(&mac, 0);
                    int64_t got1 = mac_array_ch_result(&mac, 1);
                    int64_t got2 = mac_array_ch_result(&mac, 2);
                    int64_t want0 = naive_conv_ref_ch(0, out_r, out_c);
                    int64_t want1 = naive_conv_ref_ch(1, out_r, out_c);
                    int64_t want2 = naive_conv_ref_ch(2, out_r, out_c);
                    out_count++;
                    if (got0 != want0 || got1 != want1 || got2 != want2) {
                        fail_count++;
                        printf("[FAIL] out(%d,%d) ch0:%lld/%lld ch1:%lld/%lld ch2:%lld/%lld\n",
                               out_r, out_c,
                               (long long)got0, (long long)want0,
                               (long long)got1, (long long)want1,
                               (long long)got2, (long long)want2);
                    }
                }
            }

            line_buffer_array_step(&arr, image[0][row][col], image[1][row][col], image[2][row][col], 1, 0);

            for (int k = PIPE_DEPTH - 1; k > 0; k--) { pos_row[k] = pos_row[k-1]; pos_col[k] = pos_col[k-1]; }
            pos_row[0] = row; pos_col[0] = col;
            if (pos_valid_count < PIPE_DEPTH) pos_valid_count++;
        }
    }

    /* 파이프라인 drain: 늘어난 만큼(PIPE_DEPTH-2) 더 흘려보내야 마지막 자리들이 나옴 */
    for (int d = 0; d < PIPE_DEPTH - 1; d++) {
        mac_array_step(&mac, arr.win_out, weight_in, arr.win_valid, 3);
        if (mac_array_valid(&mac) && pos_valid_count >= PIPE_DEPTH - 1) {
            int out_r = pos_row[PIPE_DEPTH - 2] - 2, out_c = pos_col[PIPE_DEPTH - 2] - 2;
            if (out_r >= 0 && out_c >= 0) {
                int64_t got0 = mac_array_ch_result(&mac, 0);
                int64_t got1 = mac_array_ch_result(&mac, 1);
                int64_t got2 = mac_array_ch_result(&mac, 2);
                int64_t want0 = naive_conv_ref_ch(0, out_r, out_c);
                int64_t want1 = naive_conv_ref_ch(1, out_r, out_c);
                int64_t want2 = naive_conv_ref_ch(2, out_r, out_c);
                out_count++;
                if (got0 != want0 || got1 != want1 || got2 != want2) {
                    fail_count++;
                    printf("[FAIL-drain] out(%d,%d)\n", out_r, out_c);
                }
            }
        }
        line_buffer_array_step(&arr, 0, 0, 0, 0, 0);
        for (int k = PIPE_DEPTH - 1; k > 0; k--) { pos_row[k] = pos_row[k-1]; pos_col[k] = pos_col[k-1]; }
    }

    printf("[conv2] 검증한 출력 픽셀 수: %d, 실패: %d, 기대치: %d\n",
           out_count, fail_count, (IMG_WIDTH - 2) * (IMG_HEIGHT - 2));
    return (fail_count == 0 && out_count == (IMG_WIDTH - 2) * (IMG_HEIGHT - 2)) ? 0 : 1;
}

/* conv1(1채널) 시나리오: NUM_ACTIVE_CH=1, mac_valid가 정상적으로 뜨는지 + 값 검증 */
static int run_conv1_test(void) {
    for (int r = 0; r < IMG_HEIGHT; r++)
        for (int c = 0; c < IMG_WIDTH; c++)
            image[0][r][c] = (int16_t)((r * IMG_WIDTH + c) % 35 - 10);

    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++)
            weight[0][r][c] = (int16_t)(r * 3 + c - 4);

    int16_t weight_in[3][9];
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++)
            weight_in[0][r * 3 + c] = weight[0][r][c];
    for (int k = 0; k < 9; k++) { weight_in[1][k] = 0; weight_in[2][k] = 0; }

    line_buffer_array_t arr;
    line_buffer_array_reset(&arr);
    mac_array_t mac;
    mac_array_reset(&mac);

    int out_count = 0, fail_count = 0;
    int pos_row[PIPE_DEPTH], pos_col[PIPE_DEPTH];
    int pos_valid_count = 0;

    for (int row = 0; row < IMG_HEIGHT; row++) {
        for (int col = 0; col < IMG_WIDTH; col++) {
            mac_array_step(&mac, arr.win_out, weight_in, arr.win_valid, 1);

            if (mac_array_valid(&mac) && pos_valid_count >= PIPE_DEPTH - 1) {
                int out_r = pos_row[PIPE_DEPTH - 2] - 2, out_c = pos_col[PIPE_DEPTH - 2] - 2;
                if (out_r >= 0 && out_c >= 0) {
                    int64_t got0 = mac_array_ch_result(&mac, 0);
                    int64_t want0 = naive_conv_ref_ch(0, out_r, out_c);
                    out_count++;
                    if (got0 != want0) {
                        fail_count++;
                        printf("[FAIL] conv1 out(%d,%d): got=%lld want=%lld\n",
                               out_r, out_c, (long long)got0, (long long)want0);
                    }
                }
            }

            line_buffer_array_step(&arr, image[0][row][col], 0, 0, 1, 0);

            for (int k = PIPE_DEPTH - 1; k > 0; k--) { pos_row[k] = pos_row[k-1]; pos_col[k] = pos_col[k-1]; }
            pos_row[0] = row; pos_col[0] = col;
            if (pos_valid_count < PIPE_DEPTH) pos_valid_count++;
        }
    }

    for (int d = 0; d < PIPE_DEPTH - 1; d++) {
        mac_array_step(&mac, arr.win_out, weight_in, arr.win_valid, 1);
        if (mac_array_valid(&mac) && pos_valid_count >= PIPE_DEPTH - 1) {
            int out_r = pos_row[PIPE_DEPTH - 2] - 2, out_c = pos_col[PIPE_DEPTH - 2] - 2;
            if (out_r >= 0 && out_c >= 0) {
                int64_t got0 = mac_array_ch_result(&mac, 0);
                int64_t want0 = naive_conv_ref_ch(0, out_r, out_c);
                out_count++;
                if (got0 != want0) {
                    fail_count++;
                    printf("[FAIL-drain] conv1 out(%d,%d)\n", out_r, out_c);
                }
            }
        }
        line_buffer_array_step(&arr, 0, 0, 0, 0, 0);
        for (int k = PIPE_DEPTH - 1; k > 0; k--) { pos_row[k] = pos_row[k-1]; pos_col[k] = pos_col[k-1]; }
    }

    printf("[conv1] 검증한 출력 픽셀 수: %d, 실패: %d, 기대치: %d\n",
           out_count, fail_count, (IMG_WIDTH - 2) * (IMG_HEIGHT - 2));
    return (fail_count == 0 && out_count == (IMG_WIDTH - 2) * (IMG_HEIGHT - 2)) ? 0 : 1;
}

int main(void) {
    int fail = 0;
    fail |= run_conv2_test();
    fail |= run_conv1_test();
    printf(fail ? "=> 전체 FAIL\n" : "=> 전체 PASS\n");
    return fail;
}
