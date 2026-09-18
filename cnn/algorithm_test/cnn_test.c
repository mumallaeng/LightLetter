#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <math.h>

/*
 * LightLetter CNN - layer 1, 2 정수 모델
 *
 *   input  1 x 28 x 28
 *   conv1  1 -> 6ch, 3x3, stride 1, padding 0      -> 6 x 26 x 26
 *   ReLU + INT16 재양자화
 *   maxpool 2x2, stride 1 (line buffer)            -> 6 x 25 x 25
 *   conv2  6 -> 16ch, 3x3, stride 1, padding 0     -> 16 x 23 x 23
 *   ReLU + INT16 재양자화
 *   maxpool 2x2, stride 1 (line buffer)            -> 16 x 22 x 22
 *
 * shape은 tb/cnn_golden 의 Net(conv_channels=[1,6,16], padding=0,
 * pool_stride=1) 과 같다.
 */

#define IN_H        28
#define IN_W        28

#define IN_CH       1
#define OUT_CH      6

#define KERNEL_H    3
#define KERNEL_W    3

#define STRIDE      1

#define OUT_H       ((IN_H - KERNEL_H) / STRIDE + 1)
#define OUT_W       ((IN_W - KERNEL_W) / STRIDE + 1)

/* 2x2 / stride 1 max pooling 출력 크기 */
#define POOL_K      2
#define POOL_STRIDE 1
#define POOL_OUT(n) (((n) - POOL_K) / POOL_STRIDE + 1)

#define P1_H        POOL_OUT(OUT_H)
#define P1_W        POOL_OUT(OUT_W)

/* Layer 2: three line buffers process six input channels in two passes. */
#define L2_IN_H                 P1_H
#define L2_IN_W                 P1_W
#define L2_IN_CH                OUT_CH
#define L2_OUT_CH               16
#define L2_CHANNELS_PER_PASS    3
#define L2_NUM_PASSES           (L2_IN_CH / L2_CHANNELS_PER_PASS)
#define L2_OUT_H                ((L2_IN_H - KERNEL_H) / STRIDE + 1)
#define L2_OUT_W                ((L2_IN_W - KERNEL_W) / STRIDE + 1)

#define P2_H        POOL_OUT(L2_OUT_H)
#define P2_W        POOL_OUT(L2_OUT_W)

#if (L2_IN_CH % L2_CHANNELS_PER_PASS) != 0
#error "L2_IN_CH must be divisible by L2_CHANNELS_PER_PASS"
#endif


/*
 * INT16 양자화
 * ============
 *
 * golden(tb/cnn_golden/model.py 의 Quant16)은 scale을 항상 2의 거듭제곱으로
 * 잡는다.
 *
 *     scale = 2 ^ ceil( log2( max|x| / 32767 ) )
 *
 * 즉 실수값 x 는 정수 q 와 지수 E 로 표현된다.
 *
 *     x ~= q * 2^E,   q = round_to_even(x / 2^E),   -32768 <= q <= 32767
 *
 * scale이 2의 거듭제곱이므로 하드웨어에서 곱셈/나눗셈이 필요 없고,
 * 재양자화가 산술 shift 하나로 끝난다.
 *
 *   - activation q : int16_t
 *   - weight     q : int16_t
 *   - bias         : accumulator 단위 정수 (아래 참고)
 *   - accumulator  : int64_t
 *
 * MAC 누산값의 단위
 * -----------------
 *   acc = sum( q_in * q_w )  이므로 실수값은  acc * 2^(E_in + E_w) 이다.
 *   bias는 같은 단위로 미리 변환해서 더한다.
 *
 *       bias_acc = round( bias_real / 2^(E_in + E_w) )
 *
 * 재양자화 (다음 layer 입력으로 만들기)
 * -------------------------------------
 *   다음 layer는 지수 E_out 인 int16을 받으므로
 *
 *       q_out = round_to_even( acc * 2^(E_in + E_w) / 2^E_out )
 *             = round_to_even( acc / 2^SHIFT ),
 *         SHIFT = E_out - (E_in + E_w)
 *
 *   ReLU는 shift 전에 적용한다 (acc < 0 이면 0). 순서를 바꿔도 결과는 같다.
 *   마지막에 [-32768, 32767] 로 saturate 한다.
 */
