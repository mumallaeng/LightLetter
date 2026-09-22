/*
 * pool_l1 testbench: 입력 시나리오 x handshake 조건마다 controller / datapath / 출력을 매 사이클 검사한다.
 *
 *   make -f pool_l1.mk test                    모든 시나리오 x handshake 조건
 *   make -f pool_l1.mk log                     logs/pool_l1_real_A.log / .csv (1 클럭 = 1 줄)
 *   make -f pool_l1.mk log SC=maxpos HS=E      원하는 run 의 로그 (SC, HS 에 all 가능)
 *   (or: build/test_pool_l1 [vectors/conv_l1.txt] [-l] [-s scenario|all] [-h A|B|C|D|E|all])
 *
 * 입력 스트림 (팀원 FIFO 변경 후 conv_l1 출력): pass 0 (och0~2) 26x26 raster -> pass 1 (och3~5) 26x26 raster,
 * entry = out_data0..2, pass 마지막 픽셀에 ch_done, valid 는 받아갈 때까지 유지, run 마다 2 프레임.
 *
 * 모니터 (기대값은 입력으로 testbench 가 직접 계산):
 *   CTRL  (row_cnt, col_cnt) = 다음에 받을 픽셀 위치 · out_ready = pool_ready | ~win_valid ·
 *         pool_valid 는 홀수 행 && 홀수 열 픽셀에서만 · pool_ch_done 은 pass 마지막 출력에서만 ·
 *         pass 수, ch_done 위치 불일치 카운터 (dbg_ch_err_cnt)
 *   DP    mem_we 때 쓴 값 = 윗줄 가로 짝 max · 홀수 열에서 prev_reg = 직전 짝수 열 값 · 출력 때 mem_rd = 윗줄 짝 max
 *   OUT   pool_data0..2 = 2x2 max (pass, py, px) · pool_valid && !pool_ready 면 다음 클럭까지 값 유지 ·
 *         real 첫 프레임은 Python conv1 + MaxPool 과 비교
 *
 * 시나리오: real, pattern, maxpos, random, ties, extreme, ch_done_off (ch_done 을 한 픽셀 일찍 -> 출력은 정확, 에러 카운터만 증가)
 * handshake: A 연속, B 입력 bubble, C pool_ready 35%, D 둘 다, E conv_l2 흉내 (출력 하나 받을 때마다 19 클럭 ready = 0)
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pool_l1.h"

#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define MKDIR(p) mkdir(p, 0755)
#endif

#define FRAMES      2
#define PASSES      2
#define C_ALL       (POOL_L1_LANES * PASSES)                /* 6 */
#define IN_H        POOL_L1_IN_H
#define IN_W        POOL_L1_IN_W
#define OUT_H       POOL_L1_OUT_H
#define OUT_W       POOL_L1_OUT_W
#define NPIX        (IN_H * IN_W)                           /* 676 / pass */
#define NOUT        (OUT_H * OUT_W)                         /* 169 / pass */
#define MAX_CYCLES  400000L

/* ================================================================
 * scenario / handshake
 * ================================================================ */
typedef struct
{
    const char *name;
    const char *desc;
    uint16_t    in[FRAMES][C_ALL][IN_H][IN_W];
    int         chdone_early;                   /* ch_done 을 몇 픽셀 일찍 주는지 (0 = 정상) */
    int         py_frames;                      /* Python 기대값이 있는 프레임 수 */
    uint16_t    py[C_ALL][OUT_H][OUT_W];
} scenario_t;

typedef struct
{
    const char *name;
    int         in_pct;         /* out_valid 확률 */
    int         ready_pct;      /* pool_ready 확률 */
    int         busy;           /* > 0: 출력 하나 받을 때마다 busy 클럭 동안 pool_ready = 0 (conv_l2 흉내) */
    uint32_t    seed;
} hs_cfg_t;

static const hs_cfg_t g_hs[] = {
    {"A", 100, 100,  0, 11},    /* 연속 */
    {"B",  60, 100,  0, 22},    /* 입력 bubble */
    {"C", 100,  35,  0, 33},    /* pool_ready 35% */
    {"D",  35,  45,  0, 44},    /* 둘 다 */
    {"E", 100, 100, 19, 55},    /* conv_l2 흉내 */
};
#define N_HS ((int)(sizeof g_hs / sizeof g_hs[0]))

static uint16_t g_ref[FRAMES][C_ALL][OUT_H][OUT_W];

