/*
 * test_pipeline_logged.c
 * ---------------------------------------------------------------
 * test_final_pipeline.c와 동일한 conv2 시나리오를 돌리되, CSV가 아니라
 * 사람이 그대로 읽을 수 있는 일반 텍스트(.log) 파일로 클록별 기록을 남긴다.
 * Vivado $display 로그와 형태가 비슷해서, 나중에 RTL 시뮬레이션 로그랑
 * 육안으로 나란히 대조하기 편하게 하려는 목적.
 *
 * 빌드: gcc -Wall -Wextra -std=c11 test_pipeline_logged.c -o test_pipeline_logged
 * 실행: ./test_pipeline_logged
 *   -> 같은 폴더에 golden_log.txt 생성됨
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

int main(void) {
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

    FILE *fp = fopen("golden_log.txt", "w");
    if (!fp) {
        fprintf(stderr, "golden_log.txt 파일을 열 수 없습니다\n");
        return 1;
    }

    fprintf(fp, "=== golden_log.txt : line_buffer_array + mac_array 클록별 기록 ===\n");
    fprintf(fp, "=== 전체 파이프라인 3클록 지연 반영됨 (line_buffer 포함 총 4클록) ===\n\n");

    long clk = 0;
    int out_count = 0, fail_count = 0;

    int pos_row[PIPE_DEPTH] = {0}, pos_col[PIPE_DEPTH] = {0};
    int pos_valid_count = 0;

    for (int row = 0; row < IMG_HEIGHT; row++) {
        for (int col = 0; col < IMG_WIDTH; col++) {
            clk++;

            /* 순서 중요: mac_array 먼저(line_buffer_array의 이전 상태로) */
            mac_array_step(&mac, arr.win_out, weight_in, arr.win_valid, 3);

            uint8_t mv = mac_array_valid(&mac);
            int64_t c0 = mac_array_ch_result(&mac, 0);
            int64_t c1 = mac_array_ch_result(&mac, 1);
            int64_t c2 = mac_array_ch_result(&mac, 2);

            fprintf(fp, "clk=%-5ld row=%-3d col=%-3d win_valid=%d%d%d mac_valid=%d",
                    clk, row, col,
                    arr.win_valid[2], arr.win_valid[1], arr.win_valid[0],
                    mv);

            if (mv) {
                fprintf(fp, "  ch_result0=%-8lld ch_result1=%-8lld ch_result2=%-8lld",
                        (long long)c0, (long long)c1, (long long)c2);

                if (pos_valid_count >= PIPE_DEPTH - 1) {
                    int out_r = pos_row[PIPE_DEPTH - 2] - 2, out_c = pos_col[PIPE_DEPTH - 2] - 2;
                    if (out_r >= 0 && out_c >= 0) {
                        int64_t w0 = naive_conv_ref_ch(0, out_r, out_c);
                        int64_t w1 = naive_conv_ref_ch(1, out_r, out_c);
                        int64_t w2 = naive_conv_ref_ch(2, out_r, out_c);
                        int match = (c0 == w0 && c1 == w1 && c2 == w2);
                        fprintf(fp, "  <- out(%d,%d) %s", out_r, out_c, match ? "OK" : "MISMATCH");
                        out_count++;
                        if (!match) fail_count++;
                    }
                }
            }
            fprintf(fp, "\n");

            line_buffer_array_step(&arr, image[0][row][col], image[1][row][col], image[2][row][col], 1, 0);

            for (int k = PIPE_DEPTH - 1; k > 0; k--) { pos_row[k] = pos_row[k-1]; pos_col[k] = pos_col[k-1]; }
            pos_row[0] = row; pos_col[0] = col;
            if (pos_valid_count < PIPE_DEPTH) pos_valid_count++;
        }
    }

    /* drain: 늘어난 파이프라인 깊이만큼 (PIPE_DEPTH-1클록) */
    for (int d = 0; d < PIPE_DEPTH - 1; d++) {
        clk++;
        mac_array_step(&mac, arr.win_out, weight_in, arr.win_valid, 3);
        uint8_t mv = mac_array_valid(&mac);
        int64_t c0 = mac_array_ch_result(&mac, 0);
        int64_t c1 = mac_array_ch_result(&mac, 1);
        int64_t c2 = mac_array_ch_result(&mac, 2);

        fprintf(fp, "clk=%-5ld (drain)          win_valid=%d%d%d mac_valid=%d",
                clk, arr.win_valid[2], arr.win_valid[1], arr.win_valid[0], mv);
        if (mv) {
            fprintf(fp, "  ch_result0=%-8lld ch_result1=%-8lld ch_result2=%-8lld",
                    (long long)c0, (long long)c1, (long long)c2);
            if (pos_valid_count >= PIPE_DEPTH - 1) {
                int out_r = pos_row[PIPE_DEPTH - 2] - 2, out_c = pos_col[PIPE_DEPTH - 2] - 2;
                if (out_r >= 0 && out_c >= 0) {
                    int64_t w0 = naive_conv_ref_ch(0, out_r, out_c);
                    int64_t w1 = naive_conv_ref_ch(1, out_r, out_c);
                    int64_t w2 = naive_conv_ref_ch(2, out_r, out_c);
                    int match = (c0 == w0 && c1 == w1 && c2 == w2);
                    fprintf(fp, "  <- out(%d,%d) %s", out_r, out_c, match ? "OK" : "MISMATCH");
                    out_count++;
                    if (!match) fail_count++;
                }
            }
        }
        fprintf(fp, "\n");
        line_buffer_array_step(&arr, 0, 0, 0, 0, 0);
        for (int k = PIPE_DEPTH - 1; k > 0; k--) { pos_row[k] = pos_row[k-1]; pos_col[k] = pos_col[k-1]; }
    }

    fprintf(fp, "\n=== 총 %ld클록, 검증 출력 %d개, 실패 %d개 ===\n", clk, out_count, fail_count);
    fclose(fp);

    printf("golden_log.txt 생성 완료 (총 %ld클록)\n", clk);
    printf("검증 출력 %d개, 실패 %d개\n", out_count, fail_count);
    return fail_count ? 1 : 0;
}
