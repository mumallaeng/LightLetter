#include "cpu_cnn.h"
#include "cpu_cnn_weights.h"

#define C1_OUT  26      /* 28 - 2 */
#define P1_OUT  13
#define C2_OUT  11      /* 13 - 2 */
#define P2_OUT  5

/* 중간 결과. 보드에서 한 번에 한 장만 돌리므로 static 으로 둡니다. */
static int16_t c1[6][C1_OUT][C1_OUT];
static int16_t p1[6][P1_OUT][P1_OUT];
static int16_t c2[16][C2_OUT][C2_OUT];
static int16_t p2[16 * P2_OUT * P2_OUT];     /* och-major 5x5 raster = pool_l2 stream order = fc1 input */
static int16_t y1[FC1_OUT];
static int16_t y2[FC2_OUT];
static int16_t lg[FC3_OUT];

/*
 * ReLU (relu 일 때) -> round-half-to-even >> s -> clamp
 * (relu_quant.v / quantizer.v / fc_quant_signed.v, gen_fc_golden.quant)
 */
static int16_t quant(int64_t acc, int s, int relu)
{
    int64_t q, rem, half;

    if (relu && acc < 0) {
        acc = 0;
    }
    q    = acc >> s;                            /* arithmetic shift = floor */
    rem  = acc & (((int64_t)1 << s) - 1);
    half = (int64_t)1 << (s - 1);
    if (rem > half || (rem == half && (q & 1))) {
        q++;
    }
    if (q > 32767) {
        q = 32767;
    }
    if (q < (relu ? 0 : -32768)) {
        q = relu ? 0 : -32768;
    }
    return (int16_t)q;
}

static void conv1(const int16_t img[CPU_CNN_IMG_PIX])
{
    int o, y, x, ky, kx;

    for (o = 0; o < 6; o++) {
        for (y = 0; y < C1_OUT; y++) {
            for (x = 0; x < C1_OUT; x++) {
                int64_t acc = conv1_b[o];
                for (ky = 0; ky < 3; ky++) {
                    for (kx = 0; kx < 3; kx++) {
                        acc += (int32_t)conv1_w[o][0][ky][kx] * img[(y + ky) * CPU_CNN_IMG_W + x + kx];
                    }
                }
                c1[o][y][x] = quant(acc, CONV1_SCALE_EXP, 1);
            }
        }
    }
}

static void pool1(void)
{
    int c, y, x;

    for (c = 0; c < 6; c++) {
        for (y = 0; y < P1_OUT; y++) {
            for (x = 0; x < P1_OUT; x++) {
                int16_t m = c1[c][2 * y][2 * x];
                if (c1[c][2 * y][2 * x + 1] > m)     m = c1[c][2 * y][2 * x + 1];
                if (c1[c][2 * y + 1][2 * x] > m)     m = c1[c][2 * y + 1][2 * x];
                if (c1[c][2 * y + 1][2 * x + 1] > m) m = c1[c][2 * y + 1][2 * x + 1];
                p1[c][y][x] = m;
            }
        }
    }
}

static void conv2(void)
{
    int o, ci, y, x, ky, kx;

    for (o = 0; o < 16; o++) {
        for (y = 0; y < C2_OUT; y++) {
            for (x = 0; x < C2_OUT; x++) {
                int64_t acc = conv2_b[o];
                for (ci = 0; ci < 6; ci++) {
                    for (ky = 0; ky < 3; ky++) {
                        for (kx = 0; kx < 3; kx++) {
                            acc += (int32_t)conv2_w[o][ci][ky][kx] * p1[ci][y + ky][x + kx];
                        }
                    }
                }
                c2[o][y][x] = quant(acc, CONV2_SCALE_EXP, 1);
            }
        }
    }
}

static void pool2(void)
{
    int c, y, x;

    /* 11 -> 5 : 마지막 행/열은 버립니다 (floor) */
    for (c = 0; c < 16; c++) {
        for (y = 0; y < P2_OUT; y++) {
            for (x = 0; x < P2_OUT; x++) {
                int16_t m = c2[c][2 * y][2 * x];
                if (c2[c][2 * y][2 * x + 1] > m)     m = c2[c][2 * y][2 * x + 1];
                if (c2[c][2 * y + 1][2 * x] > m)     m = c2[c][2 * y + 1][2 * x];
                if (c2[c][2 * y + 1][2 * x + 1] > m) m = c2[c][2 * y + 1][2 * x + 1];
                p2[(c * P2_OUT + y) * P2_OUT + x] = m;
            }
        }
    }
}

static void fc(const int16_t *x, int n_in, const int16_t *w, const int32_t *b,
               int n_out, int s, int relu, int16_t *y)
{
    int n, i;

    for (n = 0; n < n_out; n++) {
        const int16_t *row = w + n * n_in;
        int64_t acc = b[n];
        for (i = 0; i < n_in; i++) {
            acc += (int32_t)x[i] * row[i];
        }
        y[n] = quant(acc, s, relu);
    }
}

int cpu_cnn_run(const int16_t img[CPU_CNN_IMG_PIX], int16_t logit[CPU_CNN_NUM_CLASS])
{
    int i, cls;

    conv1(img);
    pool1();
    conv2();
    pool2();
    fc(p2, FC1_IN, &fc1_w[0][0], fc1_b, FC1_OUT, FC1_SCALE_EXP, FC1_RELU, y1);
    fc(y1, FC2_IN, &fc2_w[0][0], fc2_b, FC2_OUT, FC2_SCALE_EXP, FC2_RELU, y2);
    fc(y2, FC3_IN, &fc3_w[0][0], fc3_b, FC3_OUT, FC3_SCALE_EXP, FC3_RELU, lg);

    /* argmax : 같은 값이면 작은 index (argmax.v) */
    cls = 0;
    for (i = 1; i < CPU_CNN_NUM_CLASS; i++) {
        if (lg[i] > lg[cls]) {
            cls = i;
        }
    }
    if (logit) {
        for (i = 0; i < CPU_CNN_NUM_CLASS; i++) {
            logit[i] = lg[i];
        }
    }
    return cls;
}
