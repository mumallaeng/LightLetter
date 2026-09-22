/*
 * cnn_chain testbench: 입력 이미지 -> conv_l1 -> pool_l1 -> conv_l2 -> 출력
 *
 *   make -f cnn_chain.mk test          조건 A B C D 로 실행 (real 이미지, run 마다 2 프레임)
 *   make -f cnn_chain.mk log HS=A      logs/cnn_chain_real_A.log / .csv (1 클럭 = 1 줄)
 *   make -f cnn_chain.mk out HS=A      + conv_l2 최종 출력 맵 logs/cnn_chain_real_A_f0_out.txt
 *
 * 연결: conv_l1 Reorder Buffer 출력 (group 0 = och0~2 전 픽셀 -> group 1 = och3~5, group 마다 out_ch_done)
 *       -> pool_l1 (out_valid / out_data0..2 / ch_done, out_ready) -> conv_l2 (in_valid / pixel_in0..2 / ch_done, in_ready)
 *       -> conv_l2 Reorder Buffer 출력 (och 0 11x11 -> ... -> och 15)
 *
 * 경계 모니터 (기대값 = 입력 이미지로 계산한 정수 레퍼런스 conv1 -> maxpool -> conv2):
 *   L1    conv_l1 출력 = pool_l1 입력 : conv1 코드, pass 0 raster -> pass 1 raster, pass 마지막 픽셀에 ch_done
 *   POOL  pool_l1 출력 = conv_l2 입력 : 2x2 max, pool_ch_done / 첫 프레임은 Python conv1 + MaxPool 과 비교
 *   L2    conv_l2 출력                : conv2 코드 (채널 우선), 채널마다 out_ch_done / 첫 프레임은 Python conv2 + ReLU
 *   END   개수, Reorder Buffer overrun, pool ch_err, timeout
 *
 * 프레임 게이팅: 팀원 out_reorder 는 한 프레임만 담고 conv FSM 으로 가는 backpressure 가 없다.
 *   그래서 다음 이미지는 conv_l1 이 앞 프레임을 다 내보낸 뒤 (chain_l1_idle) 시작한다.
 *   conv_l2 쪽도 같은 제약이 있어 -g 2 로 conv_l2 까지 비는 것을 기다리게 할 수 있다 (기본 1 = conv_l1 만).
 *
 * 조건: A 입력 연속 / 출력 ready 100%, B 입력 60%, C 출력 ready 35%, D 입력 35% / 출력 45%
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cnn_chain.h"
#include "pool_l1.h"

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
#define N_IN1       (IN1 * IN1)             /* 784 */
#define N_PIX1      (O1 * O1)               /* 676 */
#define N_E1        (N_PIX1 * 2)            /* conv_l1 출력 entry / frame : 1352 */
#define N_P1        (P1 * P1)               /* 169 */
#define N_PIX2      (O2 * O2)               /* 121 */
#define N_O2        (N_PIX2 * C2)           /* 1936 */
#define MAX_CYCLES  1000000L

/* ================================================================
 * data
 * ================================================================ */
static int16_t  g_img[FRAMES][IN1][IN1];
static int16_t  g_w1[C1 * 9], g_w2[C2 * C1 * 9];
static int32_t  g_b1[C1], g_b2[C2];
static uint8_t  g_s1, g_s2;
static uint16_t g_py_pool[C1][N_P1];        /* Python conv1 + MaxPool */
static uint16_t g_py_conv2[C2][N_PIX2];     /* Python conv2 + ReLU (quantized) */

static uint16_t g_ref1[FRAMES][C1][O1][O1]; /* 정수 레퍼런스 */
static uint16_t g_refp[FRAMES][C1][P1][P1];
static uint16_t g_ref2[FRAMES][C2][N_PIX2];

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

static uint16_t max2(uint16_t a, uint16_t b)
{
    return a > b ? a : b;
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
                    g_ref2[f][oc][y * O2 + x] = quantize(acc, g_s2);
                }
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
    int c_in = hdr[0], h = hdr[1], w = hdr[2], c_out = hdr[3], oh = h - 2, n = oh * oh, pn = (oh / 2) * (oh / 2);
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
    for (int i = 0; ok && i < c_out * pn; i++)
    {
        ok = fscanf(f, "%d", &v) == 1;
        if (layer == 1)
            g_py_pool[i / pn][i % pn] = (uint16_t)v;
    }
    fclose(f);
    if (layer == 1)
        g_s1 = (uint8_t)hdr[5];
    else
        g_s2 = (uint8_t)hdr[5];
    return !ok;
}

