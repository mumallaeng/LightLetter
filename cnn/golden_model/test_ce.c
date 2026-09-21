/*
 * CE end-to-end test: conv1 -> MaxPool -> conv2, bit-exact against the Python golden model.
 *
 *   make -f ce.mk test
 *   (or: build/test_ce [vectors/ce_lenet5.txt] [-t])   -t: write ce_conv{1,2}_trace.csv for the first run
 *
 * Each run:
 *   1. conv1 CE (line_buffer x1, 28x28x1) on the real input image
 *      -> FIFO entries decoded back to [6][26][26]
 *   2. MaxPool 2x2 (behavioral, testbench only - the MaxPooling module is not part of this model)
 *      -> [6][13][13]
 *   3. conv2 CE (line_buffer_array, 13x13x6, 2 passes) fed with the pooled conv1 output
 *      -> [16][11][11]
 *
 * Pass criteria per layer:
 *   - bit-exact against an integer reference (int64 conv + bias, ReLU, round-half-even >> SCALE_EXP,
 *     clip 32767) computed here from the same input
 *   - against Python ("convN + ReLU (quantized)", "convN + MaxPool") no value off by more than 1:
 *     Python runs the MAC in float32, so a sum sitting on a .5 rounding boundary may differ by one
 *
 * The previous stage streams pass 0 (ch0~2) raster, then pass 1 (ch3~5) raster, with ch_done on
 * the last pixel of each pass. in_valid stays up until the pixel is accepted.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ce_top.h"

#define MAX_CYCLES  4000000L
#define MAX_FRAMES  2

typedef struct
{
    ce_param_t p;
    int        oh, ow, ph, pw;
    int16_t   *in;      /* [c_in][h][w]   */
    wgt_t     *w;       /* [c_out][c_in][3][3] */
    int32_t   *bias;    /* [c_out] */
    uint16_t  *conv;    /* [c_out][oh][ow] */
    uint16_t  *pool;    /* [c_out][ph][pw] */
} layer_vec_t;

typedef struct
{
    int      frames;
    int      in_pct;        /* in_valid 확률 */
    int      ready_pct;     /* out_ready 확률 */
    uint32_t seed;
} run_cfg_t;

typedef struct
{
    long     cycles;
    int      sent, recv;
    int      done_err;      /* out_ch_done 위치 오류 */
    int      extra;         /* 기대보다 많이 나온 값 */
    uint32_t fifo_ovf, sat, ob_ch_ovf, ob_acc_ovf;
    uint16_t fifo_peak;
} run_stat_t;

static ce_top_t g_top;
static int      g_fail;

/* ================================================================
 * vectors
 * ================================================================ */
static int read_ints(FILE *f, int n, int64_t *dst)
{
    for (int i = 0; i < n; i++)
    {
        long long v;
        if (fscanf(f, "%lld", &v) != 1)
            return 1;
        dst[i] = v;
    }
    return 0;
}

static void *alloc_copy(const int64_t *src, int n, int elem)
{
    char *p = malloc((size_t)n * elem);
    for (int i = 0; i < n; i++)
    {
        if (elem == 2)
            ((int16_t *)p)[i] = (int16_t)src[i];
        else
            ((int32_t *)p)[i] = (int32_t)src[i];
    }
    return p;
}

