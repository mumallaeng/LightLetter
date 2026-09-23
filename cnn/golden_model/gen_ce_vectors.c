/*
 * Runs the C golden model chain (conv_l1 -> pool_l1 -> conv_l2 -> pool_l2) on vectors/conv_l{1,2}.txt
 * and writes the $readmemh files an RTL testbench needs to compare against it.
 *
 * ROM contents used by the RTL itself (<mem>, default rtl/cnn/mem):
 *   convN_weight.mem   432-bit entry = {lane2[8:0], lane1[8:0], lane0[8:0]} INT16, tap order ky*3+kx,
 *                      line order [out_ch][grp] (grp = is_ch35: 0 = in_ch 0~2, 1 = in_ch 3~5).
 *                      conv1 (C_IN 1) uses lane 0 of grp 0 only, everything else is 0.
 *   convN_bias.mem     32-bit bias per output channel (already written by gen_rtl_vectors.c;
 *                      re-emitted here as convN_bias_ce.mem only to prove both sources agree)
 *
 * Stage boundary streams for the testbench (<out>, default tb/cnn/vectors):
 *   ce1_stim.mem       conv_l1 pixel_in : 16-bit code, 28x28 raster, FRAMES frames
 *   ce1_out.mem        conv_l1 out      : {out_ch_done, out_data2, out_data1, out_data0}  49 bit
 *   pool1_out.mem      pool_l1 out      : {pool_ch_done, pool_data2, pool_data1, pool_data0} 49 bit
 *   ce2_out.mem        conv_l2 out      : {out_ch_done, out_data} 17 bit
 *   pool2_out.mem      pool_l2 out      : {pool_ch_done, pool_data} 17 bit
 *   ce_params.txt      dimensions, scale_exp and entry counts of every stage
 *
 * Streams are captured with in_valid / out_ready at 100%: the golden data is the *value sequence*,
 * so the RTL testbench is free to apply its own backpressure and compare transfer by transfer.
 * Frame 2 is the mirrored image, same as test_cnn_chain.c.
 *
 *   make -f cnn_chain.mk rtl-vectors
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cnn_chain.h"
#include "common.h"
#include "pool_l1.h"

#define FRAMES      2
#define IN1         28
#define C1          6
#define O1          26
#define P1          13
#define C2          16
#define O2          11
#define P2          5
#define N_IN1       (IN1 * IN1)             /* 784  conv_l1 입력 픽셀 / frame           */
#define N_PIX1      (O1 * O1)               /* 676  conv_l1 출력 픽셀 / pass            */
#define N_E1        (N_PIX1 * 2)            /* 1352 conv_l1 출력 entry (PACK 3) / frame */
#define N_P1        (P1 * P1)               /* 169  pool_l1 출력 픽셀 / pass            */
#define N_E2        (O2 * O2 * C2)          /* 1936 conv_l2 출력 entry (PACK 1) / frame */
#define N_E2P       (P2 * P2 * C2)          /* 400  pool_l2 출력 entry / frame          */
#define MAX_CYCLES  1000000L

static int16_t g_img[FRAMES][IN1][IN1];
static int16_t g_w1[C1 * 9], g_w2[C2 * C1 * 9];
static int32_t g_b1[C1], g_b2[C2];
static uint8_t g_s1, g_s2;

static uint16_t g_l2_out[FRAMES * N_E2];    /* pool_l2 를 따로 돌리기 위한 conv_l2 출력 */
static int      g_n_l2_out;

static pool_l1_t g_pool1, g_pool2;

/* ================================================================
 * vectors/conv_lN.txt 읽기 (test_cnn_chain.c 와 같은 형식)
 * ================================================================ */
