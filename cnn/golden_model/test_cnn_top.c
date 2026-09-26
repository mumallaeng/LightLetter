/*
 * cnn_top testbench: 입력 이미지 -> cnn_top (conv_l1 -> pool_l1 -> conv_l2 -> pool_l2 -> FC1 -> FC2 -> FC3 -> argmax) -> class
 *
 *   make -f cnn_top.mk test          조건 A B C D 로 실행 (real 이미지, run 마다 2 프레임)
 *   make -f cnn_top.mk log HS=A      logs/cnn_top_real_A.log / .csv (1 클럭 = 1 줄)
 *
 * weight / bias: conv 는 vectors/conv_l1.txt, conv_l2.txt, FC 는 vectors/fc1.txt ~ fc3.txt (팀원 export)
 *
 * 경계 모니터 (기대값 = 입력 이미지로 계산한 정수 레퍼런스 conv1 -> maxpool -> conv2 -> maxpool -> fc1 -> fc2 -> fc3):
 *   L1    conv_l1 -> pool_l1 : conv1 코드, pass 0 raster -> pass 1 raster, pass 마지막 픽셀에 out_ch_done
 *   P1    pool_l1 -> conv_l2 : 2x2 max, pass 마지막 (12,12) 에 pool_ch_done
 *   L2    conv_l2 -> pool_l2 : conv2 코드 (채널 우선), 채널마다 out_ch_done
 *   P2    pool_l2 -> fc_top  : 2x2 max (floor), 채널마다 (4,4) 에 pool_ch_done / 첫 프레임은 Python conv2 + MaxPool
 *   FC1   FC1 -> FC2         : bias + sum(x * w), ReLU, round-half-even >> 15, 120 개
 *   FC2   FC2 -> FC3         : 같은 식 >> 14, 84 개
 *   OUT   FC3 -> argmax      : signed (ReLU 없음) >> 13, logit 36 개
 *   AM    argmax -> top 출력 : 프레임마다 cnn_done 1 번, 36 번째 logit 다음 클럭, cnn_result = 레퍼런스 logit 의 argmax
 *         (= 실제 받은 logit 의 argmax, 같은 값이면 낮은 index)
 *   END   개수, Reorder Buffer overrun, pool ch_err, pool_l2 범위 밖 읽기, FC staging drop / 폭 overflow, timeout
 *   XFC   팀원 fc_top 을 따로 하나 더 만들어 같은 400 개를 넣었을 때 logit 이 cnn_top 과 같은지 (배선 확인)
 *
 * 프레임 게이팅: 다음 이미지는 conv_l1 이 앞 프레임을 다 내보낸 뒤 (cnn_top_l1_idle) 시작한다.
 * 조건: 입력 in_valid 비율 A 100%, B 60%, C 35%, D 15% (argmax logit_ready 가 항상 1 이라 top 에 출력 ready 가 없다)
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cnn_top.h"

#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define MKDIR(p) mkdir(p, 0755)
#endif

#define FRAMES      2
#define IN1         28
#define C1          6
#define O1          26
#define P1          13
#define C2          16
#define O2          11
#define P2          5
#define N_IN1       (IN1 * IN1)             /* 784 */
#define N_PIX1      (O1 * O1)               /* 676 */
#define N_E1        (N_PIX1 * 2)            /* conv_l1 출력 entry / frame : 1352 */
#define N_P1        (P1 * P1)               /* 169 */
#define N_PIX2      (O2 * O2)               /* 121 */
#define N_O2        (N_PIX2 * C2)           /* 1936 */
#define N_PIX3      (P2 * P2)               /* 25 */
#define N_O3        (N_PIX3 * C2)           /* 400 = FC1 입력 */
#define N_FC1       120
#define N_FC2       84
#define N_FC3       36
#define MAX_CYCLES  1000000L
#define MAX_ROM     (FC_MAX_ROM * FC_MAX_LANES)

/* ================================================================
 * data
 * ================================================================ */
static int16_t  g_img[FRAMES][IN1][IN1];
static int16_t  g_w1[C1 * 9], g_w2[C2 * C1 * 9];
static int32_t  g_b1[C1], g_b2[C2];
static uint8_t  g_s1, g_s2;
static uint16_t g_py_conv2[C2][N_PIX2];     /* Python conv2 + ReLU (quantized) */

static fc_param_t g_fcp[3];
static int16_t    g_fc_rom[3][MAX_ROM];
static int32_t    g_fc_bias[3][FC_MAX_N_OUT];

static uint16_t g_ref1[FRAMES][C1][O1][O1]; /* 정수 레퍼런스 */
static uint16_t g_refp[FRAMES][C1][P1][P1];
static uint16_t g_ref2[FRAMES][C2][O2][O2];
static uint16_t g_ref3[FRAMES][N_O3];       /* pool_l2 = FC1 입력 (c*25 + y*5 + x) */
static int16_t  g_reffc[FRAMES][3][FC_MAX_N_OUT];

