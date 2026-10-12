/*
 * conv_l1 testbench: 입력 시나리오 x handshake 조건마다 모듈 경계를 매 사이클 검사한다.
 *
 *   make -f conv_l1.mk test                    모든 시나리오 x handshake 조건
 *   make -f conv_l1.mk log                     logs/conv_l1_real_A.log / .csv (1 클럭 = 1 줄)
 *   make -f conv_l1.mk log SC=impulse HS=C     원하는 run 의 로그 (SC, HS 에 all 가능)
 *   (or: build/test_conv_l1 [vectors/conv_l1.txt] [-l] [-s scenario|all] [-h A|B|C|D|all])
 *
 * 모니터 (기대값은 모두 입력 + weight + bias 로 testbench 가 직접 계산):
 *   FSM   상태 전이가 그림 (IDLE -> IMG_IN <-> WAIT_MAC -> STOP -> IDLE) 대로인지, 픽셀은 IDLE/IMG_IN
 *         에서만 받는지, WAIT_MAC/STOP 과 win_valid 동안 out_ready = 0 인지, phase_clear 가 프레임
 *         입력이 끝난 뒤 STOP 에서만 뜨는지, mac_start / phase_clear 개수
 *   LB    win_valid 때 win_out 이 (oy, ox) 위치의 3x3 window 와 같은지
 *   W/ROM cal_valid 때 out_ch_sel 이 0..5 순서인지, weight_in lane 0 = W[och], lane 1/2 = 0 인지,
 *         MAC 도는 동안 window 가 유지되는지
 *   MAC   mac_valid 때 ch_result0 = window x weight (9-tap 합), ch_result1/2 = 0
 *   OB    sum_valid 때 sum_data = bias + 합 (Output Buffer 출력은 레지스터, 1 클럭 뒤)
 *   OUT   Reorder Buffer 출력: group 0 = {och0,1,2} 전 픽셀 -> group 1 = {och3,4,5} 전 픽셀,
 *         값 = ReLU -> round-half-even >> SCALE_EXP -> clip 32767, out_ch_done 은 group 마다 마지막 픽셀
 *         (정수 레퍼런스, bit-exact). real 입력 첫 프레임은 Python 결과와도 비교 (float32 라 +-1 허용)
 *
 * 입력 시나리오 (run 마다 2 프레임 연속, 프레임마다 다른 입력):
 *   real      Python 입력 이미지 + 실제 weight/bias (프레임 2 = 좌우 반전)
 *   pattern   픽셀마다 다른 값 -> window 위치가 어긋나면 바로 드러남
 *   impulse   픽셀 1개만 1, weight 가 (och, tap) 을 인코딩, 모서리 포함
 *   random    입력 / weight INT16 전체 범위 (음수 포함)
 *   rounding  shift 2 로 .5 경계가 자주 나옴 (round-half-even 검증)
 *   extreme   누산 최대 / 최소 (Output Buffer 비트폭), 32767 clip, ReLU
 * handshake 조건: A 연속, B 입력 bubble, C 출력 backpressure, D 둘 다
 * 프레임 게이팅: 팀원 out_reorder 는 한 프레임만 담고 conv FSM 으로 가는 backpressure 가 없으므로,
 *   다음 프레임 입력은 앞 프레임이 다 출력된 뒤 (conv_l1_idle) 시작한다.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "conv_l1.h"

#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define MKDIR(p) mkdir(p, 0755)
#endif

#define FRAMES      2
#define MAX_CYCLES  400000L
#define NWIN        CONV_L1_N                               /* window / frame : 676 */
#define NVAL        (CONV_L1_N * CONV_L1_C_OUT)             /* output / frame : 4056 */
#define NPIX        (CONV_L1_IN_H * CONV_L1_IN_W)           /* pixel / frame  : 784 */
#define NW          (CONV_L1_C_OUT * CONV_L1_C_IN * CE_KK)

/* ================================================================
 * scenario (입력 데이터) / handshake 조건
 * ================================================================ */
typedef struct
{
    const char *name;
    const char *desc;
    uint8_t     scale_exp;
    wgt_t       w[NW];                                          /* [och][9] */
    int32_t     bias[CONV_L1_C_OUT];
    int16_t     in[FRAMES][CONV_L1_IN_H][CONV_L1_IN_W];
    int         py_frames;                                      /* Python 기대값이 있는 프레임 수 */
    uint16_t    py[CONV_L1_C_OUT][CONV_L1_N];
} scenario_t;

typedef struct
{
    const char *name;
    int         in_pct;         /* in_valid 확률 */
    int         ready_pct;      /* out_ready 확률 */
    uint32_t    seed;
} hs_cfg_t;