static int read_vec(const char *path, int layer)
{
    FILE *f = fopen(path, "r");
    int hdr[6], v, ok = f != NULL;

    for (int i = 0; ok && i < 6; i++)
        ok = fscanf(f, "%d", &hdr[i]) == 1;
    if (!ok)
    {
        if (f) fclose(f);
        return 1;
    }
    int c_in = hdr[0], h = hdr[1], w = hdr[2], c_out = hdr[3], oh = h - 2, n = oh * oh, pn = (oh / 2) * (oh / 2);
    for (int i = 0; ok && i < c_in * h * w; i++)
    {
        ok = fscanf(f, "%d", &v) == 1;
        if (layer == 1)
        {
            g_img[0][i / w][i % w]         = (int16_t)v;
            g_img[1][i / w][w - 1 - i % w] = (int16_t)v;       /* frame 2 = 좌우 반전 */
        }
    }
    for (int i = 0; ok && i < c_out * c_in * 9; i++)
    {
        ok = fscanf(f, "%d", &v) == 1;
        (layer == 1 ? g_w1 : g_w2)[i] = (int16_t)v;
    }
    for (int i = 0; ok && i < c_out; i++)
    {
        ok = fscanf(f, "%d", &v) == 1;
        (layer == 1 ? g_b1 : g_b2)[i] = v;
    }
    for (int i = 0; ok && i < c_out * n; i++)                  /* Python conv 코드: 여기선 안 쓴다 */
        ok = fscanf(f, "%d", &v) == 1;
    for (int i = 0; ok && i < c_out * pn; i++)                 /* Python pool 코드: 여기선 안 쓴다 */
        ok = fscanf(f, "%d", &v) == 1;
    fclose(f);
    if (layer == 1)
        g_s1 = (uint8_t)hdr[5];
    else
        g_s2 = (uint8_t)hdr[5];
    return !ok;
}

/* ================================================================
 * $readmemh 출력
 * ================================================================ */
static FILE *open_out(const char *dir, const char *name)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "w");
    if (!f)
    {
        perror(path);
        exit(1);
    }
    return f;
}

/* value = w[bits-1:0], 상위부터 nibble 단위로 */
static void put_hex_words(FILE *f, const uint64_t *w, int bits)
{
    for (int d = (bits + 3) / 4 - 1; d >= 0; d--)
    {
        unsigned nib = (unsigned)((w[d / 16] >> (4 * (d % 16))) & 0xF);
        fputc("0123456789abcdef"[nib], f);
    }
    fputc('\n', f);
}

static void put_hex(FILE *f, uint64_t v, int bits)
{
    uint64_t w[2] = {v, 0};
    put_hex_words(f, w, bits);
}

/*
 * weight ROM: weight_rom.c 의 배치를 그대로 쓴다.
 *   data[oc][ic / 3][(ic % 3) * 9 + k] = weight[(oc * c_in + ic) * 9 + k]
 * 한 줄 = 한 entry = 432bit, lane0 이 LSB 쪽 (bit 143:0), tap k 가 lane 안에서 LSB 쪽.
 */
static void write_weight_mem(const char *mem_dir, const char *name, const int16_t *weight, int c_out, int c_in)
{
    FILE *f = open_out(mem_dir, name);

    for (int oc = 0; oc < c_out; oc++)
        for (int grp = 0; grp < CE_MAX_GROUPS; grp++)
        {
            uint64_t w[7] = {0};                                   /* 432bit = 27 x 16bit */
            for (int lane = 0; lane < CE_LANES; lane++)
            {
                int ic = grp * CE_LANES + lane;
                if (ic >= c_in)
                    continue;
                for (int k = 0; k < CE_KK; k++)
                {
                    int      slot = lane * CE_KK + k;              /* 0..26, 16bit 단위 위치 */
                    uint64_t val  = (uint64_t)(uint16_t)weight[(oc * c_in + ic) * CE_KK + k];
                    w[slot / 4] |= val << (16 * (slot % 4));
                }
            }
            put_hex_words(f, w, 16 * CE_TAPS);
        }
    fclose(f);
}

static void write_bias_mem(const char *mem_dir, const char *name, const int32_t *bias, int c_out)
{
    FILE *f = open_out(mem_dir, name);
    for (int i = 0; i < c_out; i++)
        put_hex(f, (uint32_t)bias[i], 32);
    fclose(f);
}

/* ================================================================
 * conv_l1 -> pool_l1 -> conv_l2 를 100% handshake 로 돌리며 경계 스트림을 받아 적는다
 * ================================================================ */