static uint32_t rnd_next(uint32_t *s)
{
    *s ^= *s << 13;
    *s ^= *s >> 17;
    *s ^= *s << 5;
    return *s;
}

static uint16_t max2(uint16_t a, uint16_t b)
{
    return a > b ? a : b;
}

static void make_reference(const scenario_t *sc)
{
    for (int f = 0; f < FRAMES; f++)
        for (int c = 0; c < C_ALL; c++)
            for (int py = 0; py < OUT_H; py++)
                for (int px = 0; px < OUT_W; px++)
                {
                    const uint16_t (*x)[IN_W] = sc->in[f][c];
                    g_ref[f][c][py][px] = max2(max2(x[2 * py][2 * px], x[2 * py][2 * px + 1]),
                                               max2(x[2 * py + 1][2 * px], x[2 * py + 1][2 * px + 1]));
                }
}

/* ================================================================
 * monitors
 * ================================================================ */
enum { M_CTRL, M_DP, M_OUT, N_MON };
static const char *g_mon_name[N_MON] = {"CTRL", "DP", "OUT"};

typedef struct
{
    long checks, errs;
    int  shown;
} mon_t;

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
 * clock log: 한 클럭 = 한 줄 (.log 사람이 읽는 고정폭, .csv 가공용). posedge 직전 값.
 * ================================================================ */
typedef struct
{
    int  f, p, y, x;            /* FIFO 맨 앞 픽셀 위치 */
    int  have_in;               /* 입력이 남아 있는지 */
    int  tx, of, op, opy, opx;  /* 출력 전달 (pool_valid & pool_ready) 과 그 위치 */
    long err;
} clk_rec_t;

static void log_header(FILE *log, FILE *csv, const scenario_t *sc, const hs_cfg_t *hs)
{
    if (log)
    {
        fprintf(log,
            "# pool_l1 clock log | scenario %s (%s) | handshake %s: out_valid %d%% / pool_ready %d%%%s | %d frames\n"
            "#\n"
            "# 한 줄 = 한 클럭, posedge 직전 값. '.' = 비활성\n"
            "#  IN    out_valid/out_ready, FIFO 맨 앞 픽셀 f(frame) p(pass) (y,x) = out_data0,1,2, C = ch_done\n"
            "#  CTRL  row_cnt,col_cnt  row_odd col_odd\n"
            "#  ACT   받은 픽셀의 동작: prev (prev_reg <- d), mem[k] (pool_mem[k] <- pair), OUT (출력), wait (출력 픽셀인데 pool_ready = 0)\n"
            "#  DP    prev_reg / pair = max(prev_reg, d) / mem_rd = pool_mem[addr]  (lane0,1,2)\n"
            "#  POOL  pool_valid/pool_ready, 전달된 출력 f p (py,px) = pool_data0,1,2 (* = pool_ch_done)\n"
            "#  !!    이 클럭에 모니터 오류 (콘솔에 내용 출력)\n"
            "#\n",
            sc->name, sc->desc, hs->name, hs->in_pct, hs->ready_pct,
            hs->busy ? ", 출력 후 19클럭 busy" : "", FRAMES);
        fprintf(log, "%6s | %-3s %-34s | %-9s | %-7s | %-17s %-17s %-17s | %-3s %-30s\n",
                "cycle", "IN", "f p (y,x) = d0,d1,d2", "CTRL", "ACT", "prev_reg", "pair", "mem_rd", "POOL", "out");
    }
    if (csv)
        fprintf(csv, "cycle,out_valid,out_ready,in_frame,in_pass,in_y,in_x,out_data0,out_data1,out_data2,ch_done,"
                     "row_cnt,col_cnt,row_odd,col_odd,win_valid,pixel_valid,prev_we,mem_we,pool_mem_addr,"
                     "prev_reg0,prev_reg1,prev_reg2,pair0,pair1,pair2,mem_rd0,mem_rd1,mem_rd2,"
                     "pool_valid,pool_ready,pool_data0,pool_data1,pool_data2,pool_ch_done,"
                     "out_frame,out_pass,out_py,out_px,monitor_err\n");
}