static uint16_t quantize(int64_t acc, int s)
{
    int64_t q = 0;
    if (acc > 0)
    {
        q = acc >> s;
        if (s > 0)
        {
            int64_t rem = acc & (((int64_t)1 << s) - 1), half = (int64_t)1 << (s - 1);
            if (rem > half || (rem == half && (q & 1)))
                q++;
        }
    }
    return (uint16_t)(q > 32767 ? 32767 : q);
}

/* FC 양자화: (ReLU) -> round-half-even >> s -> clamp. relu = 0 이면 signed */
static int16_t quantize_fc(int64_t acc, int s, int relu)
{
    if (relu && acc < 0)
        acc = 0;
    int64_t one = (int64_t)1 << s, half = one >> 1;
    int64_t q = acc >= 0 ? acc / one : -((-acc + one - 1) / one);      /* floor */
    int64_t rem = acc - q * one;                                        /* 0 .. one-1 */
    if (rem > half || (rem == half && (q & 1)))
        q++;
    if (q > FC_OUT_MAX)
        q = FC_OUT_MAX;
    if (q < FC_OUT_MIN)
        q = FC_OUT_MIN;
    return (int16_t)q;
}

static uint16_t max2(uint16_t a, uint16_t b)
{
    return a > b ? a : b;
}

/* ROM row g*n_out + n, lane i = w[n][g*lanes + i] */
static void ref_fc(int layer, const int16_t *x, int16_t *y)
{
    const fc_param_t *p = &g_fcp[layer];
    for (int n = 0; n < p->n_out; n++)
    {
        int64_t acc = g_fc_bias[layer][n];
        for (int k = 0; k < p->n_in; k++)
            acc += (int64_t)x[k] * g_fc_rom[layer][((k / p->lanes) * p->n_out + n) * p->lanes + k % p->lanes];
        y[n] = quantize_fc(acc, p->scale_exp, p->relu);
    }
}

static void make_reference(void)
{
    for (int f = 0; f < FRAMES; f++)
    {
        for (int oc = 0; oc < C1; oc++)
            for (int y = 0; y < O1; y++)
                for (int x = 0; x < O1; x++)
                {
                    int64_t acc = g_b1[oc];
                    for (int k = 0; k < 9; k++)
                        acc += (int64_t)g_img[f][y + k / 3][x + k % 3] * g_w1[oc * 9 + k];
                    g_ref1[f][oc][y][x] = quantize(acc, g_s1);
                }
        for (int c = 0; c < C1; c++)
            for (int py = 0; py < P1; py++)
                for (int px = 0; px < P1; px++)
                    g_refp[f][c][py][px] = max2(max2(g_ref1[f][c][2 * py][2 * px], g_ref1[f][c][2 * py][2 * px + 1]),
                                                max2(g_ref1[f][c][2 * py + 1][2 * px], g_ref1[f][c][2 * py + 1][2 * px + 1]));
        for (int oc = 0; oc < C2; oc++)
            for (int y = 0; y < O2; y++)
                for (int x = 0; x < O2; x++)
                {
                    int64_t acc = g_b2[oc];
                    for (int ic = 0; ic < C1; ic++)
                        for (int k = 0; k < 9; k++)
                            acc += (int64_t)g_refp[f][ic][y + k / 3][x + k % 3] * g_w2[(oc * C1 + ic) * 9 + k];
                    g_ref2[f][oc][y][x] = quantize(acc, g_s2);
                }
        for (int c = 0; c < C2; c++)
            for (int py = 0; py < P2; py++)
                for (int px = 0; px < P2; px++)
                    g_ref3[f][c * N_PIX3 + py * P2 + px] =
                        max2(max2(g_ref2[f][c][2 * py][2 * px], g_ref2[f][c][2 * py][2 * px + 1]),
                             max2(g_ref2[f][c][2 * py + 1][2 * px], g_ref2[f][c][2 * py + 1][2 * px + 1]));

        int16_t x0[N_O3];
        for (int i = 0; i < N_O3; i++)
            x0[i] = (int16_t)g_ref3[f][i];
        ref_fc(0, x0, g_reffc[f][0]);
        ref_fc(1, g_reffc[f][0], g_reffc[f][1]);
        ref_fc(2, g_reffc[f][1], g_reffc[f][2]);
    }
}