static int run_chain(const char *out_dir, int *n_e1, int *n_p1, int *n_o2, long *cycles)
{
    FILE *f_stim = open_out(out_dir, "ce1_stim.mem");
    FILE *f_ce1  = open_out(out_dir, "ce1_out.mem");
    FILE *f_p1   = open_out(out_dir, "pool1_out.mem");
    FILE *f_ce2  = open_out(out_dir, "ce2_out.mem");

    chain_l1_init(g_w1, g_b1, g_s1);
    chain_l2_init(g_w2, g_b2, g_s2);
    pool_l1_init(&g_pool1, POOL_L1_IN_H, POOL_L1_IN_W, POOL_L1_LANES);

    for (int f = 0; f < FRAMES; f++)
        for (int i = 0; i < N_IN1; i++)
            put_hex(f_stim, (uint16_t)g_img[f][i / IN1][i % IN1], 16);

    const int total_in = FRAMES * N_IN1, total_out = FRAMES * N_E2;
    int e1 = 0, q = 0, o = 0, sent = 0;
    long cyc;

    for (cyc = 0; cyc < MAX_CYCLES; cyc++)
    {
        /* 프레임 게이팅: 앞 프레임이 conv_l1 에서 다 빠질 때까지 다음 이미지를 넣지 않는다 */
        int gate  = sent > 0 && sent % N_IN1 == 0 && sent < total_in && !chain_l1_idle();
        int valid = sent < total_in && !gate;
        int i     = sent < total_in ? sent : total_in - 1;

        /* 조합 평가 순서는 test_cnn_chain.c 와 동일 */
        chain_l2_in_t  l2i = {0};
        chain_l2_out_t l2o;
        l2i.out_ready = 1;
        chain_l2_comb(&l2i, &l2o);                                  /* ① */
        uint8_t l2_in_ready = l2o.in_ready;

        chain_l1_in_t  l1i;
        chain_l1_out_t l1o;
        l1i.in_valid  = (uint8_t)valid;
        l1i.pixel_in  = g_img[i / N_IN1][(i % N_IN1) / IN1][(i % N_IN1) % IN1];
        l1i.ch_done   = (uint8_t)(valid && i % N_IN1 == N_IN1 - 1);
        l1i.out_ready = 0;
        chain_l1_comb(&l1i, &l1o);                                  /* ② */

        pool_l1_in_t  pin;
        pool_l1_out_t pout;
        pin.out_valid  = l1o.out_valid;
        pin.ch_done    = l1o.out_ch_done;
        pin.pool_ready = l2_in_ready;
        for (int j = 0; j < 3; j++)
            pin.out_data[j] = l1o.out_data[j];
        pool_l1_comb(&g_pool1, &pin, &pout);                        /* ③ */

        l1i.out_ready = pout.out_ready;
        chain_l1_comb(&l1i, &l1o);                                  /* ④ */

        l2i.in_valid = pout.pool_valid;
        l2i.ch_done  = pout.pool_ch_done;
        for (int j = 0; j < 3; j++)
            l2i.pixel_in[j] = (int16_t)pout.pool_data[j];
        chain_l2_comb(&l2i, &l2o);                                  /* ⑤ */

        if (l1o.out_valid && pout.out_ready)
        {
            uint64_t v = (uint64_t)l1o.out_data[0] | ((uint64_t)l1o.out_data[1] << 16) |
                         ((uint64_t)l1o.out_data[2] << 32) | ((uint64_t)l1o.out_ch_done << 48);
            put_hex(f_ce1, v, 49);
            e1++;
        }
        if (pout.pool_valid && l2_in_ready)
        {
            uint64_t v = (uint64_t)pout.pool_data[0] | ((uint64_t)pout.pool_data[1] << 16) |
                         ((uint64_t)pout.pool_data[2] << 32) | ((uint64_t)pout.pool_ch_done << 48);
            put_hex(f_p1, v, 49);
            q++;
        }
        if (l2o.out_valid)
        {
            put_hex(f_ce2, (uint64_t)l2o.out_data | ((uint64_t)l2o.out_ch_done << 16), 17);
            if (o < FRAMES * N_E2)
                g_l2_out[o] = l2o.out_data;
            o++;
        }

        sent += valid && l1o.in_ready;
        chain_l1_seq();
        pool_l1_seq(&g_pool1);
        chain_l2_seq();

        if (sent == total_in && o == total_out && chain_l1_idle() && chain_l2_idle())
            break;
    }
    fclose(f_stim); fclose(f_ce1); fclose(f_p1); fclose(f_ce2);

    g_n_l2_out = o < FRAMES * N_E2 ? o : FRAMES * N_E2;
    *n_e1 = e1; *n_p1 = q; *n_o2 = o; *cycles = cyc;
    return !(e1 == FRAMES * N_E1 && q == FRAMES * 2 * N_P1 && o == total_out && cyc < MAX_CYCLES);
}

/* ================================================================
 * pool_l2 (11x11x16 -> 5x5x16, LANES 1): conv_l2 출력을 그대로 먹인다
 * ================================================================ */
