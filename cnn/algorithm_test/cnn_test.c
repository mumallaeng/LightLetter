#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>

#define IN_H        28
#define IN_W        28

#define IN_CH       1
#define OUT_CH      6

#define KERNEL_H    3
#define KERNEL_W    3

#define STRIDE      1

#define OUT_H       ((IN_H - KERNEL_H) / STRIDE + 1)
#define OUT_W       ((IN_W - KERNEL_W) / STRIDE + 1)

/* Layer 2: three line buffers process six input channels in two passes. */
#define L2_IN_H                 OUT_H
#define L2_IN_W                 OUT_W
#define L2_IN_CH                OUT_CH
#define L2_OUT_CH               16
#define L2_CHANNELS_PER_PASS    3
#define L2_NUM_PASSES           (L2_IN_CH / L2_CHANNELS_PER_PASS)
#define L2_OUT_H                ((L2_IN_H - KERNEL_H) / STRIDE + 1)
#define L2_OUT_W                ((L2_IN_W - KERNEL_W) / STRIDE + 1)

#if (L2_IN_CH % L2_CHANNELS_PER_PASS) != 0
#error "L2_IN_CH must be divisible by L2_CHANNELS_PER_PASS"
#endif


/*
 * INT16 quantization
 *
 *   activation : int16_t
 *   weight     : int16_t
 *   bias       : int32_t
 *   acc / psum : int64_t
 *
 * int16 * int16 는 최대 2^30 이므로
 * 9개(layer 1) 또는 54개(layer 2) 항을 더하면 int32 범위를 넘을 수 있음.
 * 따라서 누산기와 출력(requantization 이전 값)은 int64_t 로 둔다.
 */


/*
 * MAC trace
 *
 * hw_style 모델에서 3x3 window 하나와 3x3 weight 커널 하나의 연산이
 * 끝날 때마다 한 줄씩 콘솔과 TRACE_FILE_PATH(CSV)에 기록한다.
 *
 *   step       : 전체 연산 순서 (0부터)
 *   layer      : 1 또는 2
 *   oy, ox     : 출력 좌표
 *   pass       : line buffer pass 번호 (layer 1은 항상 0)
 *   oc, ic     : 출력 / 입력 채널
 *   window     : 3x3 입력 window (열은 공백, 행은 '/' 로 구분)
 *   weight     : 3x3 weight 커널 (같은 형식)
 *   acc_before : 이번 연산 전 누산값 (첫 연산이면 bias)
 *   mac_out    : sum(window * weight)
 *   acc_after  : acc_before + mac_out
 */
#define TRACE_FILE_PATH     "cnn_trace.csv"
#define TRACE_TO_CONSOLE    1

static FILE *trace_fp = NULL;
static uint64_t trace_step = 0;

static void trace_write(const char *line)
{
#if TRACE_TO_CONSOLE
    fputs(line, stdout);
#endif
    if (trace_fp != NULL)
    {
        fputs(line, trace_fp);
    }
}

static int trace_append_3x3(
    char *buf,
    size_t size,
    const int16_t m[KERNEL_H][KERNEL_W]
)
{
    int n = 0;

    for (int ky = 0; ky < KERNEL_H; ky++)
    {
        for (int kx = 0; kx < KERNEL_W; kx++)
        {
            const char *sep =
                (kx > 0) ? " " : (ky > 0) ? "/" : "";

            n += snprintf(buf + n, size - n, "%s%d", sep, m[ky][kx]);
        }
    }

    return n;
}

static void trace_mac(
    int layer, int oy, int ox, int pass, int oc, int ic,
    const int16_t window[KERNEL_H][KERNEL_W],
    const int16_t kernel[KERNEL_H][KERNEL_W],
    int64_t acc_before,
    int64_t mac_out
)
{
    char line[512];
    int n = snprintf(line, sizeof(line),
                     "%" PRIu64 ",%d,%d,%d,%d,%d,%d,",
                     trace_step, layer, oy, ox, pass, oc, ic);

    n += trace_append_3x3(line + n, sizeof(line) - n, window);
    n += snprintf(line + n, sizeof(line) - n, ",");
    n += trace_append_3x3(line + n, sizeof(line) - n, kernel);
    snprintf(line + n, sizeof(line) - n,
             ",%" PRId64 ",%" PRId64 ",%" PRId64 "\n",
             acc_before, mac_out, acc_before + mac_out);

    trace_write(line);
    trace_step++;
}


/*
 * Input:
 *   ifmap[28][28]
 *
 * Weight:
 *   weight[6][3][3]
 *
 * Output:
 *   ofmap[6][26][26]
 */