static const hs_cfg_t g_hs[] = {
    {"A", 100, 100, 11},        /* 연속 스트림 */
    {"B",  60, 100, 22},        /* 입력 bubble */
    {"C", 100,  35, 33},        /* 출력 backpressure */
    {"D",  35,  45, 44},        /* 둘 다 */
};
#define N_HS ((int)(sizeof g_hs / sizeof g_hs[0]))

/* ================================================================
 * reference (입력으로부터 직접 계산)
 * ================================================================ */
static int64_t  g_tap[FRAMES][CONV_L1_N][CONV_L1_C_OUT];
static int64_t  g_sum[FRAMES][CONV_L1_N][CONV_L1_C_OUT];
static uint16_t g_code[FRAMES][CONV_L1_N][CONV_L1_C_OUT];

static uint32_t rnd_next(uint32_t *s)
{
    *s ^= *s << 13;
    *s ^= *s >> 17;
    *s ^= *s << 5;
    return *s;
}

static uint16_t quantize(int64_t acc, int s)
{
    int64_t q = 0;
    if (acc > 0)
    {
        q = acc >> s;
        if (s > 0)
        {
            int64_t rem  = acc & (((int64_t)1 << s) - 1);
            int64_t half = (int64_t)1 << (s - 1);
            if (rem > half || (rem == half && (q & 1)))
                q++;
        }
    }
    return (uint16_t)(q > 32767 ? 32767 : q);
}

static void make_reference(const scenario_t *sc)
{
    for (int f = 0; f < FRAMES; f++)
        for (int pos = 0; pos < CONV_L1_N; pos++)
        {
            int oy = pos / CONV_L1_OUT_W, ox = pos % CONV_L1_OUT_W;
            for (int oc = 0; oc < CONV_L1_C_OUT; oc++)
            {
                int64_t acc = 0;
                for (int k = 0; k < CE_KK; k++)
                    acc += (int64_t)sc->in[f][oy + k / 3][ox + k % 3] * sc->w[oc * CE_KK + k];
                g_tap[f][pos][oc]  = acc;
                g_sum[f][pos][oc]  = acc + sc->bias[oc];
                g_code[f][pos][oc] = quantize(g_sum[f][pos][oc], sc->scale_exp);
            }
        }
}

/* ================================================================
 * monitors
 * ================================================================ */
enum { M_FSM, M_LB, M_WROM, M_MAC, M_OB, M_OUT, N_MON };
static const char *g_mon_name[N_MON] = {"FSM", "LB", "W/ROM", "MAC", "OB", "OUT"};

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

/* frce_controller_fsm_l1-v2 의 전이만 허용 */
static int legal_transition(total_ctrl_fsm_l1_state_t a, total_ctrl_fsm_l1_state_t b)
{
    switch (a)
    {
    case T1_IDLE:     return b == T1_IDLE || b == T1_IMG_IN;
    case T1_IMG_IN:   return b == T1_IMG_IN || b == T1_WAIT_MAC;
    case T1_WAIT_MAC: return b == T1_WAIT_MAC || b == T1_IMG_IN || b == T1_STOP;
    case T1_STOP:     return b == T1_IDLE;                           /* 1 클럭 상태 */
    }
    return 0;
}

/* ================================================================
 * clock log: 한 클럭 = 한 줄
 *   .log : 사람이 읽는 고정폭 텍스트 (비활성 값은 '.')
 *   .csv : 같은 내용을 가공용으로 (값이 없으면 빈 칸)
 * 모든 값은 posedge 직전 (comb 평가 후) 기준이다.
 * ================================================================ */
typedef struct
{
    int  px, pf, py, pxx;                   /* 이번 클럭에 받은 픽셀 (frame, y, x) */
    int  win, wf, woy, wox;                 /* LB 가 낸 window */
    int  mv, mf, mpos, moch;                /* MAC 출력의 (frame, pos, och) */
    int  sv, sf, spos, soch;                /* Output Buffer 출력 */
    int  ov, of, opos, ooch;                /* FIFO 에서 나간 entry (첫 och) */
    long err;                               /* 이 클럭에 난 모니터 오류 수 */
} clk_rec_t;

