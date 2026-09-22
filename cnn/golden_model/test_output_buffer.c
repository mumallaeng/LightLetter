/*
 * Unit tests for the Output Buffer / ReLU & Quantization golden model.
 *
 *   make -f output_buffer.mk test
 *   (or: build/test_output_buffer [vector_dir], default ./vectors - see export_ob_vectors.py)
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "output_buffer.h"
#include "relu_quant.h"

#define MAX_STIM (2 * 676 * 16)
#define MAX_OUT (676 * 16)

static output_buffer_t ob;
static relu_quant_t rq;

static int g_fail;

#define CHECK(cond, ...)                          \
    do                                            \
    {                                             \
        if (!(cond))                              \
        {                                         \
            if (g_fail < 20)                      \
            {                                     \
                printf("    FAIL: " __VA_ARGS__); \
                printf("\n");                     \
            }                                     \
            g_fail++;                             \
        }                                         \
    } while (0)

static int end_test(const char *name, int before)
{
    int failed = g_fail - before;
    printf("[%s] %s\n", failed ? "FAIL" : " ok ", name);
    return failed;
}

/* independent quantizer reference: rint() rounds half to even */
static uint16_t ref_quant(int64_t x, int shift)
{
    if (x < 0)
        x = 0;
    double q = rint(ldexp((double)x, -shift));
    return q > 32767.0 ? 32767 : (uint16_t)q;
}

static int64_t rand_signed(int bits)
{
    int64_t v = ((((int64_t)rand() << 31) ^ ((int64_t)rand() << 15) ^ rand())) & (((int64_t)1 << (bits - 1)) - 1);
    return (rand() & 1) ? -v : v;
}

/* ---------------------------------------------------------------- quantizer */
static void test_quantizer(void)
{
    int before = g_fail;
    static const struct
    {
        int64_t x;
        int shift;
        uint16_t want;
    } v[] = {
        {-5, 2, 0},
        {0, 3, 0},
        {2, 2, 0},
        {6, 2, 2},
        {10, 2, 2},
        {14, 2, 4},
        {1, 1, 0},
        {3, 1, 2},
        {5, 1, 2},
        {7, 0, 7},
        {((int64_t)32767 << 4) + 7, 4, 32767},
        {((int64_t)32767 << 4) + 8, 4, 32767},
        {(int64_t)1 << 38, 4, 32767},
        {-((int64_t)1 << 38), 4, 0},
    };
    for (unsigned i = 0; i < sizeof v / sizeof v[0]; i++)
    {
        uint16_t got = quantizer_comb(relu_comb(v[i].x), (uint8_t)v[i].shift, 0);
        CHECK(got == v[i].want, "x=%lld shift=%d got %u want %u", (long long)v[i].x, v[i].shift, got, v[i].want);
    }
    for (int i = 0; i < 20000; i++)
    {
        int shift = rand() % 20;
        int64_t x = rand_signed(shift + 18);
        uint16_t got = quantizer_comb(relu_comb(x), (uint8_t)shift, 0);
        CHECK(got == ref_quant(x, shift), "x=%lld shift=%d got %u want %u", (long long)x, shift, got, ref_quant(x, shift));
    }
    end_test("quantizer: relu, round-half-to-even, clamp", before);
}

/* ------------------------------------------------------------ output buffer */
static int64_t stim[MAX_STIM][3];
static int64_t ref_sum[2 * MAX_OUT];