void conv_1ch_6ch(
    const int16_t ifmap[IN_H][IN_W],
    const int16_t weight[OUT_CH][KERNEL_H][KERNEL_W],
    const int32_t bias[OUT_CH],
    int64_t ofmap[OUT_CH][OUT_H][OUT_W]
)
{
    for (int oc = 0; oc < OUT_CH; oc++)
    {
        for (int oy = 0; oy < OUT_H; oy++)
        {
            for (int ox = 0; ox < OUT_W; ox++)
            {
                int64_t acc = bias[oc];

                for (int ky = 0; ky < KERNEL_H; ky++)
                {
                    for (int kx = 0; kx < KERNEL_W; kx++)
                    {
                        int16_t input_value =
                            ifmap[oy * STRIDE + ky]
                                 [ox * STRIDE + kx];

                        int16_t weight_value =
                            weight[oc][ky][kx];

                        acc +=
                            (int32_t)input_value *
                            (int32_t)weight_value;
                    }
                }

                ofmap[oc][oy][ox] = acc;
            }
        }
    }
}

void conv_1ch_6ch_hw_style(
    const int16_t ifmap[IN_H][IN_W],
    const int16_t weight[OUT_CH][KERNEL_H][KERNEL_W],
    const int32_t bias[OUT_CH],
    int64_t ofmap[OUT_CH][OUT_H][OUT_W]
)
{
    /*
     * 하나의 spatial position마다
     * 3x3 window를 만들고
     * 그 window를 6개 output channel이 공유
     */
    for (int oy = 0; oy < OUT_H; oy++)
    {
        for (int ox = 0; ox < OUT_W; ox++)
        {
            /*
             * 실제 HW에서는 이 부분이
             * 3x3 Line Buffer 출력에 해당
             */
            int16_t window[3][3];

            for (int ky = 0; ky < 3; ky++)
            {
                for (int kx = 0; kx < 3; kx++)
                {
                    window[ky][kx] =
                        ifmap[oy + ky][ox + kx];
                }
            }


            /*
             * 하나의 window를 6개 MAC이 재사용
             */
            for (int oc = 0; oc < OUT_CH; oc++)
            {
                int64_t mac = 0;

                for (int ky = 0; ky < 3; ky++)
                {
                    for (int kx = 0; kx < 3; kx++)
                    {
                        mac +=
                            (int32_t)window[ky][kx]
                            *
                            (int32_t)weight[oc][ky][kx];
                    }
                }

                trace_mac(1, oy, ox, 0, oc, 0,
                          (const int16_t (*)[KERNEL_W])window,
                          weight[oc],
                          bias[oc],
                          mac);

                ofmap[oc][oy][ox] = bias[oc] + mac;
            }
        }
    }
}


/* Straightforward layer-2 model used as the golden reference. */
void conv_6ch_16ch_ref(
    const int16_t ifmap[L2_IN_CH][L2_IN_H][L2_IN_W],
    const int16_t weight[L2_OUT_CH][L2_IN_CH][KERNEL_H][KERNEL_W],
    const int32_t bias[L2_OUT_CH],
    int64_t ofmap[L2_OUT_CH][L2_OUT_H][L2_OUT_W]
)
{
    for (int oc = 0; oc < L2_OUT_CH; oc++)
    {
        for (int oy = 0; oy < L2_OUT_H; oy++)
        {
            for (int ox = 0; ox < L2_OUT_W; ox++)
            {
                int64_t acc = bias[oc];

                for (int ic = 0; ic < L2_IN_CH; ic++)
                {
                    for (int ky = 0; ky < KERNEL_H; ky++)
                    {
                        for (int kx = 0; kx < KERNEL_W; kx++)
                        {
                            acc +=
                                (int32_t)ifmap[ic][oy * STRIDE + ky]
                                                    [ox * STRIDE + kx]
                                *
                                (int32_t)weight[oc][ic][ky][kx];
                        }
                    }
                }

                ofmap[oc][oy][ox] = acc;
            }
        }
    }
}


/*
 * Layer 2 hardware-style model: 6 input channels -> 16 output channels.
 *
 * Line buffer는 3개뿐이므로 입력 이미지를 3채널씩 나눠 받는다.
 *
 * pass 0: 입력 채널 0~2 이미지가 들어옴
 *         프레임 전체를 raster 순서로 훑으며
 *         psum_mem[oc][oy][ox] = bias + sum(ch 0~2)
 * pass 1: pass 0이 모든 위치에서 끝난 뒤 입력 채널 3~5 이미지가 들어옴
 *         psum_mem[oc][oy][ox] += sum(ch 3~5)
 *
 * pass 사이의 중간 누산값은 psum_mem(16 x 24 x 24)에 보관한다.
 * 한 위치의 3개 window는 16개 output channel이 재사용한다.
 * psum_after_pass 는 pass별 psum_mem 상태를 그대로 노출한다.
 */