static void log_header(FILE *log, FILE *csv, const scenario_t *sc, const hs_cfg_t *hs)
{
    if (log)
    {
        fprintf(log,
            "# conv_l1 clock log | scenario %s (%s) | handshake %s: in_valid %d%% / out_ready %d%% | %d frames\n"
            "#\n"
            "# 한 줄 = 한 클럭, posedge 직전 값. '.' = 비활성\n"
            "#  FSM   state, ch_count, 제어 pulse: S = mac_start, C = phase_clear, D = mac_done\n"
            "#  IN    in_valid/in_ready, 받은 픽셀 f(frame) (y,x) = pixel_in\n"
            "#  LB    line buffer 쓰기 위치 (row,col), win_valid 일 때 나온 window f (oy,ox)\n"
            "#  WAC   state, cal_valid 일 때 out_ch_sel\n"
            "#  MAC   파이프 3단 valid (곱셈 reg / FF1 / FF2), mac_valid 일 때 f pos och: ch_result0\n"
            "#  OB    내부 pixel/och 카운터, sum_valid 일 때 pos och = sum (bias 포함, ReLU 전)\n"
            "#  RB    Reorder Buffer 에 이번 프레임 쓴 entry 수, out_valid/out_ready, 나간 entry pos och = out_data0,1,2 (* = out_ch_done)\n"
            "#  !!    이 클럭에 모니터 오류 (콘솔에 내용 출력)\n"
            "#\n",
            sc->name, sc->desc, hs->name, hs->in_pct, hs->ready_pct, FRAMES);
        fprintf(log, "%6s | %-8s cc SCD | %-3s %-18s | %-7s %-11s | %-10s och | %-3s %-30s | %-7s %-24s | %4s %-3s %-26s\n",
                "cycle", "FSM", "IN", "pixel", "LB wr", "window", "WAC", "MAC", "out (f pos och: ch_result0)",
                "OB", "sum", "RB", "v/r", "out");
    }
    if (csv)
        fprintf(csv, "cycle,fsm,ch_count,mac_start,phase_clear,mac_done,in_valid,in_ready,ch_done,"
                     "pixel_valid,px_frame,px_y,px_x,pixel_in,"
                     "lb_wr_row,lb_wr_col,win_valid,win_frame,win_oy,win_ox,"
                     "wac,cal_valid,out_ch_sel,"
                     "mac_s1,mac_s2,mac_valid,mac_frame,mac_pos,mac_och,ch_result0,ch_result1,ch_result2,"
                     "ob_pixel_cnt,ob_och_cnt,sum_valid,sum_frame,sum_pos,sum_och,sum_data,"
                     "reorder_wr_cnt,out_valid,out_ready,out_frame,out_pos,out_och,out_data0,out_data1,out_data2,"
                     "out_ch_done,monitor_err\n");
}