static int load_layer(FILE *f, int layer, layer_vec_t *lv)
{
    int64_t hdr[6];
    static int64_t tmp[16 * 6 * 28 * 28];

    if (read_ints(f, 6, hdr))
        return 1;

    ce_param_t *p = &lv->p;
    p->layer     = (uint8_t)layer;
    p->c_in      = (uint8_t)hdr[0];
    p->in_h      = (uint8_t)hdr[1];
    p->in_w      = (uint8_t)hdr[2];
    p->c_out     = (uint8_t)hdr[3];
    p->pack      = (uint8_t)hdr[4];
    p->scale_exp = (uint8_t)hdr[5];
    lv->oh = p->in_h - 2;
    lv->ow = p->in_w - 2;
    lv->ph = lv->oh / 2;
    lv->pw = lv->ow / 2;

    int n_in = p->c_in * p->in_h * p->in_w, n_w = p->c_out * p->c_in * CE_KK;
    int n_conv = p->c_out * lv->oh * lv->ow, n_pool = p->c_out * lv->ph * lv->pw;

    if (read_ints(f, n_in, tmp))
        return 1;
    lv->in = alloc_copy(tmp, n_in, 2);
    if (read_ints(f, n_w, tmp))
        return 1;
    lv->w = alloc_copy(tmp, n_w, 2);
    if (read_ints(f, p->c_out, tmp))
        return 1;
    lv->bias = alloc_copy(tmp, p->c_out, 4);
    if (read_ints(f, n_conv, tmp))
        return 1;
    lv->conv = alloc_copy(tmp, n_conv, 2);
    if (read_ints(f, n_pool, tmp))
        return 1;
    lv->pool = alloc_copy(tmp, n_pool, 2);
    return 0;
}

/* ================================================================
 * helpers
 * ================================================================ */
static uint32_t rnd(uint32_t *s)
{
    *s ^= *s << 13;
    *s ^= *s >> 17;
    *s ^= *s << 5;
    return *s;
}

/* MaxPool 2x2 stride 2 (floor), [c][h][w] -> [c][h/2][w/2] */
static void maxpool(const uint16_t *x, int c, int h, int w, uint16_t *y)
{
    for (int ch = 0; ch < c; ch++)
        for (int py = 0; py < h / 2; py++)
            for (int px = 0; px < w / 2; px++)
            {
                uint16_t m = 0;
                for (int dy = 0; dy < 2; dy++)
                    for (int dx = 0; dx < 2; dx++)
                    {
                        uint16_t v = x[(ch * h + 2 * py + dy) * w + 2 * px + dx];
                        if (v > m)
                            m = v;
                    }
                y[(ch * (h / 2) + py) * (w / 2) + px] = m;
            }
}

/* integer reference: [c_in][h][w] -> [c_out][h-2][w-2] codes */
static void ref_conv(const layer_vec_t *lv, const int16_t *x, uint16_t *y)
{
    const ce_param_t *p = &lv->p;
    const int s = p->scale_exp;

    for (int oc = 0; oc < p->c_out; oc++)
        for (int oy = 0; oy < lv->oh; oy++)
            for (int ox = 0; ox < lv->ow; ox++)
            {
                int64_t acc = lv->bias[oc];
                for (int ic = 0; ic < p->c_in; ic++)
                    for (int ky = 0; ky < CE_K; ky++)
                        for (int kx = 0; kx < CE_K; kx++)
                            acc += (int64_t)x[(ic * p->in_h + oy + ky) * p->in_w + ox + kx] *
                                   lv->w[((oc * p->c_in + ic) * CE_K + ky) * CE_K + kx];

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
                y[(oc * lv->oh + oy) * lv->ow + ox] = (uint16_t)(q > 32767 ? 32767 : q);
            }
}

typedef struct
{
    int exact, off1, worse;
} diff_t;

/* counts |got - exp| = 0 / 1 / >1, prints the first few above `tol` */
static diff_t compare(const char *what, const uint16_t *got, const uint16_t *exp, int c, int h,
                      int w, int tol)
{
    diff_t d = {0, 0, 0};
    int shown = 0;

    for (int i = 0; i < c * h * w; i++)
    {
        int e = abs((int)got[i] - (int)exp[i]);
        d.exact += (e == 0);
        d.off1  += (e == 1);
        d.worse += (e > 1);
        if (e > tol && shown++ < 5)
            printf("    %s: ch %d (y %d, x %d) got %u expect %u\n", what,
                   i / (h * w), (i / w) % h, i % w, got[i], exp[i]);
    }
    return d;
}

static void trace_header(FILE *f)
{
    fprintf(f, "cycle,tfsm,wac,in_valid,in_ready,ch_done,pixel_valid,win_valid,phase_clear,"
               "is_ch35,ch_count,mac_start,mac_done,out_ch_sel,weight_valid,rom_grp,"
               "mac_valid,ch_result0,ch_result1,ch_result2,ob_state,sum_valid,sum_data,sum_ch_done,"
               "out_valid,out_ready,out_data0,out_data1,out_data2,out_ch_done\n");
}