void conv_6ch_16ch_hw_style(
    const int16_t ifmap[L2_IN_CH][L2_IN_H][L2_IN_W],
    const int16_t weight[L2_OUT_CH][L2_IN_CH][KERNEL_H][KERNEL_W],
    const int32_t bias[L2_OUT_CH],
    int64_t ofmap[L2_OUT_CH][L2_OUT_H][L2_OUT_W],
    int64_t psum_after_pass[L2_NUM_PASSES][L2_OUT_CH][L2_OUT_H][L2_OUT_W]
)
{
    /* pass 사이 누산값을 보관하는 psum 메모리 */
    static int64_t psum_mem[L2_OUT_CH][L2_OUT_H][L2_OUT_W];

    /* 이번 pass에 들어오는 3채널 입력 이미지 (line buffer 3개의 입력) */
    static int16_t pass_in[L2_CHANNELS_PER_PASS][L2_IN_H][L2_IN_W];

    for (int oc = 0; oc < L2_OUT_CH; oc++)
    {
        for (int oy = 0; oy < L2_OUT_H; oy++)
        {
            for (int ox = 0; ox < L2_OUT_W; ox++)
            {
                psum_mem[oc][oy][ox] = bias[oc];
            }
        }
    }

    for (int pass = 0; pass < L2_NUM_PASSES; pass++)
    {
        /* 이번 pass의 입력 채널 이미지만 들어온다. */
        for (int lane = 0; lane < L2_CHANNELS_PER_PASS; lane++)
        {
            int ic = pass * L2_CHANNELS_PER_PASS + lane;

            for (int y = 0; y < L2_IN_H; y++)
            {
                for (int x = 0; x < L2_IN_W; x++)
                {
                    pass_in[lane][y][x] = ifmap[ic][y][x];
                }
            }
        }

        for (int oy = 0; oy < L2_OUT_H; oy++)
        {
            for (int ox = 0; ox < L2_OUT_W; ox++)
            {
                int16_t window[L2_CHANNELS_PER_PASS][KERNEL_H][KERNEL_W];

                /* The three line buffers create three 3x3 windows. */
                for (int lane = 0; lane < L2_CHANNELS_PER_PASS; lane++)
                {
                    for (int ky = 0; ky < KERNEL_H; ky++)
                    {
                        for (int kx = 0; kx < KERNEL_W; kx++)
                        {
                            window[lane][ky][kx] =
                                pass_in[lane][oy * STRIDE + ky]
                                             [ox * STRIDE + kx];
                        }
                    }
                }

                /* Reuse these input windows across all output channels. */
                for (int oc = 0; oc < L2_OUT_CH; oc++)
                {
                    int64_t acc = psum_mem[oc][oy][ox];

                    for (int lane = 0; lane < L2_CHANNELS_PER_PASS; lane++)
                    {
                        int ic = pass * L2_CHANNELS_PER_PASS + lane;
                        int64_t mac = 0;

                        for (int ky = 0; ky < KERNEL_H; ky++)
                        {
                            for (int kx = 0; kx < KERNEL_W; kx++)
                            {
                                mac +=
                                    (int32_t)window[lane][ky][kx]
                                    *
                                    (int32_t)weight[oc][ic][ky][kx];
                            }
                        }

                        trace_mac(2, oy, ox, pass, oc, ic,
                                  (const int16_t (*)[KERNEL_W])window[lane],
                                  weight[oc][ic],
                                  acc,
                                  mac);

                        acc += mac;
                    }

                    psum_mem[oc][oy][ox] = acc;
                    psum_after_pass[pass][oc][oy][ox] = acc;
                }
            }
        }
    }

    for (int oc = 0; oc < L2_OUT_CH; oc++)
    {
        for (int oy = 0; oy < L2_OUT_H; oy++)
        {
            for (int ox = 0; ox < L2_OUT_W; ox++)
            {
                ofmap[oc][oy][ox] = psum_mem[oc][oy][ox];
            }
        }
    }
}