static void log_row(FILE *log, FILE *csv, const conv_l1_t *m, const conv_l1_in_t *in,
                    const conv_l1_out_t *out, const clk_rec_t *r)
{
    int s1 = m->mac.mu[0][0].valid_reg, s2 = m->mac.c_valid_reg[0];
    int taken = out->out_valid && in->out_ready;

    if (log)
    {
        char px[48] = ".", win[32] = ".", och[8] = ".", mac[64] = ".", sum[48] = ".", fo[64] = ".";

        if (r->px)
            snprintf(px, sizeof px, "f%d (%2d,%2d)=%d", r->pf, r->py, r->pxx, in->pixel_in);
        if (r->win)
            snprintf(win, sizeof win, "f%d (%d,%d)", r->wf, r->woy, r->wox);
        if (m->wac_o.cal_valid)
            snprintf(och, sizeof och, "%d", m->wac_o.out_ch_sel);
        if (r->mv)
            snprintf(mac, sizeof mac, "f%d %3d %d: %lld", r->mf, r->mpos, r->moch, (long long)m->ob_i.ch_result0);
        if (r->sv)
            snprintf(sum, sizeof sum, "%3d %d = %lld", r->spos, r->soch, (long long)m->ob_o.sum_data);
        if (taken)
            snprintf(fo, sizeof fo, "%3d %d-%d = %u,%u,%u%s", r->opos, r->ooch, r->ooch + CONV_L1_PACK - 1,
                     out->out_data[0], out->out_data[1], out->out_data[2], out->out_ch_done ? "*" : "");

        fprintf(log, "%6ld | %-8s %2d %c%c%c | %d/%d %-18s | %2d,%-4d %-11s | %-10s %3s | %d%d%d %-30s | %3d,%-3d %-24s | %4d %d/%d %-26s%s\n",
                g_cycle, total_ctrl_fsm_l1_state_name(m->fsm.state), m->fsm.ch_count,
                m->fsm_o.mac_start ? 'S' : '.', m->fsm_o.phase_clear ? 'C' : '.', m->wac_o.mac_done ? 'D' : '.',
                in->in_valid, out->in_ready, px,
                m->lb.write_row_num, m->lb.write_col, win,
                weight_addr_ctrl_l1_state_name(m->wac.state), och,
                s1, s2, m->ob_i.mac_valid, mac,
                m->ob.pixel_cnt, m->ob.out_ch_cnt, sum,
                m->rq.u_out_reorder.wr_cnt, out->out_valid, in->out_ready, fo,
                r->err ? "  !!" : "");
    }

    if (csv)
    {
        fprintf(csv, "%ld,%s,%d,%d,%d,%d,%d,%d,%d,", g_cycle, total_ctrl_fsm_l1_state_name(m->fsm.state),
                m->fsm.ch_count, m->fsm_o.mac_start, m->fsm_o.phase_clear, m->wac_o.mac_done,
                in->in_valid, out->in_ready, in->ch_done);
        if (r->px)
            fprintf(csv, "1,%d,%d,%d,%d,", r->pf, r->py, r->pxx, in->pixel_in);
        else
            fprintf(csv, "0,,,,%d,", in->pixel_in);
        fprintf(csv, "%d,%d,", m->lb.write_row_num, m->lb.write_col);
        if (r->win)
            fprintf(csv, "1,%d,%d,%d,", r->wf, r->woy, r->wox);
        else
            fprintf(csv, "0,,,,");
        fprintf(csv, "%s,%d,%d,", weight_addr_ctrl_l1_state_name(m->wac.state), m->wac_o.cal_valid,
                m->wac_o.out_ch_sel);
        fprintf(csv, "%d,%d,%d,", s1, s2, m->ob_i.mac_valid);
        if (r->mv)
            fprintf(csv, "%d,%d,%d,", r->mf, r->mpos, r->moch);
        else
            fprintf(csv, ",,,");
        fprintf(csv, "%lld,%lld,%lld,", (long long)m->ob_i.ch_result0, (long long)m->ob_i.ch_result1,
                (long long)m->ob_i.ch_result2);
        fprintf(csv, "%d,%d,%d,", m->ob.pixel_cnt, m->ob.out_ch_cnt, m->ob_o.sum_valid);
        if (r->sv)
            fprintf(csv, "%d,%d,%d,%lld,", r->sf, r->spos, r->soch, (long long)m->ob_o.sum_data);
        else
            fprintf(csv, ",,,,");
        fprintf(csv, "%d,%d,%d,", m->rq.u_out_reorder.wr_cnt, out->out_valid, in->out_ready);
        if (taken)
            fprintf(csv, "%d,%d,%d,%u,%u,%u,%d,", r->of, r->opos, r->ooch, out->out_data[0], out->out_data[1],
                    out->out_data[2], out->out_ch_done);
        else
            fprintf(csv, ",,,,,,,");
        fprintf(csv, "%ld\n", r->err);
    }
}

/* ================================================================
 * one run
 * ================================================================ */
typedef struct
{
    long     cycles;
    int      py_exact, py_off1, py_worse;
    uint16_t fifo_peak;
} run_res_t;

static conv_l1_t g_dut;