static void test_output_buffer(const ob_param_t *p, unsigned seed)
{
    int before = g_fail;
    const int frames = 2, n = p->n, co = p->c_out, g = p->num_groups;
    const int per_pass = n * co, per_frame = g * per_pass;
    int32_t bias[OB_MAX_C_OUT];
    char name[96];

    srand(seed);
    for (int i = 0; i < co; i++)
        bias[i] = (int32_t)rand_signed(32);
    for (int i = 0; i < frames * per_frame; i++)
        for (int k = 0; k < 3; k++)
            stim[i][k] = (p->layer == 1 && k) ? 0 : rand_signed(34);

    /* arrival: frame -> group -> pixel -> channel,  output: frame -> pixel -> channel */
    int nref = 0;
    for (int f = 0; f < frames; f++)
        for (int i = 0; i < per_pass; i++)
        {
            int64_t a = bias[i % co];
            for (int grp = 0; grp < g; grp++)
                for (int k = 0; k < 3; k++)
                    a += stim[f * per_frame + grp * per_pass + i][k];
            ref_sum[nref++] = ob_sext(a, OB_ACC_W);
        }

    output_buffer_in_t in = {0};
    output_buffer_out_t out;
    output_buffer_init(&ob, p, bias, (uint8_t)co);

    int fed = 0, got = 0, exp_en = 0;
    for (int cyc = 0; (fed < frames * per_frame || got < nref) && cyc < 40 * MAX_STIM; cyc++)
    {
        int fire = (ob.state != OB_IDLE) && fed < frames * per_frame && (rand() % 4 != 0);
        in.mac_valid = (uint8_t)fire;
        in.ch_result0 = fire ? stim[fed][0] : 111; /* garbage while mac_valid = 0 */
        in.ch_result1 = fire ? stim[fed][1] : 222;
        in.ch_result2 = fire ? stim[fed][2] : 333;

        output_buffer_comb(&ob, &in, &out);
        output_buffer_comb(&ob, &in, &out); /* comb must be repeatable */

        CHECK(out.ch3_5_en == exp_en, "cycle %d: ch3_5_en %d want %d", cyc, out.ch3_5_en, exp_en);

        if (out.sum_valid)
        {
            CHECK(got < nref && out.sum_data == ref_sum[got], "output %d: got %lld want %lld", got,
                  (long long)out.sum_data, (long long)(got < nref ? ref_sum[got] : 0));
            got++;
        }
        if (fire)
        {
            if (fed % per_pass == per_pass - 1)
                exp_en = (g > 1) ? !exp_en : 0; /* group flips after the last result of a pass */
            fed++;
        }
        output_buffer_seq(&ob);
    }

    CHECK(got == nref, "outputs %d want %d", got, nref);
    CHECK(!ob.out_ch_cnt && !ob.pixel_cnt && !ob.group_cnt && !ob.buf_addr, "counters not back at 0");
    CHECK(!ob.dbg_ch_ovf_cnt && !ob.dbg_acc_ovf_cnt, "unexpected width overflow");

    snprintf(name, sizeof name, "output buffer conv%d, seed %u: %d outputs vs reference", p->layer, seed, nref);
    end_test(name, before);
}

static void test_width_counters(void)
{
    int before = g_fail;
    int32_t bias[6] = {0};
    output_buffer_in_t in = {0};
    output_buffer_out_t out;

    output_buffer_init(&ob, &OB_PARAM_CONV1, bias, 6);
    output_buffer_comb(&ob, &in, &out);
    output_buffer_seq(&ob); /* leave IDLE */

    in.mac_valid = 1;
    in.ch_result0 = ((int64_t)1 << (OB_CH_W - 1)) - 1; /* largest legal value */
    output_buffer_comb(&ob, &in, &out);
    output_buffer_seq(&ob);
    CHECK(ob.dbg_ch_ovf_cnt == 0, "legal value flagged");

    in.ch_result0 = (int64_t)1 << (OB_CH_W - 1); /* one past it */
    output_buffer_comb(&ob, &in, &out);
    output_buffer_seq(&ob);
    CHECK(ob.dbg_ch_ovf_cnt == 1, "overflow not counted");

    end_test("output buffer: ch_result width overflow counter", before);
}

/* ------------------------------------------------------ relu & quantization */
static int64_t rq_x[MAX_OUT];

/* read order of the reorder buffer: group g (PACK channels) outer, pixel inner */
static int rq_src_index(int entry, int n, int c_out, int pack, int lane)
{
    int g = entry / n, pix = entry % n;
    return pix * c_out + g * pack + lane;
}