/*
 * 지수(E)는 학습된 모델의 Quant16 observer 값에서 정해진다.
 *
 *   E = ceil( log2( maximum / 32767 ) )
 *
 * E_IN 은 입력이 0~1 이라 maximum = 1.0 으로 고정 -> ceil(-14.99996) = -14.
 * 나머지는 테스트용 값이다. 학습된 모델로 바꿀 때는 노트북에서
 *   weight_quant[i].scale, activation_quant[i].scale  (= 2^E)
 * 를 읽어 그대로 넣으면 된다.
 */
#define E_IN        (-14)   /* 입력 이미지 scale : 2^-14 (0~1 정규화) */
#define E_W1        (-16)   /* conv1 weight scale      (테스트값) */
#define E_ACT1      (-15)   /* conv1 출력 activation scale (테스트값) */
#define E_W2        (-16)   /* conv2 weight scale      (테스트값) */
#define E_ACT2      (-15)   /* conv2 출력 activation scale (테스트값) */

#define SHIFT1      (E_ACT1 - (E_IN + E_W1))
#define SHIFT2      (E_ACT2 - (E_ACT1 + E_W2))

#define Q16_MAX     32767
#define Q16_MIN     (-32768)


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
 *
 * ReLU / 재양자화 / max pooling 은 trace에 남기지 않는다.
 * 단계별 전체 출력은 STAGE_FILE_PATH 에 따로 기록한다.
 */
#define TRACE_FILE_PATH     "cnn_trace.csv"
#define STAGE_FILE_PATH     "cnn_stages.txt"
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
 * 양자화 기본 연산
 */

/* round-to-nearest-even 산술 우shift. shift <= 0 이면 좌shift. */
static int64_t shift_round_even(int64_t v, int shift)
{
    if (shift <= 0)
    {
        return v << (-shift);
    }

    int64_t step = (int64_t)1 << shift;
    int64_t q    = v >> shift;              /* 산술 shift = floor */
    int64_t rem  = v - (q << shift);        /* 0 <= rem < step */
    int64_t half = step >> 1;

    if (rem > half || (rem == half && (q & 1) != 0))
    {
        q += 1;
    }

    return q;
}

static int16_t saturate16(int64_t q)
{
    if (q > Q16_MAX)
    {
        return (int16_t)Q16_MAX;
    }

    if (q < Q16_MIN)
    {
        return (int16_t)Q16_MIN;
    }

    return (int16_t)q;
}

/* ReLU -> 재양자화(shift) -> saturate. 하드웨어의 PE 출력단에 해당. */
static int16_t relu_requantize(int64_t acc, int shift)
{
    if (acc < 0)
    {
        acc = 0;
    }

    return saturate16(shift_round_even(acc, shift));
}

/* 실수 -> int16 양자화 (입력 이미지, weight 준비용. 호스트 쪽 작업) */
static int16_t quantize_real(double x, int exponent)
{
    double scaled = ldexp(x, -exponent);
    double r      = nearbyint(scaled);   /* 기본 rounding mode = round-to-even */

    if (r > (double)Q16_MAX)
    {
        return (int16_t)Q16_MAX;
    }

    if (r < (double)Q16_MIN)
    {
        return (int16_t)Q16_MIN;
    }

    return (int16_t)r;
}

/* 실수 bias -> accumulator 단위 정수 */
static int64_t bias_to_acc(double bias_real, int e_in, int e_w)
{
    return (int64_t)nearbyint(ldexp(bias_real, -(e_in + e_w)));
}