static int run(const scenario_t *sc, const hs_cfg_t *hs, FILE *log, FILE *csv, run_res_t *res)
{
    conv_l1_t *m = &g_dut;
    const int total_in = FRAMES * NPIX;

    memset(g_mon, 0, sizeof g_mon);
    memset(res, 0, sizeof *res);
    make_reference(sc);
    conv_l1_init(m, sc->w, sc->bias, sc->scale_exp);

    uint32_t seed = hs->seed;
    int sent = 0, cur_valid = 0;
    int lbw = 0, wsel = 0, wv = 0, mv = 0, sv = 0, ov = 0;    /* 모니터별 진행 카운터 */
    int n_phase_clear = 0, n_mac_start = 0;
    int16_t held[CE_KK];                                       /* LB 가 마지막으로 낸 window (기대값) */
    total_ctrl_fsm_l1_state_t prev = T1_IDLE;

    log_header(log, csv, sc, hs);

    for (g_cycle = 0; g_cycle < MAX_CYCLES; g_cycle++)
    {
        conv_l1_in_t  in;
        conv_l1_out_t out;
        clk_rec_t     rec;
        long          errs_before = total_errs();

        memset(&rec, 0, sizeof rec);

        /* ---------------- 이전 단: valid 는 받아갈 때까지 유지 ---------------- */
        /* 새 프레임 첫 픽셀은 앞 프레임이 다 출력된 뒤 (reorder 가 비어야 다음 프레임을 받음) */
        int frame_gate = sent > 0 && sent % NPIX == 0 && !cur_valid && !conv_l1_idle(m);
        if (!cur_valid && sent < total_in && !frame_gate)
            cur_valid = (int)(rnd_next(&seed) % 100) < hs->in_pct;

        int i   = sent < total_in ? sent : total_in - 1;
        int f   = i / NPIX;
        int pix = i % NPIX;

        in.in_valid  = (uint8_t)cur_valid;
        in.ch_done   = (uint8_t)(pix == NPIX - 1);
        in.pixel_in  = sc->in[f][pix / CONV_L1_IN_W][pix % CONV_L1_IN_W];
        in.out_ready = (uint8_t)((int)(rnd_next(&seed) % 100) < hs->ready_pct);

        conv_l1_comb(m, &in, &out);

        /* ================= FSM ================= */
        total_ctrl_fsm_l1_state_t st = m->fsm.state;
        if (g_cycle > 0)
            expect(M_FSM, legal_transition(prev, st), "illegal transition %s -> %s",
                   total_ctrl_fsm_l1_state_name(prev), total_ctrl_fsm_l1_state_name(st));
        prev = st;

        if (m->fsm_o.pixel_valid)
        {
            expect(M_FSM, st == T1_IDLE || st == T1_IMG_IN, "frame %d pixel %d accepted in %s", f, pix,
                   total_ctrl_fsm_l1_state_name(st));
            rec.px  = 1;
            rec.pf  = f;
            rec.py  = pix / CONV_L1_IN_W;
            rec.pxx = pix % CONV_L1_IN_W;
        }
        if (st == T1_WAIT_MAC || st == T1_STOP || m->lb.win_valid)
            expect(M_FSM, !out.in_ready, "out_ready = 1 in %s (win_valid %d)",
                   total_ctrl_fsm_l1_state_name(st), m->lb.win_valid);
        if (m->fsm_o.phase_clear)
        {
            /* 프레임 입력이 다 들어온 뒤 STOP 에서만 */
            expect(M_FSM, st == T1_STOP && sent > 0 && sent % NPIX == 0, "phase_clear in %s after %d pixels",
                   total_ctrl_fsm_l1_state_name(st), sent);
            n_phase_clear++;
        }
        if (m->fsm_o.mac_start)
        {
            expect(M_FSM, st == T1_WAIT_MAC, "mac_start in %s", total_ctrl_fsm_l1_state_name(st));
            n_mac_start++;
        }

        /* ================= Line Buffer ================= */
        if (m->lb.win_valid)
        {
            int wf = lbw / NWIN, pos = lbw % NWIN;
            int oy = pos / CONV_L1_OUT_W, ox = pos % CONV_L1_OUT_W, bad = -1;
            for (int k = 0; k < CE_KK; k++)
            {
                held[k] = wf < FRAMES ? sc->in[wf][oy + k / 3][ox + k % 3] : 0;
                if (m->lb.win_out[k] != held[k] && bad < 0)
                    bad = k;
            }
            expect(M_LB, wf < FRAMES && bad < 0, "window %d (frame %d oy %d ox %d) tap %d: got %d expect %d",
                   lbw, wf, oy, ox, bad, bad < 0 ? 0 : m->lb.win_out[bad], bad < 0 ? 0 : held[bad]);
            rec.win = 1;
            rec.wf  = wf;
            rec.woy = oy;
            rec.wox = ox;
            lbw++;
        }

        /* ================= Weight Addr Ctrl / Weight ROM ================= */
        if (m->wac_o.cal_valid)
        {
            int win = wv / CONV_L1_C_OUT, och = wsel % CONV_L1_C_OUT, bad = -1;

            expect(M_WROM, m->wac_o.out_ch_sel == och, "out_ch_sel %d expect %d", m->wac_o.out_ch_sel, och);
            for (int k = 0; k < CE_KK && bad < 0; k++)
                if (m->mac_weight[0][k] != sc->w[och * CE_KK + k])
                    bad = k;
            for (int lane = 1; lane < CE_LANES; lane++)
                for (int k = 0; k < CE_KK; k++)
                    if (bad < 0 && m->mac_weight[lane][k] != 0)
                        bad = lane * CE_KK + k;
            expect(M_WROM, bad < 0, "weight_in och %d lane %d tap %d wrong", och, bad / CE_KK, bad % CE_KK);
            expect(M_WROM, lbw == win + 1 && !memcmp(m->lb.win_out, held, sizeof held),
                   "window not held while MAC runs (window %d, LB at %d)", win, lbw);
            wsel++;
            wv++;
        }

        /* ================= MAC Array ================= */
        if (m->ob_i.mac_valid)
        {
            int win = mv / CONV_L1_C_OUT, och = mv % CONV_L1_C_OUT;
            int wf = win / NWIN, pos = win % NWIN;
            int ok = wf < FRAMES && m->ob_i.ch_result0 == g_tap[wf][pos][och] &&
                     m->ob_i.ch_result1 == 0 && m->ob_i.ch_result2 == 0;
            expect(M_MAC, ok, "frame %d pos %d och %d: got %lld %lld %lld expect %lld 0 0", wf, pos, och,
                   (long long)m->ob_i.ch_result0, (long long)m->ob_i.ch_result1, (long long)m->ob_i.ch_result2,
                   wf < FRAMES ? (long long)g_tap[wf][pos][och] : 0LL);
            rec.mv   = 1;
            rec.mf   = wf;
            rec.mpos = pos;
            rec.moch = och;
            mv++;
        }

        /* ================= Output Buffer ================= */
        if (m->ob_o.sum_valid)
        {
            int of = sv / NVAL, pos = (sv / CONV_L1_C_OUT) % CONV_L1_N, och = sv % CONV_L1_C_OUT;
            expect(M_OB, of < FRAMES && m->ob_o.sum_data == g_sum[of][pos][och],
                   "frame %d pos %d och %d: sum %lld expect %lld", of, pos, och,
                   (long long)m->ob_o.sum_data, of < FRAMES ? (long long)g_sum[of][pos][och] : 0LL);
            rec.sv   = 1;
            rec.sf   = of;
            rec.spos = pos;
            rec.soch = och;
            sv++;
        }

        /* ================= FIFO 출력: entry = PACK 개 값 ================= */
        if (out.out_valid && in.out_ready)
        {
            /* entry 순서: frame -> group (och0~2, och3~5) -> pixel */
            int of = ov / (2 * CONV_L1_N), grp = (ov / CONV_L1_N) % 2, pos = ov % CONV_L1_N;
            int och0 = grp * CONV_L1_PACK;
            for (int j = 0; j < CONV_L1_PACK; j++)
            {
                int och = och0 + j;
                expect(M_OUT, of < FRAMES && out.out_data[j] == g_code[of][pos][och],
                       "frame %d pos %d och %d: code %u expect %u", of, pos, och, out.out_data[j],
                       of < FRAMES ? g_code[of][pos][och] : 0);
                if (of < sc->py_frames)
                {
                    int d = abs((int)out.out_data[j] - (int)sc->py[och][pos]);
                    res->py_exact += d == 0;
                    res->py_off1  += d == 1;
                    res->py_worse += d > 1;
                }
            }
            expect(M_OUT, out.out_ch_done == (pos == CONV_L1_N - 1), "out_ch_done %d at pos %d",
                   out.out_ch_done, pos);
            rec.ov   = 1;
            rec.of   = of;
            rec.opos = pos;
            rec.ooch = och0;
            ov++;
        }

        rec.err = total_errs() - errs_before;
        log_row(log, csv, m, &in, &out, &rec);

        /* ---------------- posedge clk ---------------- */
        if (in.in_valid && out.in_ready)
        {
            sent++;
            cur_valid = 0;
        }
        conv_l1_seq(m);

        if (sent == total_in && ov * CONV_L1_PACK >= FRAMES * NVAL && conv_l1_idle(m))
            break;
    }

    /* ---------------- 개수 / 상태 검사 ---------------- */
    expect(M_FSM, g_cycle < MAX_CYCLES, "timeout: sent %d/%d, FSM %s, WAC %s", sent, total_in,
           total_ctrl_fsm_l1_state_name(m->fsm.state), weight_addr_ctrl_l1_state_name(m->wac.state));
    expect(M_FSM, n_mac_start == FRAMES * NWIN, "mac_start %d expect %d", n_mac_start, FRAMES * NWIN);
    expect(M_FSM, n_phase_clear == FRAMES, "phase_clear %d expect %d", n_phase_clear, FRAMES);
    expect(M_LB, lbw == FRAMES * NWIN, "windows %d expect %d", lbw, FRAMES * NWIN);
    expect(M_WROM, wv == FRAMES * NWIN * CONV_L1_C_OUT, "cal_valid %d expect %d", wv,
           FRAMES * NWIN * CONV_L1_C_OUT);
    expect(M_MAC, mv == FRAMES * NWIN * CONV_L1_C_OUT, "mac_valid %d expect %d", mv,
           FRAMES * NWIN * CONV_L1_C_OUT);
    expect(M_OB, sv == FRAMES * NVAL, "sum_valid %d expect %d", sv, FRAMES * NVAL);
    expect(M_OB, m->ob.dbg_ch_ovf_cnt == 0 && m->ob.dbg_acc_ovf_cnt == 0,
           "width overflow ch %u acc %u", m->ob.dbg_ch_ovf_cnt, m->ob.dbg_acc_ovf_cnt);
    expect(M_OUT, ov * CONV_L1_PACK == FRAMES * NVAL, "outputs %d expect %d", ov * CONV_L1_PACK, FRAMES * NVAL);
    expect(M_OUT, m->rq.u_out_reorder.dbg_overrun_cnt == 0, "reorder overrun %u (next frame pushed before read-out)",
           m->rq.u_out_reorder.dbg_overrun_cnt);

    res->cycles    = g_cycle;
    res->fifo_peak = m->rq.u_out_reorder.dbg_max_fill;

    return total_errs() || res->py_worse;
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
    ok = ok && hdr[0] == CONV_L1_C_IN && hdr[1] == CONV_L1_IN_H && hdr[2] == CONV_L1_IN_W &&
         hdr[3] == CONV_L1_C_OUT && hdr[4] == CONV_L1_PACK;
    for (int p = 0; ok && p < NPIX; p++)
    {
        ok = fscanf(f, "%d", &v) == 1;
        sc->in[0][p / CONV_L1_IN_W][p % CONV_L1_IN_W] = (int16_t)v;
        /* frame 1: 같은 이미지 좌우 반전 (Python 기대값 없음, 정수 레퍼런스로만 비교) */
        sc->in[1][p / CONV_L1_IN_W][CONV_L1_IN_W - 1 - p % CONV_L1_IN_W] = (int16_t)v;
    }
    for (int i = 0; ok && i < NW; i++)
    {
        ok = fscanf(f, "%d", &v) == 1;
        sc->w[i] = (wgt_t)v;
    }
    for (int i = 0; ok && i < CONV_L1_C_OUT; i++)
        ok = fscanf(f, "%d", &sc->bias[i]) == 1;
    for (int oc = 0; ok && oc < CONV_L1_C_OUT; oc++)
        for (int p = 0; ok && p < CONV_L1_N; p++)
        {
            ok = fscanf(f, "%d", &v) == 1;
            sc->py[oc][p] = (uint16_t)v;
        }
    if (f)
        fclose(f);
    if (!ok)
    {
        printf("%s: missing or malformed (run: make -f conv_l1.mk vectors)\n", path);
        return 1;
    }
    sc->name      = "real";
    sc->desc      = "Python input image (frame 2 = mirrored), real weight/bias";
    sc->scale_exp = (uint8_t)hdr[5];
    sc->py_frames = 1;
    return 0;
}