static void test_relu_quant(int n, int c_out, int pack, int shift, int ready_pct, unsigned seed)
{
    int before = g_fail;
    char name[128];
    rq_param_t p = {(uint16_t)n, (uint8_t)c_out, (uint8_t)pack, (uint8_t)shift};
    int nvals = n * c_out, entries = nvals / pack;

    srand(seed);
    for (int i = 0; i < nvals; i++)
        rq_x[i] = rand_signed(shift + 16);
    relu_quant_init(&rq, &p);

    relu_quant_in_t  in = {0};
    relu_quant_out_t out, prev = {0};
    int fed = 0, got = 0, stalled = 0;

    for (int cyc = 0; got < entries && cyc < 400000; cyc++)
    {
        int v = fed < nvals && (rand() % 3 != 0);
        in.sum_valid = (uint8_t)v;
        in.sum_data  = v ? rq_x[fed] : 0x123456;
        in.out_ready = (uint8_t)((rand() % 100) < ready_pct);

        relu_quant_comb(&rq, &in, &out);
        relu_quant_comb(&rq, &in, &out);

        if (stalled)                                    /* valid must hold with stable data until taken */
            CHECK(out.out_valid && out.out_data0 == prev.out_data0 && out.out_data1 == prev.out_data1 &&
                  out.out_data2 == prev.out_data2 && out.out_ch_done == prev.out_ch_done,
                  "cycle %d: output changed while out_ready = 0", cyc);

        if (out.out_valid && in.out_ready)
        {
            uint16_t lane[3] = {out.out_data0, out.out_data1, out.out_data2};
            for (int k = 0; k < 3; k++)
            {
                uint16_t want = k < pack ? ref_quant(rq_x[rq_src_index(got, n, c_out, pack, k)], shift) : 0;
                CHECK(lane[k] == want, "entry %d lane %d: got %u want %u", got, k, lane[k], want);
            }
            CHECK(out.out_ch_done == (got % n == n - 1), "entry %d: out_ch_done %d", got, out.out_ch_done);
            got++;
        }
        stalled = out.out_valid && !in.out_ready;
        prev = out;
        fed += v;
        relu_quant_seq(&rq);
    }

    CHECK(got == entries, "entries %d want %d", got, entries);
    CHECK(rq.u_out_reorder.dbg_overrun_cnt == 0, "reorder buffer overrun");

    snprintf(name, sizeof name, "relu_quant N=%d C_OUT=%d PACK=%d shift=%d out_ready=%d%%: %d entries group-major",
             n, c_out, pack, shift, ready_pct, entries);
    end_test(name, before);
}

static void test_reorder_overrun(void)
{
    int before = g_fail;
    rq_param_t p = {4, 2, 1, 0};                        /* 8 entries: 4 pixels x 2 channels */
    relu_quant_in_t  in = {0};
    relu_quant_out_t out;

    relu_quant_init(&rq, &p);
    for (int i = 0; i < 8 + 3; i++)                     /* never ready: frame fills, 3 extra pushes are dropped */
    {
        in.sum_valid = 1;
        in.sum_data  = 100 + i;
        relu_quant_comb(&rq, &in, &out);
        relu_quant_seq(&rq);
    }
    CHECK(rq.u_out_reorder.dbg_overrun_cnt == 3, "overrun %u want 3", rq.u_out_reorder.dbg_overrun_cnt);

    in.sum_valid = 0;
    in.out_ready = 1;
    static const int want[8] = {100, 102, 104, 106, 101, 103, 105, 107};   /* ch0 all pixels, then ch1 */
    int n = 0;
    for (int i = 0; i < 12; i++)
    {
        relu_quant_comb(&rq, &in, &out);
        if (out.out_valid)
        {
            CHECK(n < 8 && out.out_data0 == want[n], "entry %d: got %u", n, out.out_data0);
            CHECK(out.out_ch_done == (n % 4 == 3), "entry %d: out_ch_done %d", n, out.out_ch_done);
            n++;
        }
        relu_quant_seq(&rq);
    }
    CHECK(n == 8, "drained %d want 8", n);

    end_test("reorder buffer: group-major read-out, done at last pixel, overrun counted", before);
}

/* ------------------------------------- full chain against the Python golden model */
static int64_t vec_stim[MAX_STIM][3];
static int vec_exp[MAX_OUT];