static void log_row(FILE *log, FILE *csv, const pool_l1_t *m, const pool_l1_in_t *in,
                    const pool_l1_out_t *out, const clk_rec_t *r)
{
    const pool_l1_ctrl_out_t *c = &m->ctrl_o;
    const pool_l1_datapath_out_t *d = &m->dp_o;
    const uint16_t *pr = m->dp.prev_reg;

    if (log)
    {
        char pin[48] = ".", act[12] = ".", po[48] = ".";
        char s_prev[24], s_pair[24], s_mem[24];

        if (r->have_in && in->out_valid)
            snprintf(pin, sizeof pin, "f%d p%d (%2d,%2d) = %u,%u,%u%s", r->f, r->p, r->y, r->x,
                     in->out_data[0], in->out_data[1], in->out_data[2], in->ch_done ? " C" : "");
        if (c->pixel_valid)
            snprintf(act, sizeof act, "%s", c->prev_we ? "prev" : c->mem_we ? "" : "OUT");
        if (c->mem_we)
            snprintf(act, sizeof act, "mem[%d]", c->pool_mem_addr);
        if (c->pool_valid && !c->pixel_valid)
            snprintf(act, sizeof act, "wait");
        if (r->tx)
            snprintf(po, sizeof po, "f%d p%d (%2d,%2d) = %u,%u,%u%s", r->of, r->op, r->opy, r->opx,
                     out->pool_data[0], out->pool_data[1], out->pool_data[2], out->pool_ch_done ? " *" : "");
        snprintf(s_prev, sizeof s_prev, "%u,%u,%u", pr[0], pr[1], pr[2]);
        snprintf(s_pair, sizeof s_pair, "%u,%u,%u", d->pair[0], d->pair[1], d->pair[2]);
        snprintf(s_mem, sizeof s_mem, "%u,%u,%u", d->mem_rd[0], d->mem_rd[1], d->mem_rd[2]);

        fprintf(log, "%6ld | %d/%d %-34s | %2d,%-2d %d%d | %-7s | %-17s %-17s %-17s | %d/%d %-30s%s\n",
                g_cycle, in->out_valid, out->out_ready, pin, m->ctrl.row_cnt, m->ctrl.col_cnt,
                c->row_odd, c->col_odd, act, s_prev, s_pair, s_mem, out->pool_valid, in->pool_ready, po,
                r->err ? "  !!" : "");
    }

    if (csv)
    {
        fprintf(csv, "%ld,%d,%d,", g_cycle, in->out_valid, out->out_ready);
        if (r->have_in)
            fprintf(csv, "%d,%d,%d,%d,", r->f, r->p, r->y, r->x);
        else
            fprintf(csv, ",,,,");
        fprintf(csv, "%u,%u,%u,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,", in->out_data[0], in->out_data[1],
                in->out_data[2], in->ch_done, m->ctrl.row_cnt, m->ctrl.col_cnt, c->row_odd, c->col_odd,
                c->win_valid, c->pixel_valid, c->prev_we, c->mem_we, c->pool_mem_addr);
        fprintf(csv, "%u,%u,%u,%u,%u,%u,%u,%u,%u,", pr[0], pr[1], pr[2], d->pair[0], d->pair[1], d->pair[2],
                d->mem_rd[0], d->mem_rd[1], d->mem_rd[2]);
        fprintf(csv, "%d,%d,%u,%u,%u,%d,", out->pool_valid, in->pool_ready, out->pool_data[0], out->pool_data[1],
                out->pool_data[2], out->pool_ch_done);
        if (r->tx)
            fprintf(csv, "%d,%d,%d,%d,", r->of, r->op, r->opy, r->opx);
        else
            fprintf(csv, ",,,,");
        fprintf(csv, "%ld\n", r->err);
    }
}

/* ================================================================
 * one run
 * ================================================================ */
typedef struct
{
    long     cycles;
    int      py_exact, py_diff;
    uint32_t ch_err;
} run_res_t;

static pool_l1_t g_dut;