/* ================================================================
 * monitors
 * ================================================================ */
enum { M_L1, M_POOL, M_L2, M_END, N_MON };
static const char *g_mon_name[N_MON] = {"L1", "POOL", "L2", "END"};
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

/* 프레임별 단계 시간 (첫 / 마지막 cycle) */
enum { T_IN, T_P0, T_P1, T_POOL, T_L2, N_T };
static const char *g_t_short[N_T] = {"in", "l1p0", "l1p1", "pool", "l2"};
static long g_t[FRAMES][N_T][2];

static void mark(int f, int t)
{
    if (f >= FRAMES)
        return;
    if (g_t[f][t][0] < 0)
        g_t[f][t][0] = g_cycle;
    g_t[f][t][1] = g_cycle;
}

/* ================================================================
 * handshake
 * ================================================================ */
typedef struct { const char *name; int in_pct, ready_pct; uint32_t seed; } hs_cfg_t;
static const hs_cfg_t g_hs[] = {{"A", 100, 100, 11}, {"B", 60, 100, 22}, {"C", 100, 35, 33}, {"D", 35, 45, 44}};
#define N_HS ((int)(sizeof g_hs / sizeof g_hs[0]))

static uint32_t rnd_next(uint32_t *s)
{
    *s ^= *s << 13;
    *s ^= *s >> 17;
    *s ^= *s << 5;
    return *s;
}

/* ================================================================
 * one run
 * ================================================================ */
typedef struct
{
    long     cycles, gate_cycles;
    int      pool_py_exact, pool_py_diff, l2_py_exact, l2_py_diff;
    uint16_t l1_rb_peak, l2_rb_peak;
} run_res_t;

static pool_l1_t g_pool;
static int       g_gate_level = 1;      /* 1: conv_l1 이 비면 다음 프레임, 2: conv_l2 까지 비면 */