static void test_python_vectors(const char *dir, const char *file, int ready_pct)
{
    int before = g_fail;
    char path[512], name[160];
    int layer, n, co, groups, pack, shift, nstim = 0, nexp = 0;
    int32_t bias[OB_MAX_C_OUT];

    snprintf(path, sizeof path, "%s/%s", dir, file);
    FILE *f = fopen(path, "r");
    if (!f)
    {
        printf("[skip] %s not found (run export_ob_vectors.py)\n", path);
        return;
    }
    int ok = fscanf(f, "%d %d %d %d %d %d", &layer, &n, &co, &groups, &pack, &shift) == 6;
    for (int i = 0; ok && i < co; i++)
        ok = fscanf(f, "%d", &bias[i]) == 1;
    ok = ok && fscanf(f, "%d", &nstim) == 1 && nstim <= MAX_STIM;
    for (int i = 0; ok && i < nstim; i++)
        ok = fscanf(f, "%lld %lld %lld", (long long *)&vec_stim[i][0], (long long *)&vec_stim[i][1],
                    (long long *)&vec_stim[i][2]) == 3;
    ok = ok && fscanf(f, "%d", &nexp) == 1 && nexp <= MAX_OUT;
    for (int i = 0; ok && i < nexp; i++)
        ok = fscanf(f, "%d", &vec_exp[i]) == 1;
    fclose(f);
    CHECK(ok, "%s: malformed vector file", path);
    if (!ok)
    {
        end_test(path, before);
        return;
    }

    ob_param_t op = {(uint8_t)layer, (uint16_t)n, (uint8_t)co, (uint8_t)groups};
    rq_param_t rp = {(uint16_t)n, (uint8_t)co, (uint8_t)pack, (uint8_t)shift};
    output_buffer_init(&ob, &op, bias, (uint8_t)co);
    relu_quant_init(&rq, &rp);

    output_buffer_in_t oin = {0};
    output_buffer_out_t oout;
    relu_quant_in_t rin = {0};
    relu_quant_out_t rout;
    int fed = 0, got = 0, exact = 0, off1 = 0, worse = 0, done_seen = 0;

    srand(7);
    for (int cyc = 0; got < nexp && cyc < 40 * MAX_STIM; cyc++)
    {
        int fire = (ob.state != OB_IDLE) && fed < nstim;
        oin.mac_valid = (uint8_t)fire;
        oin.ch_result0 = fire ? vec_stim[fed][0] : 0;
        oin.ch_result1 = fire ? vec_stim[fed][1] : 0;
        oin.ch_result2 = fire ? vec_stim[fed][2] : 0;
        output_buffer_comb(&ob, &oin, &oout);

        rin.sum_data = oout.sum_data;
        rin.sum_valid = oout.sum_valid;
        rin.out_ready = (uint8_t)((rand() % 100) < ready_pct);
        relu_quant_comb(&rq, &rin, &rout);

        if (rout.out_valid && rin.out_ready)
        {
            uint16_t lane[3] = {rout.out_data0, rout.out_data1, rout.out_data2};
            int entry = got / pack;
            for (int k = 0; k < pack; k++, got++)
            {
                int src = rq_src_index(entry, n, co, pack, k);      /* python vectors are pixel-major */
                int d = abs((int)lane[k] - vec_exp[src]);
                exact += (d == 0);
                off1  += (d == 1);
                worse += (d > 1);
                if (d > 1 && worse <= 5)
                    printf("    value %d: got %u python %d\n", src, lane[k], vec_exp[src]);
            }
            done_seen += rout.out_ch_done;
        }
        fed += fire;
        output_buffer_seq(&ob);
        relu_quant_seq(&rq);
    }

    /* Python runs the MAC in float32, so a value sitting on a rounding boundary may differ by one code */
    CHECK(got == nexp, "outputs %d want %d", got, nexp);
    CHECK(worse == 0, "%d values differ from Python by more than 1", worse);
    CHECK(done_seen == co / pack, "out_ch_done entries %d want %d", done_seen, co / pack);
    CHECK(!ob.dbg_ch_ovf_cnt && !ob.dbg_acc_ovf_cnt, "width overflow: ch %u acc %u", ob.dbg_ch_ovf_cnt, ob.dbg_acc_ovf_cnt);
    CHECK(rq.u_out_reorder.dbg_overrun_cnt == 0, "reorder buffer overrun");

    snprintf(name, sizeof name, "%s out_ready=%d%%: %d values vs Python (group-major): %d exact, %d off by 1, %d worse | sat %u",
             file, ready_pct, nexp, exact, off1, worse, rq.dbg_sat_cnt);
    end_test(name, before);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "vectors";

    test_quantizer();
    for (unsigned seed = 1; seed <= 3; seed++)
    {
        test_output_buffer(&OB_PARAM_CONV1, seed);
        test_output_buffer(&OB_PARAM_CONV2, seed);
    }
    test_width_counters();
    test_relu_quant(676, 6, 3, 14, 100, 11);
    test_relu_quant(676, 6, 3, 14, 30, 12);
    test_relu_quant(676, 6, 3, 15, 5, 13);
    test_relu_quant(121, 16, 1, 13, 100, 14);
    test_relu_quant(121, 16, 1, 13, 40, 15);
    test_relu_quant(50, 4, 2, 0, 60, 16);
    test_reorder_overrun();
    test_python_vectors(dir, "ob_conv1.txt", 100);
    test_python_vectors(dir, "ob_conv1.txt", 35);
    test_python_vectors(dir, "ob_conv2.txt", 100);
    test_python_vectors(dir, "ob_conv2.txt", 35);

    printf(g_fail ? "\nFAILED (%d checks)\n" : "\nALL PASS\n", g_fail);
    return g_fail != 0;
}