static int run(const scenario_t *sc, const hs_cfg_t *hs, FILE *log, FILE *csv, run_res_t *res)
{
    pool_l1_t *m = &g_dut;
    const int total_in  = FRAMES * PASSES * NPIX;
    const int total_out = FRAMES * PASSES * NOUT;

    memset(g_mon, 0, sizeof g_mon);
    memset(res, 0, sizeof *res);
    make_reference(sc);
    pool_l1_reset(m);

    uint32_t seed = hs->seed;
    int sent = 0, cur_valid = 0, ov = 0, busy = 0, n_ch_done = 0;
    int hold = 0;                                   /* 지난 클럭에 pool_valid && !pool_ready 였나 */
    uint16_t hold_data[POOL_L1_LANES] = {0};

    log_header(log, csv, sc, hs);

    for (g_cycle = 0; g_cycle < MAX_CYCLES; g_cycle++)
    {
        pool_l1_in_t  in;
        pool_l1_out_t out;
        clk_rec_t     rec;
        long          errs_before = total_errs();

        memset(&rec, 0, sizeof rec);

        /* ---------------- 앞단 FIFO: valid 는 받아갈 때까지 유지 ---------------- */
        if (!cur_valid && sent < total_in)
            cur_valid = (int)(rnd_next(&seed) % 100) < hs->in_pct;

        int i   = sent < total_in ? sent : total_in - 1;
        int f   = i / (PASSES * NPIX);
        int p   = (i / NPIX) % PASSES;
        int pix = i % NPIX;
        int y   = pix / IN_W, x = pix % IN_W;

        in.out_valid = (uint8_t)cur_valid;
        in.ch_done   = (uint8_t)(cur_valid && pix == NPIX - 1 - sc->chdone_early);
        for (int l = 0; l < POOL_L1_LANES; l++)
            in.out_data[l] = sc->in[f][p * POOL_L1_LANES + l][y][x];

        /* ---------------- 다음 단 (conv_l2) ready ---------------- */
        if (hs->busy)
            in.pool_ready = (uint8_t)(busy == 0);
        else
            in.pool_ready = (uint8_t)((int)(rnd_next(&seed) % 100) < hs->ready_pct);

        pool_l1_comb(m, &in, &out);
        const pool_l1_ctrl_out_t *c = &m->ctrl_o;
        const pool_l1_datapath_out_t *d = &m->dp_o;

        rec.have_in = sent < total_in;
        rec.f = f;
        rec.p = p;
        rec.y = y;
        rec.x = x;

        /* ================= CTRL ================= */
        if (sent < total_in)
        {
            expect(M_CTRL, m->ctrl.row_cnt == y && m->ctrl.col_cnt == x, "counter (%d,%d) but next pixel is (%d,%d)",
                   m->ctrl.row_cnt, m->ctrl.col_cnt, y, x);
            int win = (y & 1) && (x & 1);
            expect(M_CTRL, out.pool_valid == (cur_valid && win), "pool_valid %d at (%d,%d) out_valid %d",
                   out.pool_valid, y, x, cur_valid);
            expect(M_CTRL, out.out_ready == (in.pool_ready || !win), "out_ready %d (pool_ready %d, output pixel %d)",
                   out.out_ready, in.pool_ready, win);
        }
        else
            expect(M_CTRL, !out.pool_valid, "pool_valid without input");

        /* ================= DP ================= */
        if (c->pixel_valid)
        {
            const uint16_t (*img)[IN_H][IN_W] = sc->in[f] + p * POOL_L1_LANES;
            if (x & 1)
                for (int l = 0; l < POOL_L1_LANES; l++)
                    expect(M_DP, m->dp.prev_reg[l] == img[l][y][x - 1], "lane %d prev_reg %u expect %u (y %d x %d)",
                           l, m->dp.prev_reg[l], img[l][y][x - 1], y, x);
            if (c->mem_we)
            {
                expect(M_DP, !(y & 1) && (x & 1), "mem_we at (%d,%d)", y, x);
                for (int l = 0; l < POOL_L1_LANES; l++)
                    expect(M_DP, d->pair[l] == max2(img[l][y][x - 1], img[l][y][x]),
                           "lane %d pool_mem[%d] <- %u expect %u", l, c->pool_mem_addr, d->pair[l],
                           max2(img[l][y][x - 1], img[l][y][x]));
            }
        }
        if (out.pool_valid && sent < total_in)
        {
            const uint16_t (*img)[IN_H][IN_W] = sc->in[f] + p * POOL_L1_LANES;
            for (int l = 0; l < POOL_L1_LANES; l++)
                expect(M_DP, d->mem_rd[l] == max2(img[l][y - 1][x - 1], img[l][y - 1][x]),
                       "lane %d mem_rd %u expect %u (y %d x %d)", l, d->mem_rd[l],
                       max2(img[l][y - 1][x - 1], img[l][y - 1][x]), y, x);
        }

        /* ================= OUT ================= */
        if (hold)
        {
            expect(M_OUT, out.pool_valid, "pool_valid dropped before pool_ready");
            for (int l = 0; l < POOL_L1_LANES; l++)
                expect(M_OUT, out.pool_data[l] == hold_data[l], "lane %d data changed while waiting", l);
        }
        hold = out.pool_valid && !in.pool_ready;
        for (int l = 0; l < POOL_L1_LANES; l++)
            hold_data[l] = out.pool_data[l];

        if (out.pool_valid && in.pool_ready)
        {
            int of = ov / (PASSES * NOUT), op = (ov / NOUT) % PASSES, q = ov % NOUT;
            int py = q / OUT_W, px = q % OUT_W;
            for (int l = 0; l < POOL_L1_LANES; l++)
            {
                int ch = op * POOL_L1_LANES + l;
                expect(M_OUT, of < FRAMES && out.pool_data[l] == g_ref[of][ch][py][px],
                       "frame %d pass %d (%d,%d) lane %d: %u expect %u", of, op, py, px, l, out.pool_data[l],
                       of < FRAMES ? g_ref[of][ch][py][px] : 0);
                if (of < sc->py_frames)
                {
                    res->py_exact += out.pool_data[l] == sc->py[ch][py][px];
                    res->py_diff  += out.pool_data[l] != sc->py[ch][py][px];
                }
            }
            expect(M_OUT, out.pool_ch_done == (q == NOUT - 1), "pool_ch_done %d at (%d,%d)", out.pool_ch_done, py, px);
            n_ch_done += out.pool_ch_done;
            rec.tx  = 1;
            rec.of  = of;
            rec.op  = op;
            rec.opy = py;
            rec.opx = px;
            ov++;
            busy = hs->busy;
        }
        else if (busy > 0)
            busy--;

        rec.err = total_errs() - errs_before;
        log_row(log, csv, m, &in, &out, &rec);

        /* ---------------- posedge clk ---------------- */
        if (c->pixel_valid)
        {
            sent++;
            cur_valid = 0;
        }
        pool_l1_seq(m);

        if (sent == total_in && ov == total_out)
            break;
    }

    /* ---------------- 개수 / 상태 검사 ---------------- */
    uint32_t exp_err = sc->chdone_early ? FRAMES * PASSES : 0;
    expect(M_CTRL, g_cycle < MAX_CYCLES, "timeout: sent %d/%d, out %d/%d", sent, total_in, ov, total_out);
    expect(M_CTRL, m->ctrl.pass_cnt == FRAMES * PASSES, "pass_cnt %u expect %d", m->ctrl.pass_cnt, FRAMES * PASSES);
    expect(M_CTRL, m->ctrl.dbg_ch_err_cnt == exp_err, "dbg_ch_err_cnt %u expect %u", m->ctrl.dbg_ch_err_cnt, exp_err);
    expect(M_OUT, ov == total_out, "outputs %d expect %d", ov, total_out);
    expect(M_OUT, n_ch_done == FRAMES * PASSES, "pool_ch_done %d expect %d", n_ch_done, FRAMES * PASSES);

    res->cycles = g_cycle;
    res->ch_err = m->ctrl.dbg_ch_err_cnt;
    return total_errs() || res->py_diff;
}