/*
 * 2x2 / stride 1 max pooling - line buffer 기반
 *
 * 한 채널의 픽셀이 raster 순서로 하나씩 들어온다고 본다.
 * 들고 있는 것은 직전 행 한 줄(line)과 직전 픽셀(left) 뿐이고,
 * 2x2 window가 완성되는 순간 출력 하나가 나온다.
 *
 *     line[x-1] = (y-1, x-1)      left = (y, x-1)
 *     line[x]   = (y-1, x)        cur  = (y, x)
 *
 * line buffer는 호출하는 쪽에서 준다 (폭 W).
 */
static void maxpool2x2_s1_linebuffer(
    const int16_t *in,
    int16_t *out,
    int H,
    int W,
    int16_t *line
)
{
    int16_t left = 0;

    for (int y = 0; y < H; y++)
    {
        for (int x = 0; x < W; x++)
        {
            int16_t cur = in[y * W + x];

            /* window가 다 모이는 것은 y >= 1, x >= 1 부터 */
            if (y >= 1 && x >= 1)
            {
                int16_t m = line[x - 1];

                if (line[x] > m) { m = line[x]; }
                if (left     > m) { m = left;     }
                if (cur      > m) { m = cur;      }

                out[(y - 1) * (W - 1) + (x - 1)] = m;
            }

            /* 다 쓴 자리에 현재 행 값을 밀어 넣는다. */
            if (x >= 1)
            {
                line[x - 1] = left;
            }

            left = cur;
        }

        line[W - 1] = left;     /* 행의 마지막 픽셀 */
    }
}


/*
 * Input:
 *   ifmap[28][28]
 *
 * Weight:
 *   weight[6][3][3]
 *
 * Output:
 *   ofmap[6][26][26]  (accumulator 단위)
 */