int main(void)
{
    static int16_t ifmap[IN_H][IN_W];
    static int16_t weight[OUT_CH][KERNEL_H][KERNEL_W];
    static int32_t bias[OUT_CH];
    static int64_t ofmap[OUT_CH][OUT_H][OUT_W];

    static int16_t l2_ifmap[L2_IN_CH][L2_IN_H][L2_IN_W];
    static int16_t l2_weight[L2_OUT_CH][L2_IN_CH][KERNEL_H][KERNEL_W];
    static int32_t l2_bias[L2_OUT_CH];
    static int64_t l2_ofmap_ref[L2_OUT_CH][L2_OUT_H][L2_OUT_W];
    static int64_t l2_ofmap_hw[L2_OUT_CH][L2_OUT_H][L2_OUT_W];
    static int64_t l2_psum_after_pass
        [L2_NUM_PASSES][L2_OUT_CH][L2_OUT_H][L2_OUT_W];


    /*
     * Test Input
     *
     * 0, 1, 2, 3, ...
     * 값이 너무 커지는 것을 막기 위해 % 16
     */
    for (int y = 0; y < IN_H; y++)
    {
        for (int x = 0; x < IN_W; x++)
        {
            ifmap[y][x] = (int16_t)((y * IN_W + x) % 16);
        }
    }


    /*
     * Test Weight
     *
     * 실제 CNN에서는 학습된 weight를 넣으면 됨.
     */
    for (int oc = 0; oc < OUT_CH; oc++)
    {
        for (int ky = 0; ky < KERNEL_H; ky++)
        {
            for (int kx = 0; kx < KERNEL_W; kx++)
            {
                weight[oc][ky][kx] = (int16_t)(oc + 1);
            }
        }

        bias[oc] = 0;
    }


    /*
     * Layer 2 test input represents a quantized 6-channel layer-1 output.
     * The signed test weights generate both positive and negative psums.
     */
    for (int ic = 0; ic < L2_IN_CH; ic++)
    {
        for (int y = 0; y < L2_IN_H; y++)
        {
            for (int x = 0; x < L2_IN_W; x++)
            {
                l2_ifmap[ic][y][x] =
                    (int16_t)((ic * 3 + y * L2_IN_W + x) % 16);
            }
        }
    }

    for (int oc = 0; oc < L2_OUT_CH; oc++)
    {
        for (int ic = 0; ic < L2_IN_CH; ic++)
        {
            for (int ky = 0; ky < KERNEL_H; ky++)
            {
                for (int kx = 0; kx < KERNEL_W; kx++)
                {
                    l2_weight[oc][ic][ky][kx] =
                        (int16_t)(((oc + ic + ky + kx) % 5) - 2);
                }
            }
        }

        l2_bias[oc] = oc - (L2_OUT_CH / 2);
    }


    /*
     * Convolution
     */
    trace_fp = fopen(TRACE_FILE_PATH, "w");

    if (trace_fp == NULL)
    {
        fprintf(stderr, "failed to open %s\n", TRACE_FILE_PATH);
        return 1;
    }

    trace_write("step,layer,oy,ox,pass,oc,ic,window,weight,"
                "acc_before,mac_out,acc_after\n");

    conv_1ch_6ch_hw_style(
        ifmap,
        weight,
        bias,
        ofmap
    );

    conv_6ch_16ch_ref(
        l2_ifmap,
        l2_weight,
        l2_bias,
        l2_ofmap_ref
    );

    conv_6ch_16ch_hw_style(
        l2_ifmap,
        l2_weight,
        l2_bias,
        l2_ofmap_hw,
        l2_psum_after_pass
    );

    fclose(trace_fp);
    trace_fp = NULL;

    // conv_1ch_6ch(
    //     ifmap,
    //     weight,
    //     bias,
    //     ofmap
    // );


    /*
     * 결과 일부 출력
     */
    for (int oc = 0; oc < OUT_CH; oc++)
    {
        printf("\n=====================\n");
        printf("Output Channel %d\n", oc);
        printf("=====================\n");

        for (int y = 0; y < 5; y++)
        {
            for (int x = 0; x < 5; x++)
            {
                printf("%6" PRId64 " ", ofmap[oc][y][x]);
            }

            printf("\n");
        }
    }

    /* Show psum accumulation for all 16 output channels at (0, 0). */
    printf("\nLayer 2 psum trace at (oy=0, ox=0)\n");
    printf("  oc |   bias | after ch 0~2 | after ch 3~5 | output\n");
    printf("-------------------------------------------------------\n");

    for (int oc = 0; oc < L2_OUT_CH; oc++)
    {
        printf("%4d | %6" PRId32 " | %12" PRId64 " | %12" PRId64 " | %6" PRId64 "\n",
               oc,
               l2_bias[oc],
               l2_psum_after_pass[0][oc][0][0],
               l2_psum_after_pass[1][oc][0][0],
               l2_ofmap_hw[oc][0][0]);
    }

    /* Verify every hardware-style output against the reference model. */
    int mismatch_count = 0;

    for (int oc = 0; oc < L2_OUT_CH; oc++)
    {
        for (int y = 0; y < L2_OUT_H; y++)
        {
            for (int x = 0; x < L2_OUT_W; x++)
            {
                if (l2_ofmap_hw[oc][y][x] != l2_ofmap_ref[oc][y][x])
                {
                    mismatch_count++;
                }
            }
        }
    }

    printf("\nLayer 2 verification: %s (mismatches: %d)\n",
           mismatch_count == 0 ? "PASS" : "FAIL",
           mismatch_count);

    return 0;
}