static int run_pool_l2(const char *out_dir, int *n_out)
{
    FILE *f = open_out(out_dir, "pool2_out.mem");
    pool_l1_init(&g_pool2, POOL_L2_IN_H, POOL_L2_IN_W, POOL_L2_LANES);

    const int n_in = g_n_l2_out, pix = O2 * O2;
    int fed = 0, got = 0;
    long cyc;

    for (cyc = 0; cyc < MAX_CYCLES; cyc++)
    {
        pool_l1_in_t  in;
        pool_l1_out_t out;
        in.out_valid    = (uint8_t)(fed < n_in);
        in.out_data[0]  = fed < n_in ? g_l2_out[fed] : 0;
        in.ch_done      = (uint8_t)(fed < n_in && fed % pix == pix - 1);
        in.pool_ready   = 1;
        for (int j = 1; j < POOL_L1_LANES; j++)
            in.out_data[j] = 0;
        pool_l1_comb(&g_pool2, &in, &out);

        if (out.pool_valid)
        {
            put_hex(f, (uint64_t)out.pool_data[0] | ((uint64_t)out.pool_ch_done << 16), 17);
            got++;
        }
        fed += in.out_valid && out.out_ready;
        pool_l1_seq(&g_pool2);

        if (fed == n_in && got == FRAMES * N_E2P)
            break;
    }
    fclose(f);
    *n_out = got;
    return !(got == FRAMES * N_E2P && cyc < MAX_CYCLES);
}

/* ================================================================ */
int main(int argc, char **argv)
{
    const char *in_dir  = argc > 1 ? argv[1] : "vectors";
    const char *out_dir = argc > 2 ? argv[2] : "../../tb/cnn/vectors";
    const char *mem_dir = argc > 3 ? argv[3] : "../../rtl/cnn/mem";
    char path[512];

    snprintf(path, sizeof path, "%s/conv_l1.txt", in_dir);
    if (read_vec(path, 1))
    {
        fprintf(stderr, "%s: missing or malformed\n", path);
        return 1;
    }
    snprintf(path, sizeof path, "%s/conv_l2.txt", in_dir);
    if (read_vec(path, 2))
    {
        fprintf(stderr, "%s: missing or malformed\n", path);
        return 1;
    }

    write_weight_mem(mem_dir, "conv1_weight.mem", g_w1, C1, 1);
    write_weight_mem(mem_dir, "conv2_weight.mem", g_w2, C2, C1);
    write_bias_mem(mem_dir, "conv1_bias_ce.mem", g_b1, C1);
    write_bias_mem(mem_dir, "conv2_bias_ce.mem", g_b2, C2);

    int  e1 = 0, p1 = 0, o2 = 0, p2 = 0;
    long cycles = 0;
    int  bad = run_chain(out_dir, &e1, &p1, &o2, &cycles);
    bad |= run_pool_l2(out_dir, &p2);

    FILE *fp = open_out(out_dir, "ce_params.txt");
    fprintf(fp,
        "FRAMES=%d\n"
        "CONV_L1 IN=%dx%dx%d OUT=%dx%dx%d PACK=3 SCALE_EXP=%d NSTIM=%d NOUT=%d\n"
        "POOL_L1 IN=%dx%dx%d OUT=%dx%dx%d LANES=%d NOUT=%d\n"
        "CONV_L2 IN=%dx%dx%d OUT=%dx%dx%d PACK=1 SCALE_EXP=%d NOUT=%d\n"
        "POOL_L2 IN=%dx%dx%d OUT=%dx%dx%d LANES=%d NOUT=%d\n"
        "CHAIN_CYCLES=%ld\n",
        FRAMES,
        IN1, IN1, 1, O1, O1, C1, g_s1, FRAMES * N_IN1, e1,
        O1, O1, C1, P1, P1, C1, POOL_L1_LANES, p1,
        P1, P1, C1, O2, O2, C2, g_s2, o2,
        O2, O2, C2, P2, P2, C2, POOL_L2_LANES, p2,
        cycles);
    fclose(fp);

    printf("weight ROM : %s/conv1_weight.mem (%d x %d x 432b), %s/conv2_weight.mem (%d x %d x 432b)\n",
           mem_dir, C1, CE_MAX_GROUPS, mem_dir, C2, CE_MAX_GROUPS);
    printf("bias   ROM : %s/conv{1,2}_bias_ce.mem (%d / %d x 32b)\n", mem_dir, C1, C2);
    printf("conv_l1    : stim %d x 16b, out %d x 49b (expect %d)\n", FRAMES * N_IN1, e1, FRAMES * N_E1);
    printf("pool_l1    : out %d x 49b (expect %d)\n", p1, FRAMES * 2 * N_P1);
    printf("conv_l2    : out %d x 17b (expect %d)\n", o2, FRAMES * N_E2);
    printf("pool_l2    : out %d x 17b (expect %d)\n", p2, FRAMES * N_E2P);
    printf("chain      : %ld cycles -> %s\n", cycles, bad ? "MISMATCHED COUNTS" : "ok");
    return bad;
}