static int read_vec(const char *path, int layer)
{
    FILE *f = fopen(path, "r");
    int hdr[6], v, ok = f != NULL;

    for (int i = 0; ok && i < 6; i++)
        ok = fscanf(f, "%d", &hdr[i]) == 1;
    if (!ok)
        return 1;
    int c_in = hdr[0], h = hdr[1], w = hdr[2], c_out = hdr[3], oh = h - 2, n = oh * oh;
    for (int i = 0; ok && i < c_in * h * w; i++)
    {
        ok = fscanf(f, "%d", &v) == 1;
        if (layer == 1)
        {
            g_img[0][i / w][i % w] = (int16_t)v;
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
    for (int i = 0; ok && i < c_out * n; i++)
    {
        ok = fscanf(f, "%d", &v) == 1;
        if (layer == 2)
            g_py_conv2[i / n][i % n] = (uint16_t)v;
    }
    fclose(f);
    if (layer == 1)
        g_s1 = (uint8_t)hdr[5];
    else
        g_s2 = (uint8_t)hdr[5];
    return !ok;
}

/* vectors/fcK.txt (export_fc_vectors.py): header, bias, 입력 (안 씀), ROM rows, 기대값 (안 씀) */
static int read_fc_vec(const char *path, int li)
{
    FILE *f = fopen(path, "r");
    unsigned lay, n_in, n_out, lanes, chunk, scale_exp, relu;
    long v;
    int ok = f != NULL;

    ok = ok && fscanf(f, "%u %u %u %u %u %u %u", &lay, &n_in, &n_out, &lanes, &chunk, &scale_exp, &relu) == 7;
    if (!ok)
        return 1;
    fc_param_t *p = &g_fcp[li];
    p->layer     = (uint8_t)lay;
    p->n_in      = (uint16_t)n_in;
    p->n_out     = (uint8_t)n_out;
    p->lanes     = (uint8_t)lanes;
    p->num_chunk = (uint8_t)chunk;
    p->acc_w     = (lay == 1) ? 40 : 38;            /* test_fc.c 와 같은 값 */
    p->scale_exp = (uint8_t)scale_exp;
    p->relu      = (uint8_t)relu;
    for (unsigned i = 0; ok && i < n_out; i++)
        ok = fscanf(f, "%ld", &v) == 1, g_fc_bias[li][i] = (int32_t)v;
    for (unsigned i = 0; ok && i < n_in; i++)
        ok = fscanf(f, "%ld", &v) == 1;
    for (unsigned i = 0; ok && i < chunk * n_out * lanes; i++)
        ok = fscanf(f, "%ld", &v) == 1, g_fc_rom[li][i] = (int16_t)v;
    fclose(f);
    return !ok;
}

/* Python conv2 (+ ReLU, quantized) 에 2x2 MaxPool */
static uint16_t py_pool2(int c, int py, int px)
{
    uint16_t m = 0;
    for (int dy = 0; dy < 2; dy++)
        for (int dx = 0; dx < 2; dx++)
            m = max2(m, g_py_conv2[c][(2 * py + dy) * O2 + 2 * px + dx]);
    return m;
}

/* ================================================================
 * monitors
 * ================================================================ */
enum { M_L1, M_P1, M_L2, M_P2, M_FC1, M_FC2, M_OUT, M_AM, M_END, M_XFC, N_MON };
static const char *g_mon_name[N_MON] = {"L1", "P1", "L2", "P2", "FC1", "FC2", "OUT", "AM", "END", "XFC"};
typedef struct { long checks, errs; int shown; } mon_t;
static mon_t g_mon[N_MON];
static long  g_cycle;

static int expect(int id, int ok, const char *fmt, ...)
{
    mon_t *m = &g_mon[id];
    m->checks++;
    if (ok)
        return 1;
    m->errs++;
    if (m->shown++ < 4)
    {
        va_list ap;
        printf("      [%s] cycle %ld: ", g_mon_name[id], g_cycle);
        va_start(ap, fmt);
        vprintf(fmt, ap);
        va_end(ap);
        printf("\n");
    }
    return 0;
}

static long total_errs(void)
{
    long e = 0;
    for (int id = 0; id < N_MON; id++)
        e += g_mon[id].errs;
    return e;
}

/* ================================================================
 * handshake
 * ================================================================ */
typedef struct { const char *name; int in_pct; uint32_t seed; } hs_cfg_t;
static const hs_cfg_t g_hs[] = {{"A", 100, 11}, {"B", 60, 22}, {"C", 35, 33}, {"D", 15, 44}};
#define N_HS ((int)(sizeof g_hs / sizeof g_hs[0]))

static uint32_t rnd_next(uint32_t *s)
{
    *s ^= *s << 13;
    *s ^= *s >> 17;
    *s ^= *s << 5;
    return *s;
}

/* ================================================================
 * 교차 확인: 팀원 fc_top 단독 (cnn_top 밖) 에 같은 400 개를 넣어 logit 을 받는다
 * ================================================================ */
static fc_top_t g_fc_alone;

static int run_fc_alone(const uint16_t *x, int16_t *logit)
{
    const int16_t *rom[3]  = {g_fc_rom[0], g_fc_rom[1], g_fc_rom[2]};
    const int32_t *bias[3] = {g_fc_bias[0], g_fc_bias[1], g_fc_bias[2]};
    int fed = 0, n = 0;

    fc_top_init(&g_fc_alone, g_fcp, rom, bias);
    for (long c = 0; c < 100000 && n < N_FC3; c++)
    {
        fc_top_in_t  in;
        fc_top_out_t out;
        in.fc_in_data  = fed < N_O3 ? x[fed] : 0;
        in.fc_in_valid = fed < N_O3;
        in.logit_ready = 1;
        fc_top_comb(&g_fc_alone, &in, &out);
        if (in.fc_in_valid && out.fc_in_ready)
            fed++;
        if (out.logit_valid)
            logit[n++] = out.logit_data;
        fc_top_seq(&g_fc_alone);
    }
    return n;
}

/* ================================================================
 * one run
 * ================================================================ */
typedef struct
{
    long cycles, gate_cycles;
    int  py_exact, py_diff;
    int  argmax[FRAMES];
    long first_logit[FRAMES], last_logit[FRAMES];
    long done_cycle[FRAMES];
    int  cnn_result[FRAMES];
} run_res_t;

static cnn_top_t g_top;
static uint16_t  g_got_p2[FRAMES][N_O3];     /* cnn_top 안 pool_l2 -> fc_top 으로 넘어간 값 */
static int16_t   g_got_logit[FRAMES][N_FC3];

static int run(const hs_cfg_t *hs, FILE *log, FILE *csv, run_res_t *res)
{
    const int total_in = FRAMES * N_IN1, total_out = FRAMES * N_FC3;
    const int16_t *rom[3]  = {g_fc_rom[0], g_fc_rom[1], g_fc_rom[2]};
    const int32_t *bias[3] = {g_fc_bias[0], g_fc_bias[1], g_fc_bias[2]};
    uint32_t seed = hs->seed;
    int sent = 0, cur_valid = 0, e1 = 0, q1 = 0, e2 = 0, q2 = 0, n1 = 0, n2 = 0, o = 0, nd = 0;
    long last_logit_cycle = -10;

    memset(g_mon, 0, sizeof g_mon);
    memset(res, 0, sizeof *res);
    cnn_top_init(&g_top, g_w1, g_b1, g_s1, g_w2, g_b2, g_s2, g_fcp, rom, bias);

    if (log)
        fprintf(log,
            "# cnn_top clock log | real image | handshake %s: image in_valid %d%% | %d frames\n"
            "#  IMG   in_valid/in_ready, 받은 픽셀 f (y,x)  ('gate' = 앞 프레임이 conv_l1 을 빠져나가길 기다리는 중)\n"
            "#  L1    conv_l1 -> pool_l1  f p (y,x)        P1  pool_l1 -> conv_l2  f p (py,px)\n"
            "#  L2    conv_l2 -> pool_l2  f och (y,x)      P2  pool_l2 -> fc_top   f och (py,px) = data\n"
            "#  FC    FC1 -> FC2 f n1 = v / FC2 -> FC3 f n2 = v / logit (-> argmax) f n = v\n"
            "#  AM    argmax 출력: cnn_done 펄스 클럭에 f = class (cnn_result)\n"
            "#  * = ch_done,  !! = 이 클럭에 모니터 오류\n#\n"
            "%6s | %-3s %-11s | %-15s | %-15s | %-16s | %-22s | %-24s | %s\n",
            hs->name, hs->in_pct, FRAMES, "cycle", "IMG", "pixel", "L1 -> P1", "P1 -> L2",
            "L2 -> P2", "P2 -> FC", "FC", "AM");
    if (csv)
        fprintf(csv, "cycle,in_valid,in_ready,gate,img_frame,img_y,img_x,"
                     "l1_tx,l1_frame,l1_pass,l1_y,l1_x,l1_ch_done,"
                     "p1_tx,p1_frame,p1_pass,p1_py,p1_px,p1_ch_done,"
                     "l2_tx,l2_frame,l2_och,l2_y,l2_x,l2_ch_done,"
                     "p2_tx,p2_frame,p2_och,p2_py,p2_px,p2_data,p2_ch_done,"
                     "fc1_tx,fc1_frame,fc1_n,fc1_data,fc2_tx,fc2_frame,fc2_n,fc2_data,"
                     "logit_tx,logit_frame,logit_n,logit_data,cnn_done,cnn_result,monitor_err\n");

    for (g_cycle = 0; g_cycle < MAX_CYCLES; g_cycle++)
    {
        long errs_before = total_errs();

        /* ---------------- 입력 이미지 소스 (프레임 게이팅) / 최종 sink ---------------- */
        int gate = sent > 0 && sent % N_IN1 == 0 && sent < total_in && !cur_valid && !cnn_top_l1_idle();
        if (gate)
            res->gate_cycles++;
        if (!cur_valid && sent < total_in && !gate)
            cur_valid = (int)(rnd_next(&seed) % 100) < hs->in_pct;
        int i = sent < total_in ? sent : total_in - 1, fi = i / N_IN1, pix = i % N_IN1;

        cnn_top_in_t  ti;
        cnn_top_out_t to;
        ti.in_valid  = (uint8_t)cur_valid;
        ti.pixel_in  = g_img[fi][pix / IN1][pix % IN1];
        ti.ch_done   = (uint8_t)(cur_valid && pix == N_IN1 - 1);
        cnn_top_comb(&g_top, &ti, &to);

        /* ================= monitors ================= */
        const cnn_top_t *m = &g_top;
        int lf = 0, lp = 0, lx = 0, qf = 0, qp = 0, qq = 0, af = 0, ac = 0, ax = 0, bf = 0, bx = 0;
        int f1 = 0, k1 = 0, f2 = 0, k2 = 0, of = 0, ok_ = 0, df = 0;

        if (m->tx_l1)
        {
            lf = e1 / N_E1; lp = (e1 / N_PIX1) % 2; lx = e1 % N_PIX1;
            for (int j = 0; j < 3; j++)
                expect(M_L1, lf < FRAMES && m->l1_o.out_data[j] == g_ref1[lf][lp * 3 + j][lx / O1][lx % O1],
                       "frame %d pass %d pixel %d lane %d: %u expect %u", lf, lp, lx, j, m->l1_o.out_data[j],
                       lf < FRAMES ? g_ref1[lf][lp * 3 + j][lx / O1][lx % O1] : 0);
            expect(M_L1, m->l1_o.out_ch_done == (lx == N_PIX1 - 1), "out_ch_done %d at pixel %d", m->l1_o.out_ch_done, lx);
            e1++;
        }
        if (m->tx_p1)
        {
            qf = q1 / (2 * N_P1); qp = (q1 / N_P1) % 2; qq = q1 % N_P1;
            for (int j = 0; j < 3; j++)
                expect(M_P1, qf < FRAMES && m->p1_o.pool_data[j] == g_refp[qf][qp * 3 + j][qq / P1][qq % P1],
                       "frame %d pass %d (%d,%d) lane %d: %u expect %u", qf, qp, qq / P1, qq % P1, j,
                       m->p1_o.pool_data[j], qf < FRAMES ? g_refp[qf][qp * 3 + j][qq / P1][qq % P1] : 0);
            expect(M_P1, m->p1_o.pool_ch_done == (qq == N_P1 - 1), "pool_ch_done %d at %d", m->p1_o.pool_ch_done, qq);
            q1++;
        }
        if (m->tx_l2)
        {
            af = e2 / N_O2; ac = (e2 / N_PIX2) % C2; ax = e2 % N_PIX2;
            expect(M_L2, af < FRAMES && m->l2_o.out_data == g_ref2[af][ac][ax / O2][ax % O2],
                   "frame %d och %d pos %d: %u expect %u", af, ac, ax, m->l2_o.out_data,
                   af < FRAMES ? g_ref2[af][ac][ax / O2][ax % O2] : 0);
            expect(M_L2, m->l2_o.out_ch_done == (ax == N_PIX2 - 1), "out_ch_done %d at och %d pos %d",
                   m->l2_o.out_ch_done, ac, ax);
            e2++;
        }
        if (m->tx_p2)
        {
            bf = q2 / N_O3; bx = q2 % N_O3;
            expect(M_P2, bf < FRAMES && m->p2_o.pool_data == g_ref3[bf][bx], "frame %d och %d (%d,%d): %u expect %u", bf,
                   bx / N_PIX3, (bx % N_PIX3) / P2, bx % P2, m->p2_o.pool_data, bf < FRAMES ? g_ref3[bf][bx] : 0);
            expect(M_P2, m->p2_o.pool_ch_done == (bx % N_PIX3 == N_PIX3 - 1), "pool_ch_done %d at %d",
                   m->p2_o.pool_ch_done, bx);
            if (bf < FRAMES)
                g_got_p2[bf][bx] = m->p2_o.pool_data;
            if (bf == 0)
            {
                uint16_t py = py_pool2(bx / N_PIX3, (bx % N_PIX3) / P2, bx % P2);
                res->py_exact += m->p2_o.pool_data == py;
                res->py_diff  += m->p2_o.pool_data != py;
            }
            q2++;
        }
        if (m->tx_fc1)
        {
            f1 = n1 / N_FC1; k1 = n1 % N_FC1;
            expect(M_FC1, f1 < FRAMES && m->fc.w_l1_data == g_reffc[f1][0][k1], "frame %d neuron %d: %d expect %d", f1,
                   k1, m->fc.w_l1_data, f1 < FRAMES ? g_reffc[f1][0][k1] : 0);
            n1++;
        }
        if (m->tx_fc2)
        {
            f2 = n2 / N_FC2; k2 = n2 % N_FC2;
            expect(M_FC2, f2 < FRAMES && m->fc.w_l2_data == g_reffc[f2][1][k2], "frame %d neuron %d: %d expect %d", f2,
                   k2, m->fc.w_l2_data, f2 < FRAMES ? g_reffc[f2][1][k2] : 0);
            n2++;
        }
        if (to.cnn_done)
        {
            /* 36 번째 logit 을 받은 다음 클럭 (argmax 는 이전 클럭까지의 logit 으로 결과를 낸다) */
            df = nd;
            int exp_cls = df < FRAMES ? argmax_ref(g_reffc[df][2], N_FC3) : -1;
            expect(M_AM, df < FRAMES, "extra cnn_done (frame %d)", df);
            expect(M_AM, o == (df + 1) * N_FC3 && last_logit_cycle == g_cycle - 1,
                   "cnn_done frame %d: %d logits so far, last logit at cycle %ld", df, o, last_logit_cycle);
            expect(M_AM, to.cnn_result == exp_cls, "frame %d cnn_result %u expect %d", df, to.cnn_result, exp_cls);
            if (df < FRAMES)
            {
                expect(M_AM, to.cnn_result == argmax_ref(g_got_logit[df], N_FC3),
                       "frame %d cnn_result %u, argmax of received logits %u", df, to.cnn_result,
                       argmax_ref(g_got_logit[df], N_FC3));
                res->done_cycle[df] = g_cycle;
                res->cnn_result[df] = to.cnn_result;
            }
            nd++;
        }
        if (m->tx_logit)
        {
            int16_t lv = m->fc_o.logit_data;
            of = o / N_FC3; ok_ = o % N_FC3;
            expect(M_OUT, of < FRAMES && lv == g_reffc[of][2][ok_], "frame %d logit %d: %d expect %d", of, ok_,
                   lv, of < FRAMES ? g_reffc[of][2][ok_] : 0);
            last_logit_cycle = g_cycle;
            if (of < FRAMES)
            {
                g_got_logit[of][ok_] = lv;
                if (ok_ == 0)
                    res->first_logit[of] = g_cycle;
                res->last_logit[of] = g_cycle;
            }
            o++;
        }

        /* ================= log ================= */
        long err = total_errs() - errs_before;
        if (log)
        {
            char a[32] = ".", b[32] = ".", c[32] = ".", d[32] = ".", e[40] = ".", g[96] = "", h[40] = ".";
            if (m->tx_in)
                snprintf(a, sizeof a, "f%d (%2d,%2d)", fi, pix / IN1, pix % IN1);
            else if (gate)
                snprintf(a, sizeof a, "gate");
            if (m->tx_l1)
                snprintf(b, sizeof b, "f%d p%d (%2d,%2d)%s", lf, lp, lx / O1, lx % O1, m->l1_o.out_ch_done ? " *" : "");
            if (m->tx_p1)
                snprintf(c, sizeof c, "f%d p%d (%2d,%2d)%s", qf, qp, qq / P1, qq % P1, m->p1_o.pool_ch_done ? " *" : "");
            if (m->tx_l2)
                snprintf(d, sizeof d, "f%d o%-2d (%2d,%2d)%s", af, ac, ax / O2, ax % O2, m->l2_o.out_ch_done ? " *" : "");
            if (m->tx_p2)
                snprintf(e, sizeof e, "f%d o%-2d (%d,%d) = %u%s", bf, bx / N_PIX3, (bx % N_PIX3) / P2, bx % P2,
                         m->p2_o.pool_data, m->p2_o.pool_ch_done ? " *" : "");
            if (m->tx_fc1)
                snprintf(g + strlen(g), sizeof g - strlen(g), "fc1 f%d n%-3d = %-6d ", f1, k1, m->fc.w_l1_data);
            if (m->tx_fc2)
                snprintf(g + strlen(g), sizeof g - strlen(g), "fc2 f%d n%-2d = %-6d ", f2, k2, m->fc.w_l2_data);
            if (m->tx_logit)
                snprintf(g + strlen(g), sizeof g - strlen(g), "logit f%d n%-2d = %d", of, ok_, m->fc_o.logit_data);
            if (to.cnn_done)
                snprintf(h, sizeof h, "cnn_done f%d = class %u", df, to.cnn_result);
            fprintf(log, "%6ld | %d/%d %-11s | %-15s | %-15s | %-16s | %-22s | %-24s | %s%s\n", g_cycle, cur_valid,
                    to.in_ready, a, b, c, d, e, g[0] ? g : ".", h, err ? "  !!" : "");
        }
        if (csv)
        {
            fprintf(csv, "%ld,%d,%d,%d,", g_cycle, cur_valid, to.in_ready, gate);
            if (m->tx_in) fprintf(csv, "%d,%d,%d,", fi, pix / IN1, pix % IN1); else fprintf(csv, ",,,");
            fprintf(csv, "%d,", m->tx_l1);
            if (m->tx_l1) fprintf(csv, "%d,%d,%d,%d,%d,", lf, lp, lx / O1, lx % O1, m->l1_o.out_ch_done); else fprintf(csv, ",,,,,");
            fprintf(csv, "%d,", m->tx_p1);
            if (m->tx_p1) fprintf(csv, "%d,%d,%d,%d,%d,", qf, qp, qq / P1, qq % P1, m->p1_o.pool_ch_done); else fprintf(csv, ",,,,,");
            fprintf(csv, "%d,", m->tx_l2);
            if (m->tx_l2) fprintf(csv, "%d,%d,%d,%d,%d,", af, ac, ax / O2, ax % O2, m->l2_o.out_ch_done); else fprintf(csv, ",,,,,");
            fprintf(csv, "%d,", m->tx_p2);
            if (m->tx_p2)
                fprintf(csv, "%d,%d,%d,%d,%u,%d,", bf, bx / N_PIX3, (bx % N_PIX3) / P2, bx % P2, m->p2_o.pool_data,
                        m->p2_o.pool_ch_done);
            else fprintf(csv, ",,,,,,");
            fprintf(csv, "%d,", m->tx_fc1);
            if (m->tx_fc1) fprintf(csv, "%d,%d,%d,", f1, k1, m->fc.w_l1_data); else fprintf(csv, ",,,");
            fprintf(csv, "%d,", m->tx_fc2);
            if (m->tx_fc2) fprintf(csv, "%d,%d,%d,", f2, k2, m->fc.w_l2_data); else fprintf(csv, ",,,");
            fprintf(csv, "%d,", m->tx_logit);
            if (m->tx_logit) fprintf(csv, "%d,%d,%d,", of, ok_, m->fc_o.logit_data); else fprintf(csv, ",,,");
            fprintf(csv, "%d,%u,%ld\n", to.cnn_done, to.cnn_result, err);
        }

        /* ---------------- posedge clk ---------------- */
        if (m->tx_in)
        {
            sent++;
            cur_valid = 0;
        }
        cnn_top_seq(&g_top);

        if (sent == total_in && nd == FRAMES && cnn_top_idle())
            break;
    }

    chain_probe_t p1, p2;
    chain_l1_probe(&p1);
    chain_l2_probe(&p2);
    expect(M_END, g_cycle < MAX_CYCLES, "timeout: image %d/%d, logit %d/%d", sent, total_in, o, total_out);
    expect(M_END, e1 == FRAMES * N_E1, "conv_l1 entries %d expect %d", e1, FRAMES * N_E1);
    expect(M_END, q1 == FRAMES * 2 * N_P1, "pool_l1 outputs %d expect %d", q1, FRAMES * 2 * N_P1);
    expect(M_END, e2 == FRAMES * N_O2, "conv_l2 outputs %d expect %d", e2, FRAMES * N_O2);
    expect(M_END, q2 == FRAMES * N_O3, "pool_l2 outputs %d expect %d", q2, FRAMES * N_O3);
    expect(M_END, n1 == FRAMES * N_FC1, "FC1 outputs %d expect %d", n1, FRAMES * N_FC1);
    expect(M_END, n2 == FRAMES * N_FC2, "FC2 outputs %d expect %d", n2, FRAMES * N_FC2);
    expect(M_END, o == total_out, "logits %d expect %d", o, total_out);
    expect(M_END, nd == FRAMES, "cnn_done pulses %d expect %d", nd, FRAMES);
    expect(M_END, p1.rb_overrun == 0 && p2.rb_overrun == 0, "reorder overrun l1 %u l2 %u", p1.rb_overrun, p2.rb_overrun);
    expect(M_END, g_top.pool1.ctrl.dbg_ch_err_cnt == 0 && g_top.pool2.ctrl.dbg_ch_err_cnt == 0,
           "pool ch_err l1 %u l2 %u", g_top.pool1.ctrl.dbg_ch_err_cnt, g_top.pool2.ctrl.dbg_ch_err_cnt);
    expect(M_END, g_top.pool2.dbg_oob_used == 0, "pool_l2 out-of-range pool_buf read used by pool_valid %u times",
           g_top.pool2.dbg_oob_used);
    expect(M_END, g_top.pool2.dbg_oob_rd == FRAMES * C2 * O2, "pool_l2 out-of-range reads %u expect %d (col 10)",
           g_top.pool2.dbg_oob_rd, FRAMES * C2 * O2);
    for (int l = 0; l < 3; l++)
    {
        const fc_layer_t *fl = &g_top.fc.l[l];
        uint32_t over = fl->p.relu ? fl->u_relu_quant.u_out_reorder.dbg_overrun_cnt
                                   : fl->u_quant_signed.u_out_reorder.dbg_overrun_cnt;
        expect(M_END, fl->u_staging.dbg_drop_cnt == 0 && fl->u_mac.dbg_ch_ovf_cnt == 0 &&
                      fl->u_output_buffer.dbg_acc_ovf_cnt == 0 && over == 0,
               "FC%d staging drop %u, CH_W ovf %u, ACC_W ovf %u, reorder overrun %u", l + 1, fl->u_staging.dbg_drop_cnt,
               fl->u_mac.dbg_ch_ovf_cnt, fl->u_output_buffer.dbg_acc_ovf_cnt, over);
    }

    /* 교차 확인: 같은 400 개를 팀원 fc_top 단독에 넣은 logit == cnn_top logit */
    for (int f = 0; f < FRAMES; f++)
    {
        int16_t alone[N_FC3];
        int n = run_fc_alone(g_got_p2[f], alone);
        expect(M_XFC, n == N_FC3, "frame %d: fc_top alone gave %d logits", f, n);
        for (int k = 0; k < N_FC3 && k < n; k++)
            expect(M_XFC, alone[k] == g_got_logit[f][k], "frame %d logit %d: cnn_top %d, fc_top alone %d", f, k,
                   g_got_logit[f][k], alone[k]);
        int best = 0;
        for (int k = 1; k < N_FC3; k++)
            if (g_got_logit[f][k] > g_got_logit[f][best])
                best = k;
        res->argmax[f] = best;
    }

    res->cycles = g_cycle;
    return total_errs() != 0;
}

/* ================================================================
 * main
 * ================================================================ */
int main(int argc, char **argv)
{
    const char *sel_hs = NULL;
    int want_log = 0, fail_runs = 0, runs = 0;

    for (int i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "-l"))
            want_log = 1;
        else if (!strcmp(argv[i], "-h") && i + 1 < argc)
            sel_hs = argv[++i];
    }
    if (want_log && !sel_hs)
        sel_hs = "A";
    if (sel_hs && !strcmp(sel_hs, "all"))
        sel_hs = NULL;
    if (want_log)
        MKDIR("logs");

    if (read_vec("vectors/conv_l1.txt", 1) || read_vec("vectors/conv_l2.txt", 2) ||
        read_fc_vec("vectors/fc1.txt", 0) || read_fc_vec("vectors/fc2.txt", 1) || read_fc_vec("vectors/fc3.txt", 2))
    {
        printf("vectors/conv_l1.txt, conv_l2.txt or fc1..3.txt missing or malformed\n");
        return 1;
    }
    make_reference();

    printf("cnn_top: image 28x28 -> conv_l1 -> pool_l1 -> conv_l2 -> pool_l2 -> FC1 -> FC2 -> FC3 -> argmax (%d classes), "
           "real image, %d frames\n\n", N_FC3, FRAMES);
    for (int h = 0; h < N_HS; h++)
    {
        if (sel_hs && strcmp(sel_hs, g_hs[h].name))
            continue;
        FILE *log = NULL, *csv = NULL;
        char name[64];
        if (want_log)
        {
            snprintf(name, sizeof name, "logs/cnn_top_real_%s.log", g_hs[h].name);
            log = fopen(name, "w");
            snprintf(name, sizeof name, "logs/cnn_top_real_%s.csv", g_hs[h].name);
            csv = fopen(name, "w");
        }
        run_res_t r;
        int fail = run(&g_hs[h], log, csv, &r);
        if (log) fclose(log);
        if (csv) fclose(csv);

        printf("[%s] %s (image in_valid %3d%%) |", fail ? "FAIL" : " ok ", g_hs[h].name, g_hs[h].in_pct);
        for (int id = 0; id < N_MON; id++)
            printf(" %s %ld%s", g_mon_name[id], g_mon[id].checks, g_mon[id].errs ? "!" : "");
        printf(" | %ld cyc (gate %ld)\n", r.cycles, r.gate_cycles);
        printf("       frame 0 pool_l2 vs Python conv2 + MaxPool: %d exact / %d diff\n", r.py_exact, r.py_diff);
        for (int f = 0; f < FRAMES; f++)
            printf("       frame %d: logits cycle %ld~%ld, cnn_done cycle %ld -> cnn_result = class %d (reference %d)\n", f,
                   r.first_logit[f], r.last_logit[f], r.done_cycle[f], r.cnn_result[f], argmax_ref(g_reffc[f][2], N_FC3));
        if (want_log)
            printf("       -> logs/cnn_top_real_%s.log / .csv\n", g_hs[h].name);
        fail_runs += fail;
        runs++;
    }
    printf("\n%d / %d runs passed -> %s\n", runs - fail_runs, runs, fail_runs ? "FAIL" : "ALL PASS");
    return fail_runs ? 1 : 0;
}
