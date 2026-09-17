#include <stdio.h>
#include <stdint.h>

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
    const uint8_t ifmap[IN_H][IN_W],
    const int8_t weight[OUT_CH][KERNEL_H][KERNEL_W],
    const int32_t bias[OUT_CH],
    int32_t ofmap[OUT_CH][OUT_H][OUT_W]
)
{
    for (int oc = 0; oc < OUT_CH; oc++)
    {
        for (int oy = 0; oy < OUT_H; oy++)
        {
            for (int ox = 0; ox < OUT_W; ox++)
            {
                int32_t acc = bias[oc];

                for (int ky = 0; ky < KERNEL_H; ky++)
                {
                    for (int kx = 0; kx < KERNEL_W; kx++)
                    {
                        uint8_t input_value =
                            ifmap[oy * STRIDE + ky]
                                 [ox * STRIDE + kx];

                        int8_t weight_value =
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
    const uint8_t ifmap[IN_H][IN_W],
    const int8_t weight[OUT_CH][KERNEL_H][KERNEL_W],
    const int32_t bias[OUT_CH],
    int32_t ofmap[OUT_CH][OUT_H][OUT_W]
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
            uint8_t window[3][3];

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
                int32_t acc = bias[oc];

                for (int ky = 0; ky < 3; ky++)
                {
                    for (int kx = 0; kx < 3; kx++)
                    {
                        acc +=
                            (int32_t)window[ky][kx]
                            *
                            (int32_t)weight[oc][ky][kx];
                    }
                }

                ofmap[oc][oy][ox] = acc;
            }
        }
    }
}


/* Straightforward layer-2 model used as the golden reference. */
void conv_6ch_16ch_ref(
    const uint8_t ifmap[L2_IN_CH][L2_IN_H][L2_IN_W],
    const int8_t weight[L2_OUT_CH][L2_IN_CH][KERNEL_H][KERNEL_W],
    const int32_t bias[L2_OUT_CH],
    int32_t ofmap[L2_OUT_CH][L2_OUT_H][L2_OUT_W]
)
{
    for (int oc = 0; oc < L2_OUT_CH; oc++)
    {
        for (int oy = 0; oy < L2_OUT_H; oy++)
        {
            for (int ox = 0; ox < L2_OUT_W; ox++)
            {
                int32_t acc = bias[oc];

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
 * pass 0: three line buffers receive input channels 0~2
 *         psum = bias + sum(ch 0~2)
 * pass 1: the line buffers receive input channels 3~5
 *         psum = previous psum + sum(ch 3~5)
 *
 * The three windows are loaded once per pass and reused by all 16 output
 * channels. psum_after_pass exposes the intermediate accumulation results.
 */
void conv_6ch_16ch_hw_style(
    const uint8_t ifmap[L2_IN_CH][L2_IN_H][L2_IN_W],
    const int8_t weight[L2_OUT_CH][L2_IN_CH][KERNEL_H][KERNEL_W],
    const int32_t bias[L2_OUT_CH],
    int32_t ofmap[L2_OUT_CH][L2_OUT_H][L2_OUT_W],
    int32_t psum_after_pass[L2_NUM_PASSES][L2_OUT_CH][L2_OUT_H][L2_OUT_W]
)
{
    for (int oy = 0; oy < L2_OUT_H; oy++)
    {
        for (int ox = 0; ox < L2_OUT_W; ox++)
        {
            int32_t psum[L2_OUT_CH];

            for (int oc = 0; oc < L2_OUT_CH; oc++)
            {
                psum[oc] = bias[oc];
            }

            for (int pass = 0; pass < L2_NUM_PASSES; pass++)
            {
                uint8_t window[L2_CHANNELS_PER_PASS][KERNEL_H][KERNEL_W];

                /* The three line buffers create three 3x3 windows. */
                for (int lane = 0; lane < L2_CHANNELS_PER_PASS; lane++)
                {
                    int ic = pass * L2_CHANNELS_PER_PASS + lane;

                    for (int ky = 0; ky < KERNEL_H; ky++)
                    {
                        for (int kx = 0; kx < KERNEL_W; kx++)
                        {
                            window[lane][ky][kx] =
                                ifmap[ic][oy * STRIDE + ky]
                                         [ox * STRIDE + kx];
                        }
                    }
                }

                /* Reuse these input windows across all output channels. */
                for (int oc = 0; oc < L2_OUT_CH; oc++)
                {
                    int32_t acc = psum[oc];

                    for (int lane = 0; lane < L2_CHANNELS_PER_PASS; lane++)
                    {
                        int ic = pass * L2_CHANNELS_PER_PASS + lane;

                        for (int ky = 0; ky < KERNEL_H; ky++)
                        {
                            for (int kx = 0; kx < KERNEL_W; kx++)
                            {
                                acc +=
                                    (int32_t)window[lane][ky][kx]
                                    *
                                    (int32_t)weight[oc][ic][ky][kx];
                            }
                        }
                    }

                    psum[oc] = acc;
                    psum_after_pass[pass][oc][oy][ox] = acc;
                }
            }

            for (int oc = 0; oc < L2_OUT_CH; oc++)
            {
                ofmap[oc][oy][ox] = psum[oc];
            }
        }
    }
}


int main(void)
{
    static uint8_t ifmap[IN_H][IN_W];
    static int8_t weight[OUT_CH][KERNEL_H][KERNEL_W];
    static int32_t bias[OUT_CH];
    static int32_t ofmap[OUT_CH][OUT_H][OUT_W];

    static uint8_t l2_ifmap[L2_IN_CH][L2_IN_H][L2_IN_W];
    static int8_t l2_weight[L2_OUT_CH][L2_IN_CH][KERNEL_H][KERNEL_W];
    static int32_t l2_bias[L2_OUT_CH];
    static int32_t l2_ofmap_ref[L2_OUT_CH][L2_OUT_H][L2_OUT_W];
    static int32_t l2_ofmap_hw[L2_OUT_CH][L2_OUT_H][L2_OUT_W];
    static int32_t l2_psum_after_pass
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
            ifmap[y][x] = (uint8_t)((y * IN_W + x) % 16);
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
                weight[oc][ky][kx] = (int8_t)(oc + 1);
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
                    (uint8_t)((ic * 3 + y * L2_IN_W + x) % 16);
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
                        (int8_t)(((oc + ic + ky + kx) % 5) - 2);
                }
            }
        }

        l2_bias[oc] = oc - (L2_OUT_CH / 2);
    }


    /*
     * Convolution
     */
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
                printf("%6d ", ofmap[oc][y][x]);
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
        printf("%4d | %6d | %12d | %12d | %6d\n",
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