static void trace_row(FILE *f, long cycle, const ce_top_t *t, const ce_in_t *in, const ce_out_t *o)
{
    fprintf(f, "%ld,%s,%s,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%lld,%lld,%lld,%s,%d,%lld,%d,"
               "%d,%d,%u,%u,%u,%d\n",
            cycle, total_state_name(t->fsm.state), wac_state_name(t->wac.state),
            in->in_valid, o->in_ready, in->ch_done, t->fsm_o.pixel_valid, t->lb_win_valid,
            t->fsm_o.phase_clear, t->fsm_o.is_ch35, t->fsm.ch_count, t->fsm_o.mac_start,
            t->wac_o.mac_done, t->wac_o.out_ch_sel, t->wac_o.weight_valid, t->rom_o.grp,
            t->ob_i.mac_valid, (long long)t->ob_i.ch_result0, (long long)t->ob_i.ch_result1,
            (long long)t->ob_i.ch_result2, ob_state_name(t->ob.state), t->ob_o.sum_valid,
            (long long)t->ob_o.sum_data, t->ob_o.ch_done, o->out_valid, in->out_ready,
            o->out_data0, o->out_data1, o->out_data2, o->out_ch_done);
}

/* ================================================================
 * one layer, cfg->frames frames back to back
 *   input : [frames][c_in][h][w]
 *   output: [frames][c_out][oh][ow]
 * ================================================================ */
static int run_layer(const layer_vec_t *lv, const int16_t *input, const run_cfg_t *cfg,
                     FILE *trace, uint16_t *output, run_stat_t *st)
{
    const ce_param_t *p = &lv->p;
    ce_top_t *t = &g_top;

    if (ce_top_init(t, p, lv->w, lv->bias))
        return 1;

    int passes    = (p->c_in + CE_LANES - 1) / CE_LANES;
    int pix_pass  = p->in_h * p->in_w;
    int n         = lv->oh * lv->ow;
    int total_in  = cfg->frames * passes * pix_pass;
    int total_out = cfg->frames * n * p->c_out;

    uint32_t seed = cfg->seed;
    int      cur_valid = 0;

    memset(st, 0, sizeof(*st));
    if (trace)
        trace_header(trace);

    for (st->cycles = 0; st->cycles < MAX_CYCLES; st->cycles++)
    {
        ce_in_t  in;
        ce_out_t out;

        /* ---- 이전 단: valid 는 받아갈 때까지 유지 ---- */
        if (!cur_valid && st->sent < total_in)
            cur_valid = (int)(rnd(&seed) % 100) < cfg->in_pct;

        int i     = st->sent < total_in ? st->sent : total_in - 1;
        int frame = i / (passes * pix_pass);
        int pass  = (i / pix_pass) % passes;
        int pix   = i % pix_pass;
        const int16_t *img = input + (size_t)frame * p->c_in * pix_pass;

        in.in_valid = (uint8_t)cur_valid;
        in.ch_done  = (uint8_t)(pix == pix_pass - 1);
        for (int lane = 0; lane < CE_LANES; lane++)
        {
            int ic = pass * CE_LANES + lane;
            in.pixel_in[lane] = ic < p->c_in ? img[ic * pix_pass + pix] : 0;
        }

        /* ---- 다음 단 (MaxPooling) ready ---- */
        in.out_ready = (uint8_t)((int)(rnd(&seed) % 100) < cfg->ready_pct);

        ce_top_comb(t, &in, &out);
        if (trace)
            trace_row(trace, st->cycles, t, &in, &out);

        /* ---- 출력 수집: 한 entry = PACK 개 값, 순서 pixel -> och ---- */
        if (out.out_valid && in.out_ready)
        {
            uint16_t d[3] = {out.out_data0, out.out_data1, out.out_data2};
            for (int j = 0; j < p->pack; j++)
            {
                if (st->recv >= total_out)
                {
                    st->extra++;
                    continue;
                }
                int v  = st->recv++;
                int fr = v / (n * p->c_out);
                int px = (v % (n * p->c_out)) / p->c_out;
                int oc = v % p->c_out;
                output[(size_t)fr * p->c_out * n + oc * n + px] = d[j];
                if (j == 0 && out.out_ch_done != (px == n - 1))
                    st->done_err++;
            }
        }

        /* ---- posedge clk ---- */
        if (in.in_valid && out.in_ready)
        {
            st->sent++;
            cur_valid = 0;
        }
        ce_top_seq(t);

        if (st->sent == total_in && st->recv >= total_out && ce_top_idle(t))
            break;
    }

    st->fifo_ovf   = t->rq.u_output_fifo.dbg_overflow_cnt;
    st->fifo_peak  = t->rq.u_output_fifo.dbg_max_count;
    st->sat        = t->rq.dbg_sat_cnt;
    st->ob_ch_ovf  = t->ob.dbg_ch_ovf_cnt;
    st->ob_acc_ovf = t->ob.dbg_acc_ovf_cnt;
    ce_top_free(t);

    return st->cycles >= MAX_CYCLES || st->recv != total_out || st->extra || st->done_err ||
           st->fifo_ovf || st->ob_ch_ovf || st->ob_acc_ovf;
}