static void make_pattern(scenario_t *sc)
{
    uint32_t s = 0x1234567u;

    sc->name      = "pattern";
    sc->desc      = "unique pixel values (y*32 + x + 1), small random weights";
    sc->scale_exp = 8;
    for (int i = 0; i < NW; i++)
        sc->w[i] = (wgt_t)((int)(rnd_next(&s) % 128) - 64);
    for (int oc = 0; oc < CONV_L1_C_OUT; oc++)
        sc->bias[oc] = (int32_t)(rnd_next(&s) % 4096) - 2048;
    for (int y = 0; y < CONV_L1_IN_H; y++)
        for (int x = 0; x < CONV_L1_IN_W; x++)
        {
            int v = y * 32 + x + 1;
            sc->in[0][y][x] = (int16_t)v;
            sc->in[1][y][x] = (int16_t)(1000 - v);
        }
}

static void make_impulse(scenario_t *sc)
{
    sc->name      = "impulse";
    sc->desc      = "one pixel = 1 (inside / 4 corners), weight = (och+1)*100 + tap, bias 0";
    sc->scale_exp = 0;
    memset(sc->in, 0, sizeof sc->in);
    memset(sc->bias, 0, sizeof sc->bias);
    for (int oc = 0; oc < CONV_L1_C_OUT; oc++)
        for (int k = 0; k < CE_KK; k++)
            sc->w[oc * CE_KK + k] = (wgt_t)((oc + 1) * 100 + k);
    sc->in[0][13][7] = 1;                                           /* 안쪽 */
    sc->in[1][0][0]  = sc->in[1][0][27] = sc->in[1][27][0] = sc->in[1][27][27] = 1;   /* 모서리 */
}