static int run(const hs_cfg_t *hs, FILE *log, FILE *csv, run_res_t *res)
{
    const int total_in = FRAMES * N_IN1, total_out = FRAMES * N_O2;
    uint32_t seed = hs->seed;
    int sent = 0, cur_valid = 0, e1 = 0, q = 0, o = 0;

    memset(g_mon, 0, sizeof g_mon);
    memset(res, 0, sizeof *res);
    memset(g_t, -1, sizeof g_t);
    chain_l1_init(g_w1, g_b1, g_s1);
    chain_l2_init(g_w2, g_b2, g_s2);
    pool_l1_init(&g_pool, POOL_L1_IN_H, POOL_L1_IN_W, POOL_L1_LANES);

    if (log)
        fprintf(log,
            "# cnn_chain clock log | real image | handshake %s: image in_valid %d%% / final out_ready %d%% | %d frames\n"
            "#  IMG   입력 in_valid/in_ready, 받은 픽셀 f (y,x)  ('gate' = 앞 프레임 출력이 끝나길 기다리는 중)\n"
            "#  L1    conv_l1 FSM, Reorder Buffer 에 쓴 entry 수, conv_l1 -> pool_l1 전달 f p (y,x)\n"
            "#  POOL  pool_l1 출력 (-> conv_l2) f p (py,px) (* = pool_ch_done)\n"
            "#  L2    conv_l2 FSM, Reorder Buffer 에 쓴 entry 수, 최종 출력 f och pos = code (* = out_ch_done)\n"
            "#  !!    이 클럭에 모니터 오류\n#\n"
            "%6s | %-3s %-11s | %-8s %4s %-17s | %-18s | %-11s %4s %-22s\n",
            hs->name, hs->in_pct, hs->ready_pct, FRAMES, "cycle", "IMG", "pixel", "L1 fsm", "rb", "-> pool",
            "POOL out", "L2 fsm", "rb", "OUT");
    if (csv)
        fprintf(csv, "cycle,img_valid,img_ready,img_gate,img_frame,img_y,img_x,l1_fsm,l1_rb,"
                     "l1_tx,l1_frame,l1_pass,l1_y,l1_x,l1_ch_done,"
                     "pool_out_tx,pool_out_frame,pool_out_pass,pool_out_py,pool_out_px,pool_ch_done,"
                     "l2_fsm,l2_rb,out_frame,out_pos,out_och,out_data,out_ch_done,monitor_err\n");

    for (g_cycle = 0; g_cycle < MAX_CYCLES; g_cycle++)
    {
        long errs_before = total_errs();

        /* ---------------- 입력 이미지 소스 (프레임 게이팅) / 최종 sink ---------------- */
        int gate = sent > 0 && sent % N_IN1 == 0 && sent < total_in && !cur_valid &&
                   !(chain_l1_idle() && (g_gate_level < 2 || chain_l2_idle()));
        if (gate)
            res->gate_cycles++;
        if (!cur_valid && sent < total_in && !gate)
            cur_valid = (int)(rnd_next(&seed) % 100) < hs->in_pct;
        int i = sent < total_in ? sent : total_in - 1, fi = i / N_IN1, pix = i % N_IN1;
        uint8_t sink_ready = (uint8_t)((int)(rnd_next(&seed) % 100) < hs->ready_pct);

        /* ---------------- 조합 평가 순서 ----------------
         * ready 는 모두 레지스터로 정해진다 (conv_l2 in_ready, pool out_ready = pool_ready | ~win_valid).
         * ① conv_l2 in_ready -> ② conv_l1 출력 -> ③ pool_l1 -> ④ conv_l1 (pop 확정) -> ⑤ conv_l2 */
        chain_l2_in_t  l2i = {0};
        chain_l2_out_t l2o;
        l2i.out_ready = sink_ready;
        chain_l2_comb(&l2i, &l2o);                                   /* ① */
        uint8_t l2_in_ready = l2o.in_ready;

        chain_l1_in_t  l1i;
        chain_l1_out_t l1o;
        l1i.in_valid  = (uint8_t)cur_valid;
        l1i.pixel_in  = g_img[fi][pix / IN1][pix % IN1];
        l1i.ch_done   = (uint8_t)(cur_valid && pix == N_IN1 - 1);
        l1i.out_ready = 0;
        chain_l1_comb(&l1i, &l1o);                                   /* ② */

        pool_l1_in_t  pin;
        pool_l1_out_t pout;
        pin.out_valid  = l1o.out_valid;
        pin.ch_done    = l1o.out_ch_done;
        pin.pool_ready = l2_in_ready;
        for (int j = 0; j < 3; j++)
            pin.out_data[j] = l1o.out_data[j];
        pool_l1_comb(&g_pool, &pin, &pout);                          /* ③ */

        l1i.out_ready = pout.out_ready;
        chain_l1_comb(&l1i, &l1o);                                   /* ④ */

        l2i.in_valid = pout.pool_valid;
        l2i.ch_done  = pout.pool_ch_done;
        for (int j = 0; j < 3; j++)
            l2i.pixel_in[j] = (int16_t)pout.pool_data[j];
        chain_l2_comb(&l2i, &l2o);                                   /* ⑤ */

        /* ================= monitors ================= */
        int img_tx  = cur_valid && l1o.in_ready;
        int l1_tx   = l1o.out_valid && pout.out_ready;
        int pout_tx = pout.pool_valid && l2_in_ready;
        int out_tx  = l2o.out_valid && sink_ready;
        int lf = 0, lp = 0, lx = 0, qf = 0, qp = 0, qq = 0, of = 0, opos = 0, ooch = 0;

        if (img_tx)
            mark(fi, T_IN);
        if (l1_tx)
        {
            lf = e1 / N_E1; lp = (e1 / N_PIX1) % 2; lx = e1 % N_PIX1;
            for (int j = 0; j < 3; j++)
                expect(M_L1, lf < FRAMES && l1o.out_data[j] == g_ref1[lf][lp * 3 + j][lx / O1][lx % O1],
                       "frame %d pass %d pixel %d lane %d: %u expect %u", lf, lp, lx, j, l1o.out_data[j],
                       lf < FRAMES ? g_ref1[lf][lp * 3 + j][lx / O1][lx % O1] : 0);
            expect(M_L1, l1o.out_ch_done == (lx == N_PIX1 - 1), "out_ch_done %d at pixel %d", l1o.out_ch_done, lx);
            mark(lf, lp ? T_P1 : T_P0);
            e1++;
        }
        if (pout_tx)
        {
            qf = q / (2 * N_P1); qp = (q / N_P1) % 2; qq = q % N_P1;
            for (int j = 0; j < 3; j++)
            {
                int c = qp * 3 + j;
                expect(M_POOL, qf < FRAMES && pout.pool_data[j] == g_refp[qf][c][qq / P1][qq % P1],
                       "frame %d pass %d (%d,%d) lane %d: %u expect %u", qf, qp, qq / P1, qq % P1, j, pout.pool_data[j],
                       qf < FRAMES ? g_refp[qf][c][qq / P1][qq % P1] : 0);
                if (qf == 0)
                {
                    res->pool_py_exact += pout.pool_data[j] == g_py_pool[c][qq];
                    res->pool_py_diff  += pout.pool_data[j] != g_py_pool[c][qq];
                }
            }
            expect(M_POOL, pout.pool_ch_done == (qq == N_P1 - 1), "pool_ch_done %d at %d", pout.pool_ch_done, qq);
            mark(qf, T_POOL);
            q++;
        }
        if (out_tx)
        {
            /* conv_l2 출력 순서: frame -> och -> pixel (채널 우선) */
            of = o / N_O2; ooch = (o / N_PIX2) % C2; opos = o % N_PIX2;
            expect(M_L2, of < FRAMES && l2o.out_data == g_ref2[of][ooch][opos], "frame %d och %d pos %d: %u expect %u",
                   of, ooch, opos, l2o.out_data, of < FRAMES ? g_ref2[of][ooch][opos] : 0);
            expect(M_L2, l2o.out_ch_done == (opos == N_PIX2 - 1), "out_ch_done %d at och %d pos %d", l2o.out_ch_done,
                   ooch, opos);
            if (of == 0)
            {
                res->l2_py_exact += l2o.out_data == g_py_conv2[ooch][opos];
                res->l2_py_diff  += l2o.out_data != g_py_conv2[ooch][opos];
            }
            mark(of, T_L2);
            o++;
        }

        /* ================= log ================= */
        long err = total_errs() - errs_before;
        chain_probe_t p1, p2;
        chain_l1_probe(&p1);
        chain_l2_probe(&p2);
        if (log)
        {
            char a[32] = ".", b[32] = ".", d[40] = ".", e[40] = ".";
            if (img_tx)
                snprintf(a, sizeof a, "f%d (%2d,%2d)", fi, pix / IN1, pix % IN1);
            else if (gate)
                snprintf(a, sizeof a, "gate");
            if (l1_tx)
                snprintf(b, sizeof b, "f%d p%d (%2d,%2d)%s", lf, lp, lx / O1, lx % O1, l1o.out_ch_done ? " *" : "");
            if (pout_tx)
                snprintf(d, sizeof d, "f%d p%d (%2d,%2d)%s", qf, qp, qq / P1, qq % P1, pout.pool_ch_done ? " *" : "");
            if (out_tx)
                snprintf(e, sizeof e, "f%d o%-2d %3d = %u%s", of, ooch, opos, l2o.out_data, l2o.out_ch_done ? " *" : "");
            fprintf(log, "%6ld | %d/%d %-11s | %-8s %4u %-17s | %-18s | %-11s %4u %-22s%s\n",
                    g_cycle, cur_valid, l1o.in_ready, a, p1.fsm, p1.rb_fill, b, d, p2.fsm, p2.rb_fill, e,
                    err ? "  !!" : "");
        }
        if (csv)
        {
            fprintf(csv, "%ld,%d,%d,%d,", g_cycle, cur_valid, l1o.in_ready, gate);
            if (img_tx) fprintf(csv, "%d,%d,%d,", fi, pix / IN1, pix % IN1); else fprintf(csv, ",,,");
            fprintf(csv, "%s,%u,%d,", p1.fsm, p1.rb_fill, l1_tx);
            if (l1_tx) fprintf(csv, "%d,%d,%d,%d,%d,", lf, lp, lx / O1, lx % O1, l1o.out_ch_done); else fprintf(csv, ",,,,,");
            fprintf(csv, "%d,", pout_tx);
            if (pout_tx) fprintf(csv, "%d,%d,%d,%d,%d,", qf, qp, qq / P1, qq % P1, pout.pool_ch_done); else fprintf(csv, ",,,,,");
            fprintf(csv, "%s,%u,", p2.fsm, p2.rb_fill);
            if (out_tx) fprintf(csv, "%d,%d,%d,%u,%d,", of, opos, ooch, l2o.out_data, l2o.out_ch_done); else fprintf(csv, ",,,,,");
            fprintf(csv, "%ld\n", err);
        }

        /* ---------------- posedge clk ---------------- */
        if (img_tx)
        {
            sent++;
            cur_valid = 0;
        }
        chain_l1_seq();
        pool_l1_seq(&g_pool);
        chain_l2_seq();

        if (sent == total_in && o == total_out && chain_l1_idle() && chain_l2_idle())
            break;
    }

    chain_probe_t p1, p2;
    chain_l1_probe(&p1);
    chain_l2_probe(&p2);
    expect(M_END, g_cycle < MAX_CYCLES, "timeout: image %d/%d, out %d/%d", sent, total_in, o, total_out);
    expect(M_END, e1 == FRAMES * N_E1, "conv_l1 entries %d expect %d", e1, FRAMES * N_E1);
    expect(M_END, q == FRAMES * 2 * N_P1, "pool outputs %d expect %d", q, FRAMES * 2 * N_P1);
    expect(M_END, o == total_out, "conv_l2 outputs %d expect %d", o, total_out);
    expect(M_END, p1.rb_overrun == 0 && p2.rb_overrun == 0, "reorder overrun l1 %u l2 %u", p1.rb_overrun, p2.rb_overrun);
    expect(M_END, g_pool.ctrl.dbg_ch_err_cnt == 0, "pool ch_err %u", g_pool.ctrl.dbg_ch_err_cnt);

    res->cycles     = g_cycle;
    res->l1_rb_peak = p1.rb_peak;
    res->l2_rb_peak = p2.rb_peak;
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
        else if (!strcmp(argv[i], "-g") && i + 1 < argc)
            g_gate_level = atoi(argv[++i]);
    }
    if (want_log && !sel_hs)
        sel_hs = "A";
    if (sel_hs && !strcmp(sel_hs, "all"))
        sel_hs = NULL;
    if (want_log)
        MKDIR("logs");

    if (read_vec("vectors/conv_l1.txt", 1) || read_vec("vectors/conv_l2.txt", 2))
    {
        printf("vectors/conv_l1.txt or conv_l2.txt missing or malformed\n");
        return 1;
    }
    make_reference();

    printf("cnn_chain: image 28x28 -> conv_l1 -> pool_l1 -> conv_l2 -> 11x11x16, real image, %d frames, "
           "frame gate: %s\n\n", FRAMES, g_gate_level >= 2 ? "conv_l1 + conv_l2 idle" : "conv_l1 idle");
    for (int h = 0; h < N_HS; h++)
    {
        if (sel_hs && strcmp(sel_hs, g_hs[h].name))
            continue;
        FILE *log = NULL, *csv = NULL;
        char name[64];
        if (want_log)
        {
            snprintf(name, sizeof name, "logs/cnn_chain_real_%s.log", g_hs[h].name);
            log = fopen(name, "w");
            snprintf(name, sizeof name, "logs/cnn_chain_real_%s.csv", g_hs[h].name);
            csv = fopen(name, "w");
        }
        run_res_t r;
        int fail = run(&g_hs[h], log, csv, &r);
        if (log) fclose(log);
        if (csv) fclose(csv);

        printf("[%s] %s (image %3d%% / out_ready %3d%%) |", fail ? "FAIL" : " ok ", g_hs[h].name, g_hs[h].in_pct,
               g_hs[h].ready_pct);
        for (int id = 0; id < N_MON; id++)
            printf(" %s %ld%s", g_mon_name[id], g_mon[id].checks, g_mon[id].errs ? "!" : "");
        printf(" | %ld cyc (gate %ld) | reorder peak l1 %u l2 %u\n", r.cycles, r.gate_cycles, r.l1_rb_peak,
               r.l2_rb_peak);
        printf("       frame 0 vs Python: pool %d exact / %d diff, conv_l2 %d exact / %d diff\n", r.pool_py_exact,
               r.pool_py_diff, r.l2_py_exact, r.l2_py_diff);
        for (int f = 0; f < FRAMES; f++)
        {
            printf("       frame %d:", f);
            for (int t = 0; t < N_T; t++)
                printf(" %s %ld~%ld%s", g_t_short[t], g_t[f][t][0], g_t[f][t][1], t + 1 < N_T ? "," : "\n");
        }
        if (want_log)
            printf("       -> logs/cnn_chain_real_%s.log / .csv\n", g_hs[h].name);
        fail_runs += fail;
        runs++;
    }
    printf("\n%d / %d runs passed -> %s\n", runs - fail_runs, runs, fail_runs ? "FAIL" : "ALL PASS");
    return fail_runs ? 1 : 0;
}