/* ================================================================
 * per-layer check + report
 * ================================================================ */
static int check_layer(const char *name, const layer_vec_t *lv, const run_cfg_t *cfg,
                       const int16_t *input, FILE *trace, uint16_t *conv_out, uint16_t *pool_out)
{
    const ce_param_t *p = &lv->p;
    int n_in   = p->c_in * p->in_h * p->in_w;
    int n_conv = p->c_out * lv->oh * lv->ow, n_pool = p->c_out * lv->ph * lv->pw;
    uint16_t *ref = malloc(sizeof(uint16_t) * n_conv);
    run_stat_t st;
    int ref_bad = 0;
    diff_t py = {0, 0, 0};

    int err = run_layer(lv, input, cfg, trace, conv_out, &st);

    for (int f = 0; f < cfg->frames && st.recv == cfg->frames * n_conv; f++)
    {
        diff_t d;

        maxpool(conv_out + f * n_conv, p->c_out, lv->oh, lv->ow, pool_out + f * n_pool);

        ref_conv(lv, input + (size_t)f * n_in, ref);
        d = compare("vs int ref", conv_out + f * n_conv, ref, p->c_out, lv->oh, lv->ow, 0);
        ref_bad += d.off1 + d.worse;

        d = compare("conv vs Python", conv_out + f * n_conv, lv->conv, p->c_out, lv->oh, lv->ow, 1);
        py.exact += d.exact;
        py.off1  += d.off1;
        py.worse += d.worse;
        d = compare("pool vs Python", pool_out + f * n_pool, lv->pool, p->c_out, lv->ph, lv->pw, 1);
        py.worse += d.worse;
    }
    free(ref);

    int fail = err || ref_bad || py.worse;
    printf("[%s] %s frames %d in %3d%% ready %3d%% : %d/%d values, int-ref mismatch %d | Python "
           "exact %d, off1 %d, worse %d | %ld cycles (%ld/frame), FIFO peak %u ovf %u, sat %u\n",
           fail ? "FAIL" : " ok ", name, cfg->frames, cfg->in_pct, cfg->ready_pct, st.recv,
           cfg->frames * n_conv, ref_bad, py.exact, py.off1, py.worse, st.cycles,
           st.cycles / cfg->frames, st.fifo_peak, st.fifo_ovf, st.sat);
    if (st.done_err || st.extra || st.ob_ch_ovf || st.ob_acc_ovf)
        printf("    out_ch_done err %d, extra outputs %d, OB width overflow %u/%u\n",
               st.done_err, st.extra, st.ob_ch_ovf, st.ob_acc_ovf);
    if (st.cycles >= MAX_CYCLES)
        printf("    TIMEOUT (sent %d, FSM %s, WAC %s)\n", st.sent,
               total_state_name(g_top.fsm.state), wac_state_name(g_top.wac.state));
    return fail;
}