static void make_random(scenario_t *sc)
{
    uint32_t s = 0xC0FFEEu;

    sc->name      = "random";
    sc->desc      = "input and weight full INT16 range (negatives included), random INT31 bias";
    sc->scale_exp = 18;
    for (int i = 0; i < NW; i++)
        sc->w[i] = (wgt_t)(rnd_next(&s) & 0xFFFF);
    for (int oc = 0; oc < CONV_L1_C_OUT; oc++)
        sc->bias[oc] = (int32_t)rnd_next(&s) >> 1;
    for (int f = 0; f < FRAMES; f++)
        for (int y = 0; y < CONV_L1_IN_H; y++)
            for (int x = 0; x < CONV_L1_IN_W; x++)
                sc->in[f][y][x] = (int16_t)(rnd_next(&s) & 0xFFFF);
}

static void make_rounding(scenario_t *sc)
{
    uint32_t s = 0xBADC0DEu;

    /* shift 2: remainder 0..3, so sums hit the .5 tie (rem == 2) with both even and odd q */
    sc->name      = "rounding";
    sc->desc      = "small input 0..15, weight -8..7, shift 2 -> many exact .5 ties, negatives -> ReLU";
    sc->scale_exp = 2;
    for (int i = 0; i < NW; i++)
        sc->w[i] = (wgt_t)((int)(rnd_next(&s) % 16) - 8);
    for (int oc = 0; oc < CONV_L1_C_OUT; oc++)
        sc->bias[oc] = (int32_t)(rnd_next(&s) % 64) - 32;
    for (int f = 0; f < FRAMES; f++)
        for (int y = 0; y < CONV_L1_IN_H; y++)
            for (int x = 0; x < CONV_L1_IN_W; x++)
                sc->in[f][y][x] = (int16_t)(rnd_next(&s) % 16);
}