void conv_1ch_6ch(
    const int16_t ifmap[IN_H][IN_W],
    const int16_t weight[OUT_CH][KERNEL_H][KERNEL_W],
    const int64_t bias[OUT_CH],
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
    const int64_t bias[OUT_CH],
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
    const int64_t bias[L2_OUT_CH],
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
 * pass 사이의 중간 누산값은 psum_mem(16 x 23 x 23)에 보관한다.
 * 한 위치의 3개 window는 16개 output channel이 재사용한다.
 * psum_after_pass 는 pass별 psum_mem 상태를 그대로 노출한다.
 */
void conv_6ch_16ch_hw_style(
    const int16_t ifmap[L2_IN_CH][L2_IN_H][L2_IN_W],
    const int16_t weight[L2_OUT_CH][L2_IN_CH][KERNEL_H][KERNEL_W],
    const int64_t bias[L2_OUT_CH],
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


/*
 * 단계별 출력 (콘솔 요약 + 파일 덤프)
 *
 * 콘솔 표는 tb/cnn_golden 노트북의 inspect_layers 출력과 같은 형식이다.
 * 정수값에 scale(2^exponent)을 곱해 실수로 환산한 값을 보여준다.
 */
static FILE *stage_fp = NULL;

static void stage_report_i16(
    const char *name, const int16_t *v, int ch, int h, int w, int exponent
)
{
    size_t n = (size_t)ch * h * w;
    double scale = ldexp(1.0, exponent);
    double mn = v[0] * scale, mx = v[0] * scale, sum = 0.0;
    char shape[32];

    for (size_t i = 0; i < n; i++)
    {
        double x = v[i] * scale;

        if (x < mn) { mn = x; }
        if (x > mx) { mx = x; }
        sum += x;
    }

    snprintf(shape, sizeof(shape), "(1, %d, %d, %d)", ch, h, w);
    printf("%-32s %18s %9.3f %9.3f %9.3f\n",
           name, shape, mn, mx, sum / (double)n);

    if (stage_fp != NULL)
    {
        fprintf(stage_fp, "tensor %s int16 %d %d %d %d\n",
                name, ch, h, w, exponent);

        for (size_t i = 0; i < n; i++)
        {
            fprintf(stage_fp, "%d%c", v[i], (i + 1) % 16 == 0 ? '\n' : ' ');
        }

        if (n % 16 != 0) { fputc('\n', stage_fp); }
    }
}

static void stage_report_acc(
    const char *name, const int64_t *v, int ch, int h, int w, int exponent
)
{
    size_t n = (size_t)ch * h * w;
    double scale = ldexp(1.0, exponent);
    double mn = v[0] * scale, mx = v[0] * scale, sum = 0.0;
    char shape[32];

    for (size_t i = 0; i < n; i++)
    {
        double x = (double)v[i] * scale;

        if (x < mn) { mn = x; }
        if (x > mx) { mx = x; }
        sum += x;
    }

    snprintf(shape, sizeof(shape), "(1, %d, %d, %d)", ch, h, w);
    printf("%-32s %18s %9.3f %9.3f %9.3f\n",
           name, shape, mn, mx, sum / (double)n);

    if (stage_fp != NULL)
    {
        fprintf(stage_fp, "tensor %s acc %d %d %d %d\n",
                name, ch, h, w, exponent);

        for (size_t i = 0; i < n; i++)
        {
            fprintf(stage_fp, "%" PRId64 "%c",
                    v[i], (i + 1) % 16 == 0 ? '\n' : ' ');
        }

        if (n % 16 != 0) { fputc('\n', stage_fp); }
    }
}


int main(void)
{
    /* layer 1 */
    static int16_t ifmap[IN_H][IN_W];
    static int16_t w1[OUT_CH][KERNEL_H][KERNEL_W];
    static int64_t b1[OUT_CH];
    static int64_t conv1_acc[OUT_CH][OUT_H][OUT_W];
    static int16_t conv1_q[OUT_CH][OUT_H][OUT_W];
    static int16_t pool1[OUT_CH][P1_H][P1_W];

    /* layer 2 */
    static int16_t w2[L2_OUT_CH][L2_IN_CH][KERNEL_H][KERNEL_W];
    static int64_t b2[L2_OUT_CH];
    static int64_t conv2_acc_ref[L2_OUT_CH][L2_OUT_H][L2_OUT_W];
    static int64_t conv2_acc_hw[L2_OUT_CH][L2_OUT_H][L2_OUT_W];
    static int64_t psum_after_pass
        [L2_NUM_PASSES][L2_OUT_CH][L2_OUT_H][L2_OUT_W];
    static int16_t conv2_q[L2_OUT_CH][L2_OUT_H][L2_OUT_W];
    static int16_t pool2[L2_OUT_CH][P2_H][P2_W];

    /* max pooling line buffer (폭이 큰 layer 1 기준으로 하나만 잡는다) */
    static int16_t pool_line[OUT_W];


    /*
     * 입력 이미지
     *
     * 0~255 픽셀을 0~1로 정규화한 뒤 2^E_IN 로 양자화한다.
     * (golden의 input_quant 와 같은 단계)
     */
    for (int y = 0; y < IN_H; y++)
    {
        for (int x = 0; x < IN_W; x++)
        {
            int pixel = (y * IN_W + x * 7) % 256;

            ifmap[y][x] = quantize_real(pixel / 255.0, E_IN);
        }
    }


    /*
     * Test Weight / Bias
     *
     * 실제 CNN에서는 학습된 weight를 양자화해서 넣으면 된다.
     * weight는 이미 int16 형태(q), bias는 accumulator 단위로 변환해 둔다.
     */
    for (int oc = 0; oc < OUT_CH; oc++)
    {
        for (int ky = 0; ky < KERNEL_H; ky++)
        {
            for (int kx = 0; kx < KERNEL_W; kx++)
            {
                w1[oc][ky][kx] =
                    (int16_t)((((oc + ky + kx) % 5) - 2) * 4096);
            }
        }

        b1[oc] = bias_to_acc(0.02 * (oc - 3), E_IN, E_W1);
    }

    for (int oc = 0; oc < L2_OUT_CH; oc++)
    {
        for (int ic = 0; ic < L2_IN_CH; ic++)
        {
            for (int ky = 0; ky < KERNEL_H; ky++)
            {
                for (int kx = 0; kx < KERNEL_W; kx++)
                {
                    w2[oc][ic][ky][kx] =
                        (int16_t)((((oc + ic + ky + kx) % 5) - 2) * 2048);
                }
            }
        }

        b2[oc] = bias_to_acc(0.01 * (oc - (L2_OUT_CH / 2)), E_ACT1, E_W2);
    }


    /*
     * trace / stage 파일 열기
     */
    trace_fp = fopen(TRACE_FILE_PATH, "w");
    stage_fp = fopen(STAGE_FILE_PATH, "w");

    if (trace_fp == NULL || stage_fp == NULL)
    {
        fprintf(stderr, "출력 파일을 열지 못했습니다\n");
        return 1;
    }

    trace_write("step,layer,oy,ox,pass,oc,ic,window,weight,"
                "acc_before,mac_out,acc_after\n");

    fprintf(stage_fp, "# LightLetter CNN integer model dump\n");
    fprintf(stage_fp, "config E_IN %d E_W1 %d E_ACT1 %d E_W2 %d E_ACT2 %d\n",
            E_IN, E_W1, E_ACT1, E_W2, E_ACT2);
    fprintf(stage_fp, "config SHIFT1 %d SHIFT2 %d\n", SHIFT1, SHIFT2);

    fprintf(stage_fp, "param w1 %d %d %d %d %d\n",
            OUT_CH, IN_CH, KERNEL_H, KERNEL_W, E_W1);
    for (int oc = 0; oc < OUT_CH; oc++)
    {
        for (int ky = 0; ky < KERNEL_H; ky++)
        {
            for (int kx = 0; kx < KERNEL_W; kx++)
            {
                fprintf(stage_fp, "%d ", w1[oc][ky][kx]);
            }
        }
        fprintf(stage_fp, "\n");
    }

    fprintf(stage_fp, "param b1 %d\n", OUT_CH);
    for (int oc = 0; oc < OUT_CH; oc++)
    {
        fprintf(stage_fp, "%" PRId64 " ", b1[oc]);
    }
    fprintf(stage_fp, "\n");

    fprintf(stage_fp, "param w2 %d %d %d %d %d\n",
            L2_OUT_CH, L2_IN_CH, KERNEL_H, KERNEL_W, E_W2);
    for (int oc = 0; oc < L2_OUT_CH; oc++)
    {
        for (int ic = 0; ic < L2_IN_CH; ic++)
        {
            for (int ky = 0; ky < KERNEL_H; ky++)
            {
                for (int kx = 0; kx < KERNEL_W; kx++)
                {
                    fprintf(stage_fp, "%d ", w2[oc][ic][ky][kx]);
                }
            }
        }
        fprintf(stage_fp, "\n");
    }

    fprintf(stage_fp, "param b2 %d\n", L2_OUT_CH);
    for (int oc = 0; oc < L2_OUT_CH; oc++)
    {
        fprintf(stage_fp, "%" PRId64 " ", b2[oc]);
    }
    fprintf(stage_fp, "\n");


    /*
     * Layer 1 : conv -> ReLU + 재양자화 -> max pooling
     */
    conv_1ch_6ch_hw_style(ifmap, w1, b1, conv1_acc);

    for (int oc = 0; oc < OUT_CH; oc++)
    {
        for (int y = 0; y < OUT_H; y++)
        {
            for (int x = 0; x < OUT_W; x++)
            {
                conv1_q[oc][y][x] =
                    relu_requantize(conv1_acc[oc][y][x], SHIFT1);
            }
        }

        maxpool2x2_s1_linebuffer(&conv1_q[oc][0][0], &pool1[oc][0][0],
                                 OUT_H, OUT_W, pool_line);
    }


    /*
     * Layer 2 : conv -> ReLU + 재양자화 -> max pooling
     */
    conv_6ch_16ch_ref(pool1, w2, b2, conv2_acc_ref);

    conv_6ch_16ch_hw_style(pool1, w2, b2, conv2_acc_hw, psum_after_pass);

    for (int oc = 0; oc < L2_OUT_CH; oc++)
    {
        for (int y = 0; y < L2_OUT_H; y++)
        {
            for (int x = 0; x < L2_OUT_W; x++)
            {
                conv2_q[oc][y][x] =
                    relu_requantize(conv2_acc_hw[oc][y][x], SHIFT2);
            }
        }

        maxpool2x2_s1_linebuffer(&conv2_q[oc][0][0], &pool2[oc][0][0],
                                 L2_OUT_H, L2_OUT_W, pool_line);
    }

    fclose(trace_fp);
    trace_fp = NULL;


    /*
     * 단계별 요약 (tb/cnn_golden 노트북과 같은 형식)
     */
    printf("%-32s %18s %9s %9s %9s\n", "단계", "shape", "min", "max", "mean");

    stage_report_i16("input (quantized)",
                     &ifmap[0][0], 1, IN_H, IN_W, E_IN);
    stage_report_acc("conv1 (raw)",
                     &conv1_acc[0][0][0], OUT_CH, OUT_H, OUT_W, E_IN + E_W1);
    stage_report_i16("conv1 + ReLU (quantized)",
                     &conv1_q[0][0][0], OUT_CH, OUT_H, OUT_W, E_ACT1);
    stage_report_i16("conv1 + MaxPool",
                     &pool1[0][0][0], OUT_CH, P1_H, P1_W, E_ACT1);
    stage_report_acc("conv2 (raw)",
                     &conv2_acc_hw[0][0][0], L2_OUT_CH, L2_OUT_H, L2_OUT_W,
                     E_ACT1 + E_W2);
    stage_report_i16("conv2 + ReLU (quantized)",
                     &conv2_q[0][0][0], L2_OUT_CH, L2_OUT_H, L2_OUT_W, E_ACT2);
    stage_report_i16("conv2 + MaxPool",
                     &pool2[0][0][0], L2_OUT_CH, P2_H, P2_W, E_ACT2);

    fclose(stage_fp);
    stage_fp = NULL;


    /* Show psum accumulation for all 16 output channels at (0, 0). */
    printf("\nLayer 2 psum trace at (oy=0, ox=0)\n");
    printf("  oc |        bias | after ch 0~2 | after ch 3~5 | q(act)\n");
    printf("------------------------------------------------------------\n");

    for (int oc = 0; oc < L2_OUT_CH; oc++)
    {
        printf("%4d | %11" PRId64 " | %12" PRId64 " | %12" PRId64 " | %6d\n",
               oc,
               b2[oc],
               psum_after_pass[0][oc][0][0],
               psum_after_pass[1][oc][0][0],
               conv2_q[oc][0][0]);
    }

    /* Verify every hardware-style output against the reference model. */
    int mismatch_count = 0;

    for (int oc = 0; oc < L2_OUT_CH; oc++)
    {
        for (int y = 0; y < L2_OUT_H; y++)
        {
            for (int x = 0; x < L2_OUT_W; x++)
            {
                if (conv2_acc_hw[oc][y][x] != conv2_acc_ref[oc][y][x])
                {
                    mismatch_count++;
                }
            }
        }
    }

    printf("\nLayer 2 verification: %s (mismatches: %d)\n",
           mismatch_count == 0 ? "PASS" : "FAIL",
           mismatch_count);

    printf("trace : %s (%" PRIu64 " MAC)\n", TRACE_FILE_PATH, trace_step);
    printf("stages: %s\n", STAGE_FILE_PATH);

    return 0;
}