/* ================================================================
 * scenarios
 * ================================================================ */
static int load_real(const char *path, scenario_t *sc)
{
    FILE *f = fopen(path, "r");
    int hdr[6], v, ok = f != NULL;

    for (int i = 0; ok && i < 6; i++)
        ok = fscanf(f, "%d", &hdr[i]) == 1;
    ok = ok && hdr[0] == 1 && hdr[1] == IN_H + 2 && hdr[2] == IN_W + 2 && hdr[3] == C_ALL;
    int skip = ok ? hdr[1] * hdr[2] + hdr[3] * 9 + hdr[3] : 0;     /* input, weight, bias */
    for (int i = 0; ok && i < skip; i++)
        ok = fscanf(f, "%d", &v) == 1;
    for (int c = 0; ok && c < C_ALL; c++)                           /* conv1 + ReLU codes [6][26][26] */
        for (int p = 0; ok && p < NPIX; p++)
        {
            ok = fscanf(f, "%d", &v) == 1;
            sc->in[0][c][p / IN_W][p % IN_W] = (uint16_t)v;
            /* frame 1: 좌우 반전 (Python 기대값 없음) */
            sc->in[1][c][p / IN_W][IN_W - 1 - p % IN_W] = (uint16_t)v;
        }
    for (int c = 0; ok && c < C_ALL; c++)                           /* conv1 + MaxPool [6][13][13] */
        for (int p = 0; ok && p < NOUT; p++)
        {
            ok = fscanf(f, "%d", &v) == 1;
            sc->py[c][p / OUT_W][p % OUT_W] = (uint16_t)v;
        }
    if (f)
        fclose(f);
    if (!ok)
    {
        printf("%s: missing or malformed (run: make -f conv_l1.mk vectors)\n", path);
        return 1;
    }
    sc->name      = "real";
    sc->desc      = "Python conv1 + ReLU output (frame 2 = mirrored)";
    sc->py_frames = 1;
    return 0;
}