static void make_extreme(scenario_t *sc)
{
    sc->name      = "extreme";
    sc->desc      = "weight all -32768, input all -32768 (max sum) / 32767 (min sum), INT32 edge bias";
    sc->scale_exp = 16;
    for (int i = 0; i < NW; i++)
        sc->w[i] = INT16_MIN;
    for (int oc = 0; oc < CONV_L1_C_OUT; oc++)
        sc->bias[oc] = (oc & 1) ? INT32_MIN : INT32_MAX;
    for (int y = 0; y < CONV_L1_IN_H; y++)
        for (int x = 0; x < CONV_L1_IN_W; x++)
        {
            sc->in[0][y][x] = INT16_MIN;
            sc->in[1][y][x] = INT16_MAX;
        }
}

/* ================================================================
 * main
 * ================================================================ */
static scenario_t g_sc[6];

static void usage(void)
{
    printf("usage: test_conv_l1 [vectors/conv_l1.txt] [-l] [-s scenario|all] [-h A|B|C|D|all]\n"
           "  (no option)  run every scenario x handshake condition\n"
           "  -s, -h       run only the selected scenario / handshake condition\n"
           "  -l           write logs/conv_l1_<scenario>_<cond>.log (readable) and .csv per run;\n"
           "               with -l alone, only real / A is run\n"
           "  scenarios: real pattern impulse random rounding extreme\n");
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
    make_impulse(&g_sc[2]);
    make_random(&g_sc[3]);
    make_rounding(&g_sc[4]);
    make_extreme(&g_sc[5]);
    const int n_sc = (int)(sizeof g_sc / sizeof g_sc[0]);

    printf("conv_l1: %dx%dx%d -> %dx%dx%d, %d frames per run\n", CONV_L1_IN_H, CONV_L1_IN_W,
           CONV_L1_C_IN, CONV_L1_OUT_H, CONV_L1_OUT_W, CONV_L1_C_OUT, FRAMES);
    printf("handshake: ");
    for (int h = 0; h < N_HS; h++)
        printf("%s = in_valid %d%% / out_ready %d%%%s", g_hs[h].name, g_hs[h].in_pct,
               g_hs[h].ready_pct, h + 1 < N_HS ? ", " : "\n\n");

    for (int s = 0; s < n_sc; s++)
    {
        if (sel_sc && strcmp(sel_sc, g_sc[s].name))
            continue;
        printf("%s: %s (shift %d)\n", g_sc[s].name, g_sc[s].desc, g_sc[s].scale_exp);
        for (int h = 0; h < N_HS; h++)
        {
            if (sel_hs && strcmp(sel_hs, g_hs[h].name))
                continue;

            FILE *log = NULL, *csv = NULL;
            char  name[96];
            if (want_log)
            {
                snprintf(name, sizeof name, "logs/conv_l1_%s_%s.log", g_sc[s].name, g_hs[h].name);
                log = fopen(name, "w");
                snprintf(name, sizeof name, "logs/conv_l1_%s_%s.csv", g_sc[s].name, g_hs[h].name);
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
            printf(" | %ld cyc, reorder fill peak %u", r.cycles, r.fifo_peak);
            if (g_sc[s].py_frames)
                printf(" | Python exact %d, +-1 %d, worse %d", r.py_exact, r.py_off1, r.py_worse);
            printf("\n");
            if (want_log)
                printf("         -> logs/conv_l1_%s_%s.log / .csv (%ld lines)\n", g_sc[s].name,
                       g_hs[h].name, r.cycles + 1);
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
