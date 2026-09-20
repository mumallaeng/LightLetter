/*
 * test_final_pipeline.c
 * ---------------------------------------------------------------
 * 최종 확정된 RTL 구조(line_buffer.v v4 + line_buffer_array.v 병렬 3포트
 * + MAC_unit.v psum_in 없음 + MAC_array.v 2단 FF 파이프라인)에 맞춰
 * 업데이트된 골든모델 전체를 실제 이미지 데이터로 검증한다.
 *
 * ** 중요: mac_array와 line_buffer_array를 연결하는 순서 **
 * RTL에서는 두 모듈이 같은 클록 엣지에서 각자 논블로킹 대입을 하기 때문에,
 * mac_array의 FF#1은 line_buffer_array가 "이번 클록에 갱신하기 직전" 값을
 * 본다 (서로 다른 모듈 간 동시 엣지 특성). 그래서 매 클록:
 *   1) mac_array_step()을 먼저 호출 (line_buffer_array의 *이전* 상태 사용)
 *   2) 그다음 line_buffer_array_step()을 호출 (이번 클록 픽셀 반영)
 * 이 순서를 반대로 하면 레이턴시가 1클록 어긋난다 (실제로 겪었던 버그).
 *
 * 빌드: gcc -Wall -Wextra -std=c11 test_final_pipeline.c -o test_final_pipeline
 */

#include <stdio.h>
#include "line_buffer_array.h"
#include "mac_array.h"

#define IMG_HEIGHT 28
#define NUM_CH 3

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
    /* mac_valid가 뜬 클록에서, 실제로는 "2클록 전에 완성된 창"을 보고 있는 것이므로
     * out_r/out_c도 그만큼 과거 위치를 가리켜야 함 -> 위치 추적용 지연 큐 사용 */
    int pos_row[3], pos_col[3]; /* 최근 3클록의 (row,col), [0]=이번클록 */
    int pos_valid_count = 0;

    for (int row = 0; row < IMG_HEIGHT; row++) {
        for (int col = 0; col < IMG_WIDTH; col++) {
            /* 1) mac_array 먼저 (line_buffer_array의 이전 상태로) */
            mac_array_step(&mac, arr.win_out, weight_in, arr.win_valid, 3);

            /* 이 mac_valid는 "2클록 전에 완성된 창"을 반영 */
            if (mac_array_valid(&mac) && pos_valid_count >= 2) {
                int out_r = pos_row[1] - 2, out_c = pos_col[1] - 2;
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

            /* 2) line_buffer_array 갱신 (이번 클록 픽셀 반영) */
            line_buffer_array_step(&arr, image[0][row][col], image[1][row][col], image[2][row][col], 1, 0);

            /* 위치 지연 큐 갱신 */
            pos_row[2] = pos_row[1]; pos_col[2] = pos_col[1];
            pos_row[1] = pos_row[0]; pos_col[1] = pos_col[0];
            pos_row[0] = row; pos_col[0] = col;
            if (pos_valid_count < 3) pos_valid_count++;
        }
    }

    /* 파이프라인 drain: 마지막 픽셀 이후 2클록 더 흘려보내야 마지막 2자리가 나옴
     * (line_buffer_array에는 새 픽셀을 안 넣음 -> pixel_valid=0으로 호출해 hold만 시킴) */
    for (int d = 0; d < 2; d++) {
        mac_array_step(&mac, arr.win_out, weight_in, arr.win_valid, 3);
        if (mac_array_valid(&mac) && pos_valid_count >= 2) {
            int out_r = pos_row[1] - 2, out_c = pos_col[1] - 2;
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
        line_buffer_array_step(&arr, 0, 0, 0, 0, 0); /* pixel_valid=0: hold만 */
        pos_row[2] = pos_row[1]; pos_col[2] = pos_col[1];
        pos_row[1] = pos_row[0]; pos_col[1] = pos_col[0];
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
    /* 안 쓰는 채널1,2도 채워둠 (읽지도 않지만 습관적으로 초기화) */
    for (int k = 0; k < 9; k++) { weight_in[1][k] = 0; weight_in[2][k] = 0; }

    line_buffer_array_t arr;
    line_buffer_array_reset(&arr);
    mac_array_t mac;
    mac_array_reset(&mac);

    int out_count = 0, fail_count = 0;
    int pos_row[3], pos_col[3];
    int pos_valid_count = 0;

    for (int row = 0; row < IMG_HEIGHT; row++) {
        for (int col = 0; col < IMG_WIDTH; col++) {
            mac_array_step(&mac, arr.win_out, weight_in, arr.win_valid, 1 /* NUM_ACTIVE_CH=1 */);

            if (mac_array_valid(&mac) && pos_valid_count >= 2) {
                int out_r = pos_row[1] - 2, out_c = pos_col[1] - 2;
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

            pos_row[2] = pos_row[1]; pos_col[2] = pos_col[1];
            pos_row[1] = pos_row[0]; pos_col[1] = pos_col[0];
            pos_row[0] = row; pos_col[0] = col;
            if (pos_valid_count < 3) pos_valid_count++;
        }
    }

    /* 파이프라인 drain (conv2와 동일 이유) */
    for (int d = 0; d < 2; d++) {
        mac_array_step(&mac, arr.win_out, weight_in, arr.win_valid, 1);
        if (mac_array_valid(&mac) && pos_valid_count >= 2) {
            int out_r = pos_row[1] - 2, out_c = pos_col[1] - 2;
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
        pos_row[2] = pos_row[1]; pos_col[2] = pos_col[1];
        pos_row[1] = pos_row[0]; pos_col[1] = pos_col[0];
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