static void make_pattern(scenario_t *sc)
{
    sc->name = "pattern";
    sc->desc = "unique value per pixel (ch*4000 + y*64 + x*2 + 1)";
    for (int c = 0; c < C_ALL; c++)
        for (int y = 0; y < IN_H; y++)
            for (int x = 0; x < IN_W; x++)
            {
                int v = c * 4000 + y * 64 + x * 2 + 1;
                sc->in[0][c][y][x] = (uint16_t)v;
                sc->in[1][c][y][x] = (uint16_t)(30000 - v);
            }
}

static void make_maxpos(scenario_t *sc)
{
    uint32_t s = 0x5EED;

    sc->name = "maxpos";
    sc->desc = "max of each 2x2 at position a/b/c/d in turn (all 4 compare paths)";
    for (int f = 0; f < FRAMES; f++)
        for (int c = 0; c < C_ALL; c++)
            for (int py = 0; py < OUT_H; py++)
                for (int px = 0; px < OUT_W; px++)
                {
                    int k = (py * OUT_W + px + c + f) & 3;
                    for (int j = 0; j < 4; j++)
                        sc->in[f][c][2 * py + j / 2][2 * px + j % 2] =
                            (uint16_t)(j == k ? 1000 + rnd_next(&s) % 1000 : rnd_next(&s) % 1000);
                }
}

static void make_random(scenario_t *sc)
{
    uint32_t s = 0xC0FFEE;

    sc->name = "random";
    sc->desc = "random 0 .. 32767";
    for (int f = 0; f < FRAMES; f++)
        for (int c = 0; c < C_ALL; c++)
            for (int y = 0; y < IN_H; y++)
                for (int x = 0; x < IN_W; x++)
                    sc->in[f][c][y][x] = (uint16_t)(rnd_next(&s) % 32768);
}

static void make_ties(scenario_t *sc)
{
    uint32_t s = 0x7135;

    sc->name = "ties";
    sc->desc = "values 0 .. 2 only (many equal values in a window)";
    for (int f = 0; f < FRAMES; f++)
        for (int c = 0; c < C_ALL; c++)
            for (int y = 0; y < IN_H; y++)
                for (int x = 0; x < IN_W; x++)
                    sc->in[f][c][y][x] = (uint16_t)(rnd_next(&s) % 3);
}

static void make_extreme(scenario_t *sc)
{
    sc->name = "extreme";
    sc->desc = "0 / 32767: whole windows alternate (frame 1), single 32767 per window (frame 2)";
    for (int c = 0; c < C_ALL; c++)
        for (int py = 0; py < OUT_H; py++)
            for (int px = 0; px < OUT_W; px++)
                for (int j = 0; j < 4; j++)
                {
                    int y = 2 * py + j / 2, x = 2 * px + j % 2;
                    sc->in[0][c][y][x] = ((py + px + c) & 1) ? 32767 : 0;
                    sc->in[1][c][y][x] = (j == ((py + px) & 3)) ? 32767 : 0;
                }
}

/* ================================================================
 * main
 * ================================================================ */
static scenario_t g_sc[7];

static void usage(void)
{
    printf("usage: test_pool_l1 [vectors/conv_l1.txt] [-l] [-s scenario|all] [-h A|B|C|D|E|all]\n"
           "  (no option)  run every scenario x handshake condition\n"
           "  -s, -h       run only the selected scenario / handshake condition\n"
           "  -l           write logs/pool_l1_<scenario>_<cond>.log (readable) and .csv per run;\n"
           "               with -l alone, only real / A is run\n"
           "  scenarios: real pattern maxpos random ties extreme ch_done_off\n");
}