int main(int argc, char **argv)
{
    const char *path = "vectors/ce_lenet5.txt";
    int want_trace = 0;

    for (int i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "-t"))
            want_trace = 1;
        else
            path = argv[i];
    }

    FILE *f = fopen(path, "r");
    if (!f)
    {
        perror(path);
        return 1;
    }
    layer_vec_t l1, l2;
    if (load_layer(f, 1, &l1) || load_layer(f, 2, &l2))
    {
        printf("%s: malformed vector file\n", path);
        return 1;
    }
    fclose(f);

    if (l2.p.c_in != l1.p.c_out || l2.p.in_h != l1.ph || l2.p.in_w != l1.pw)
    {
        printf("conv1 pool output (%dx%dx%d) does not match conv2 input (%dx%dx%d)\n",
               l1.p.c_out, l1.ph, l1.pw, l2.p.c_in, l2.p.in_h, l2.p.in_w);
        return 1;
    }

    printf("CE end-to-end: conv1 %dx%dx%d -> %d ch (PACK %d, shift %d) | conv2 %dx%dx%d -> %d ch "
           "(PACK %d, shift %d)\n",
           l1.p.c_in, l1.p.in_h, l1.p.in_w, l1.p.c_out, l1.p.pack, l1.p.scale_exp,
           l2.p.c_in, l2.p.in_h, l2.p.in_w, l2.p.c_out, l2.p.pack, l2.p.scale_exp);

    static const run_cfg_t cfgs[] = {
        {1, 100, 100, 1},
        {2, 100, 100, 2},
        {2,  70,  60, 3},
        {2,  40,  50, 4},
        {2, 100,  30, 5},
    };
    int n1 = l1.p.c_out * l1.oh * l1.ow, p1 = l1.p.c_out * l1.ph * l1.pw;
    int n2 = l2.p.c_out * l2.oh * l2.ow, p2 = l2.p.c_out * l2.ph * l2.pw;

    uint16_t *conv1 = calloc((size_t)MAX_FRAMES * n1, sizeof(uint16_t));
    uint16_t *pool1 = calloc((size_t)MAX_FRAMES * p1, sizeof(uint16_t));
    uint16_t *conv2 = calloc((size_t)MAX_FRAMES * n2, sizeof(uint16_t));
    uint16_t *pool2 = calloc((size_t)MAX_FRAMES * p2, sizeof(uint16_t));
    int16_t  *in1   = malloc((size_t)MAX_FRAMES * l1.p.c_in * l1.p.in_h * l1.p.in_w * sizeof(int16_t));
    int16_t  *in2   = malloc((size_t)MAX_FRAMES * p1 * sizeof(int16_t));

    for (int fr = 0; fr < MAX_FRAMES; fr++)
        memcpy(in1 + (size_t)fr * l1.p.c_in * l1.p.in_h * l1.p.in_w, l1.in,
               sizeof(int16_t) * l1.p.c_in * l1.p.in_h * l1.p.in_w);

    for (size_t c = 0; c < sizeof cfgs / sizeof cfgs[0]; c++)
    {
        const run_cfg_t *cfg = &cfgs[c];
        FILE *t1 = (want_trace && c == 0) ? fopen("ce_conv1_trace.csv", "w") : NULL;
        FILE *t2 = (want_trace && c == 0) ? fopen("ce_conv2_trace.csv", "w") : NULL;

        g_fail |= check_layer("conv1", &l1, cfg, in1, t1, conv1, pool1);

        /* conv1 -> MaxPool -> conv2 : conv2 는 이 run 의 conv1 결과로 돈다 */
        for (int i = 0; i < cfg->frames * p1; i++)
            in2[i] = (int16_t)pool1[i];
        g_fail |= check_layer("conv2", &l2, cfg, in2, t2, conv2, pool2);

        if (t1) fclose(t1);
        if (t2) fclose(t2);
    }

    printf("\n%s\n", g_fail ? "FAIL" : "ALL PASS");
    return g_fail ? 1 : 0;
}
