#include <stdio.h>
#include "line_buffer_array.h"
#include "mac_array.h"

#define IMG_HEIGHT 6

static int16_t image[IMG_HEIGHT][IMG_WIDTH];
static int16_t weight[3][3];

static int32_t naive_ref(int out_r, int out_c) {
    int32_t sum = 0;
    for (int r=0;r<3;r++)
        for (int c=0;c<3;c++)
            sum += (int32_t)image[out_r+r][out_c+c]*weight[r][c];
    return sum;
}

int main(void) {
    for (int r=0;r<IMG_HEIGHT;r++)
        for (int c=0;c<IMG_WIDTH;c++)
            image[r][c] = (int16_t)((r*IMG_WIDTH+c) % 30 - 10);
    for (int r=0;r<3;r++)
        for (int c=0;c<3;c++)
            weight[r][c] = (int16_t)(r*3+c-4);

    int16_t weight_in[3][9];
    for (int r=0;r<3;r++)
        for (int c=0;c<3;c++)
            weight_in[0][r*3+c] = weight[r][c];

    line_buffer_array_t arr;
    line_buffer_array_reset(&arr, 1);  /* conv1: NUM_BUFFERS=1 */

    int out_count=0, fail=0, mac_valid_count=0;
    for (int row=0; row<IMG_HEIGHT; row++) {
        for (int col=0; col<IMG_WIDTH; col++) {
            line_buffer_array_step(&arr, image[row][col], 1, 0);
            if (arr.win_valid[0]) {
                uint8_t wv[3] = {arr.win_valid[0], 0, 0};
                mac_array_result_t mr = mac_array_eval(arr.win_out, weight_in, wv, 1); /* num_active_ch=1 */
                if (mr.mac_valid) mac_valid_count++;
                if (!mr.mac_valid) {
                    fail++;
                    printf("[FAIL] mac_valid=0 (row=%d col=%d) - 고쳤는데도 여전히 문제\n", row, col);
                    continue;
                }
                int out_r=row-2, out_c=col-2;
                int32_t want = naive_ref(out_r,out_c);
                out_count++;
                if (mr.ch_result0 != want) {
                    fail++;
                    printf("[FAIL] out(%d,%d): got=%d want=%d\n", out_r,out_c, mr.ch_result0, want);
                }
            }
        }
    }
    printf("conv1(NUM_BUFFERS=1, num_active_ch=1) 검증: %d개 중 실패 %d개\n", out_count, fail);
    printf("mac_valid=1로 뜬 횟수: %d (win_valid=1이었던 횟수와 같아야 정상)\n", mac_valid_count);
    return fail?1:0;
}