int main(int argc, char **argv)
{
    const char *path = "vectors/conv_l1.txt", *sel_sc = NULL, *sel_hs = NULL;
    int want_log = 0, fail_runs = 0, runs = 0;
    long sum_checks[N_MON] = {0}, sum_errs[N_MON] = {0};

    for (int i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "-l"))
            want_log = 1;
        else if (!strcmp(argv[i], "-s") && i + 1 < argc)
            sel_sc = argv[++i];
        else if (!strcmp(argv[i], "-h") && i + 1 < argc)
            sel_hs = argv[++i];
        else if (argv[i][0] == '-')
        {
            usage();
            return 1;
        }
        else
            path = argv[i];
    }
    if (want_log && !sel_sc && !sel_hs)
    {
        sel_sc = "real";
        sel_hs = "A";
    }
    if (sel_sc && !strcmp(sel_sc, "all"))
        sel_sc = NULL;
    if (sel_hs && !strcmp(sel_hs, "all"))
        sel_hs = NULL;
    if (want_log)
        MKDIR("logs");

    if (load_real(path, &g_sc[0]))
        return 1;
    make_pattern(&g_sc[1]);
    make_maxpos(&g_sc[2]);
    make_random(&g_sc[3]);
    make_ties(&g_sc[4]);
    make_extreme(&g_sc[5]);
    g_sc[6] = g_sc[0];
    g_sc[6].name = "ch_done_off";
    g_sc[6].desc = "real input, ch_done one pixel early (output must stay correct, error counter counts)";
    g_sc[6].chdone_early = 1;
    const int n_sc = (int)(sizeof g_sc / sizeof g_sc[0]);

    printf("pool_l1: %dx%dx%d -> %dx%dx%d (2 pass x 3 lanes), %d frames per run\n", IN_H, IN_W, C_ALL,
           OUT_H, OUT_W, C_ALL, FRAMES);
    printf("handshake: A continuous, B out_valid 60%%, C pool_ready 35%%, D 35%% / 45%%, E conv_l2-like busy 19\n\n");

    for (int s = 0; s < n_sc; s++)
    {
        if (sel_sc && strcmp(sel_sc, g_sc[s].name))
            continue;
        printf("%s: %s\n", g_sc[s].name, g_sc[s].desc);
        for (int h = 0; h < N_HS; h++)
        {
            if (sel_hs && strcmp(sel_hs, g_hs[h].name))
                continue;

            FILE *log = NULL, *csv = NULL;
            char  name[96];
            if (want_log)
            {
                snprintf(name, sizeof name, "logs/pool_l1_%s_%s.log", g_sc[s].name, g_hs[h].name);
                log = fopen(name, "w");
                snprintf(name, sizeof name, "logs/pool_l1_%s_%s.csv", g_sc[s].name, g_hs[h].name);
                csv = fopen(name, "w");
                if (!log || !csv)
                {
                    printf("cannot write %s\n", name);
                    return 1;
                }
            }

            run_res_t r;
            int fail = run(&g_sc[s], &g_hs[h], log, csv, &r);
            if (log)
                fclose(log);
            if (csv)
                fclose(csv);

            printf("  [%s] %s |", fail ? "FAIL" : " ok ", g_hs[h].name);
            for (int id = 0; id < N_MON; id++)
            {
                printf(" %s %ld%s", g_mon_name[id], g_mon[id].checks, g_mon[id].errs ? "!" : "");
                sum_checks[id] += g_mon[id].checks;
                sum_errs[id]   += g_mon[id].errs;
            }
            printf(" | %ld cyc | ch_err %u", r.cycles, r.ch_err);
            if (g_sc[s].py_frames)
                printf(" | Python exact %d, diff %d", r.py_exact, r.py_diff);
            printf("\n");
            if (want_log)
                printf("         -> logs/pool_l1_%s_%s.log / .csv (%ld lines)\n", g_sc[s].name, g_hs[h].name,
                       r.cycles + 1);
            fail_runs += fail;
            runs++;
        }
        printf("\n");
    }
    if (!runs)
    {
        usage();
        return 1;
    }

    printf("monitor    checks   errors\n");
    for (int id = 0; id < N_MON; id++)
        printf("%-7s %9ld %8ld\n", g_mon_name[id], sum_checks[id], sum_errs[id]);
    printf("\n%d / %d runs passed -> %s\n", runs - fail_runs, runs, fail_runs ? "FAIL" : "ALL PASS");
    return fail_runs ? 1 : 0;
}
