/*
 * Fully Connected golden model test
 *   unit tests: signed quantizer, input staging ping-pong, fc_ctrl order, fc_mac latency
 *   frame tests: Python vectors (bit-exact), synthetic corner-case frame
 *
 *   ./test_fc [vector_dir]
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fc_top.h"

#define MAX_ROM_VALS (FC_MAX_ROM * FC_MAX_LANES)

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

typedef struct
{
    fc_param_t p;
    int32_t    bias[FC_MAX_N_OUT];
    uint16_t   x[FC_MAX_N_IN];
    int16_t    rom[MAX_ROM_VALS];
    int16_t    expected[FC_MAX_N_OUT];
} vec_t;

static vec_t v[3];

/* ------------------------------------------------------------ vector files */
static int read_vec(const char *dir, int layer, vec_t *out)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/fc%d.txt", dir, layer);

    FILE *f = fopen(path, "r");
    if (!f)
    {
        fprintf(stderr, "cannot open %s\n", path);
        return -1;
    }

    unsigned lay, n_in, n_out, lanes, chunk, scale_exp, relu;
    if (fscanf(f, "%u %u %u %u %u %u %u", &lay, &n_in, &n_out, &lanes, &chunk,
               &scale_exp, &relu) != 7)
    {
        fprintf(stderr, "%s: bad header\n", path);
        fclose(f);
        return -1;
    }

    out->p.layer     = (uint8_t)lay;
    out->p.n_in      = (uint16_t)n_in;
    out->p.n_out     = (uint8_t)n_out;
    out->p.lanes     = (uint8_t)lanes;
    out->p.num_chunk = (uint8_t)chunk;
    out->p.acc_w     = (layer == 1) ? 40 : 38;
    out->p.scale_exp = (uint8_t)scale_exp;
    out->p.relu      = (uint8_t)relu;

    for (unsigned i = 0; i < n_out; i++)
    {
        long b;
        if (fscanf(f, "%ld", &b) != 1) { fclose(f); return -1; }
        out->bias[i] = (int32_t)b;
    }
    for (unsigned i = 0; i < n_in; i++)
    {
        long x;
        if (fscanf(f, "%ld", &x) != 1) { fclose(f); return -1; }
        out->x[i] = (uint16_t)x;
    }
    for (unsigned r = 0; r < chunk * n_out; r++)
        for (unsigned i = 0; i < lanes; i++)
        {
            long w;
            if (fscanf(f, "%ld", &w) != 1) { fclose(f); return -1; }
            out->rom[r * lanes + i] = (int16_t)w;
        }
    for (unsigned i = 0; i < n_out; i++)
    {
        long e;
        if (fscanf(f, "%ld", &e) != 1) { fclose(f); return -1; }
        out->expected[i] = (int16_t)e;
    }

    fclose(f);
    return 0;
}

/* ------------------------------------------------- independent references */

/* neuron sums straight from the ROM rows, no pipeline: bias + sum(x * w) */
static void ref_layer(const fc_param_t *p, const int16_t *rom, const int32_t *bias,
                      const uint16_t *x, int64_t *sum)
{
    for (uint8_t n = 0; n < p->n_out; n++)
        sum[n] = bias[n];

    for (uint8_t g = 0; g < p->num_chunk; g++)
        for (uint8_t n = 0; n < p->n_out; n++)
            for (uint8_t i = 0; i < p->lanes; i++)
            {
                uint16_t xi = (g * p->lanes + i < p->n_in) ? x[g * p->lanes + i] : 0;
                sum[n] += (int64_t)xi * rom[((size_t)g * p->n_out + n) * p->lanes + i];
            }
}

/* rint() rounds half to even, same rule as the quantizers */
static int16_t ref_quant(int64_t x, int shift, int relu)
{
    if (relu && x < 0)
        x = 0;

    double q = rint(ldexp((double)x, -shift));
    if (q > 32767.0)
        return 32767;
    if (!relu && q < -32768.0)
        return -32768;
    if (relu && q < 0.0)
        return 0;
    return (int16_t)q;
}

/* ------------------------------------------------------- signed quantizer */
static int test_quant_signed(void)
{
    int before = g_fail;

    /* exact halves round to even: -3.5 -> -4, -2.5 -> -2, 2.5 -> 2, 3.5 -> 4 */
    CHECK(fc_quant_signed_comb_value(-7, 1, NULL) == -4, "-3.5 should round to -4");
    CHECK(fc_quant_signed_comb_value(-5, 1, NULL) == -2, "-2.5 should round to -2");
    CHECK(fc_quant_signed_comb_value(5, 1, NULL) == 2, "2.5 should round to 2");
    CHECK(fc_quant_signed_comb_value(7, 1, NULL) == 4, "3.5 should round to 4");

    /* negatives are kept (no ReLU) and both clamp ends saturate */
    uint8_t sat = 0;
    CHECK(fc_quant_signed_comb_value(-16, 2, &sat) == -4 && !sat, "-4 without saturation");
    CHECK(fc_quant_signed_comb_value((int64_t)40000 << 3, 3, &sat) == 32767 && sat,
          "clamp at +32767");
    CHECK(fc_quant_signed_comb_value(-((int64_t)40000 << 3), 3, &sat) == -32768 && sat,
          "clamp at -32768");

    /* shift 0 passes the value through */
    CHECK(fc_quant_signed_comb_value(-1234, 0, NULL) == -1234, "shift 0 keeps the value");

    return end_test("signed quantizer: round-half-even, clamp, no ReLU", before);
}

/* ------------------------------------------------------------- fc_staging */
static fc_staging_t stg;

static int test_staging(void)
{
    int before = g_fail;

    /* FC3 shape: the last chunk holds 4 of 5 lanes, the rest must read 0 */
    fc_param_t p = FC_PARAM_FC3;
    fc_staging_init(&stg, &p);

    fc_staging_in_t  in  = {0, 0, 0};
    fc_staging_out_t out;

    uint16_t fed = 0;
    int      swaps = 0;

    for (int cycle = 0; cycle < 400 && fed < p.n_in; cycle++)
    {
        in.in_data  = (uint16_t)(fed + 1); /* 1..84, so 0 means "not written" */
        in.in_valid = 1;
        in.chunk_done = 0;
        fc_staging_comb(&stg, &in, &out);

        if (out.in_ready)
            fed++;

        /* release the calc buffer as soon as it holds a chunk, like fc_ctrl does */
        if (out.calc_full)
        {
            uint8_t g   = (uint8_t)swaps;
            uint8_t len = fc_chunk_len(&p, g);

            for (uint8_t i = 0; i < p.lanes; i++)
            {
                uint16_t want = (i < len) ? (uint16_t)(g * p.lanes + i + 1) : 0;
                CHECK(out.x[i] == want, "chunk %u lane %u: got %u, expected %u",
                      g, i, out.x[i], want);
            }

            in.chunk_done = 1;
            fc_staging_comb(&stg, &in, &out);
            swaps++;
        }

        fc_staging_seq(&stg);
    }

    CHECK(fed == p.n_in, "fed %u of %u inputs", fed, p.n_in);
    CHECK(swaps >= p.num_chunk - 1, "saw %d chunk swaps, expected at least %u",
          swaps, p.num_chunk - 1);
    CHECK(stg.dbg_drop_cnt == 0, "staging drop counter is %u", stg.dbg_drop_cnt);

    return end_test("fc_staging: ping-pong, short last chunk zero padded", before);
}

/* ---------------------------------------------------------------- fc_ctrl */
static fc_ctrl_t ctrl;

static int test_ctrl(void)
{
    int before = g_fail;

    fc_param_t p = {2, 120, 84, 10, 12, 38, 14, 1};
    fc_ctrl_init(&ctrl, &p);

    fc_ctrl_in_t  in  = {0, 0};
    fc_ctrl_out_t out;

    /* no full calc buffer: stays in IDLE, mac_en never rises */
    for (int i = 0; i < 4; i++)
    {
        fc_ctrl_comb(&ctrl, &in, &out);
        CHECK(!out.mac_en, "mac_en must stay 0 while calc_full = 0");
        CHECK(ctrl.state == FC_IDLE, "state must stay IDLE while calc_full = 0");
        fc_ctrl_seq(&ctrl);
    }

    /* one chunk available, the next one is not: N_OUT enabled cycles, then wait in IDLE.
       calc_full drops on chunk_done because fc_staging releases that buffer. */
    in.calc_full = 1;
    in.next_full = 0;

    int enables = 0, chunk_dones = 0;
    for (int cycle = 0; cycle < 200; cycle++)
    {
        fc_ctrl_comb(&ctrl, &in, &out);

        if (out.mac_en)
        {
            CHECK(ctrl.neuron == enables % p.n_out, "neuron order broke at neuron %d", enables);
            enables++;
        }
        if (out.chunk_done)
        {
            chunk_dones++;
            in.calc_full = in.next_full; /* staging swaps; the new calc buffer may be empty */
        }
        fc_ctrl_seq(&ctrl);
    }

    CHECK(enables == p.n_out, "computed %d neurons, expected %u", enables, p.n_out);
    CHECK(chunk_dones == 1, "chunk_done pulsed %d times, expected 1", chunk_dones);
    CHECK(ctrl.state == FC_IDLE, "must wait in IDLE when next_full = 0");
    CHECK(ctrl.chunk == 1, "chunk counter is %u, expected 1", ctrl.chunk);

    /* next chunk already staged: keep issuing across the chunk boundary */
    in.calc_full = 1;
    in.next_full = 1;

    int enables2 = 0;
    for (int cycle = 0; cycle < p.n_out * 2; cycle++)
    {
        fc_ctrl_comb(&ctrl, &in, &out);
        enables2 += out.mac_en;
        fc_ctrl_seq(&ctrl);
    }
    CHECK(enables2 >= p.n_out * 2 - 1, "computed %d neurons in %u cycles with next_full = 1",
          enables2, p.n_out * 2);

    return end_test("fc_ctrl: IDLE/RUN, neuron order, chunk_done, gapless chunks", before);
}

/* ----------------------------------------------------------------- fc_mac */
static fc_mac_t mac;

static int test_mac(void)
{
    int before = g_fail;

    fc_param_t p = FC_PARAM_FC1;
    fc_mac_init(&mac, &p);

    fc_mac_in_t  in;
    fc_mac_out_t out;
    memset(&in, 0, sizeof(in));

    int64_t want = 0;
    for (uint8_t i = 0; i < p.lanes; i++)
    {
        in.x[i] = (uint16_t)(1000 + i);
        in.w[i] = (int16_t)((i & 1) ? -(300 + i) : (300 + i));
        want += (int64_t)in.x[i] * in.w[i];
    }
    in.mac_en = 1;

    /* the result appears five clocks after mac_en: operands, products, products again,
       then each half of the adder tree */
    fc_mac_comb(&mac, &in, &out);
    CHECK(!out.mac_valid, "mac_valid must be 0 in the mac_en cycle");
    fc_mac_seq(&mac);

    in.mac_en = 0;
    for (int c = 1; c <= 4; c++)
    {
        fc_mac_comb(&mac, &in, &out);
        CHECK(!out.mac_valid, "mac_valid must still be 0 %d clocks after mac_en", c);
        fc_mac_seq(&mac);
    }

    fc_mac_comb(&mac, &in, &out);
    CHECK(out.mac_valid, "mac_valid must be 1 five clocks after mac_en");
    CHECK(out.ch_result == want, "partial sum %lld, expected %lld",
          (long long)out.ch_result, (long long)want);
    fc_mac_seq(&mac);

    fc_mac_comb(&mac, &in, &out);
    CHECK(!out.mac_valid, "mac_valid must fall again with no new mac_en");

    CHECK(mac.dbg_ch_ovf_cnt == 0, "CH_W overflow counter is %u", mac.dbg_ch_ovf_cnt);

    return end_test("fc_mac: five-clock latency, partial sum, CH_W width", before);
}

/* ------------------------------------------------------------ frame tests */
static fc_top_t dut;

/* drives one frame through fc_top; returns the number of logits collected */
static int run_frame(const fc_param_t p[3], const uint16_t *x, int ready_pct,
                     int16_t *got1, int16_t *got2, int16_t *got3, int *n1, int *n2)
{
    int n3 = 0;
    *n1 = *n2 = 0;

    uint16_t fed = 0;
    for (long cycle = 0; cycle < 400000 && n3 < p[2].n_out; cycle++)
    {
        fc_top_in_t  in;
        fc_top_out_t out;

        in.fc_in_data  = (fed < p[0].n_in) ? x[fed] : 0;
        in.fc_in_valid = (fed < p[0].n_in);
        in.logit_ready = (rand() % 100) < ready_pct;
        fc_top_comb(&dut, &in, &out);

        if (in.fc_in_valid && out.fc_in_ready)
            fed++;
        if (dut.w_l1_fire && *n1 < FC_MAX_N_OUT)
            got1[(*n1)++] = dut.w_l1_data;
        if (dut.w_l2_fire && *n2 < FC_MAX_N_OUT)
            got2[(*n2)++] = dut.w_l2_data;
        if (out.logit_valid && in.logit_ready)
        {
            if (n3 < FC_MAX_N_OUT)
                got3[n3] = out.logit_data;
            n3++;
        }

        fc_top_seq(&dut);
    }
    return n3;
}

static int compare(const char *name, const int16_t *got, const int16_t *exp, int n, int count)
{
    int before = g_fail;

    CHECK(count == n, "%s: %d values, expected %d", name, count, n);
    for (int i = 0; i < n && i < count; i++)
        CHECK(got[i] == exp[i], "%s[%d]: got %d, expected %d", name, i, got[i], exp[i]);

    return g_fail - before;
}

static int test_python_vectors(int ready_pct)
{
    int before = g_fail;

    fc_param_t     p[3]    = {v[0].p, v[1].p, v[2].p};
    const int16_t *rom[3]  = {v[0].rom, v[1].rom, v[2].rom};
    const int32_t *bias[3] = {v[0].bias, v[1].bias, v[2].bias};

    fc_top_init(&dut, p, rom, bias);
    fc_top_reset(&dut);

    static int16_t got1[FC_MAX_N_OUT], got2[FC_MAX_N_OUT], got3[FC_MAX_N_OUT];
    int n1, n2;
    int n3 = run_frame(p, v[0].x, ready_pct, got1, got2, got3, &n1, &n2);

    compare("FC1", got1, v[0].expected, p[0].n_out, n1);
    compare("FC2", got2, v[1].expected, p[1].n_out, n2);
    compare("FC3", got3, v[2].expected, p[2].n_out, n3);

    for (int i = 0; i < 3; i++)
    {
        fc_layer_t *l = &dut.l[i];
        CHECK(l->u_staging.dbg_drop_cnt == 0, "FC%d staging drop %u", i + 1,
              l->u_staging.dbg_drop_cnt);
        CHECK(l->u_mac.dbg_ch_ovf_cnt == 0, "FC%d CH_W overflow %u", i + 1,
              l->u_mac.dbg_ch_ovf_cnt);
        CHECK(l->u_output_buffer.dbg_acc_ovf_cnt == 0, "FC%d ACC_W overflow %u", i + 1,
              l->u_output_buffer.dbg_acc_ovf_cnt);
        uint32_t over = p[i].relu ? l->u_relu_quant.u_out_reorder.dbg_overrun_cnt
                                  : l->u_quant_signed.u_out_reorder.dbg_overrun_cnt;
        CHECK(over == 0, "FC%d reorder overrun %u", i + 1, over);
    }

    char name[64];
    snprintf(name, sizeof(name), "Python vectors, out_ready %d%%", ready_pct);
    return end_test(name, before);
}

/* two frames back to back: counters wrap naturally, the reorder buffers must drain */
static int test_two_frames(void)
{
    int before = g_fail;

    fc_param_t     p[3]    = {v[0].p, v[1].p, v[2].p};
    const int16_t *rom[3]  = {v[0].rom, v[1].rom, v[2].rom};
    const int32_t *bias[3] = {v[0].bias, v[1].bias, v[2].bias};

    fc_top_init(&dut, p, rom, bias);
    fc_top_reset(&dut);

    static int16_t got1[FC_MAX_N_OUT], got2[FC_MAX_N_OUT], got3[FC_MAX_N_OUT];
    int n1, n2, n3;

    for (int frame = 0; frame < 2; frame++)
    {
        n3 = run_frame(p, v[0].x, 80, got1, got2, got3, &n1, &n2);

        char name[32];
        snprintf(name, sizeof(name), "frame %d FC1", frame + 1);
        compare(name, got1, v[0].expected, p[0].n_out, n1);
        snprintf(name, sizeof(name), "frame %d FC2", frame + 1);
        compare(name, got2, v[1].expected, p[1].n_out, n2);
        snprintf(name, sizeof(name), "frame %d FC3", frame + 1);
        compare(name, got3, v[2].expected, p[2].n_out, n3);
    }

    for (int i = 0; i < 3; i++)
    {
        uint32_t over = p[i].relu ? dut.l[i].u_relu_quant.u_out_reorder.dbg_overrun_cnt
                                  : dut.l[i].u_quant_signed.u_out_reorder.dbg_overrun_cnt;
        CHECK(over == 0, "FC%d reorder overrun %u across two frames", i + 1, over);
        CHECK(dut.l[i].u_staging.dbg_drop_cnt == 0, "FC%d staging drop %u", i + 1,
              dut.l[i].u_staging.dbg_drop_cnt);
    }

    return end_test("two frames back to back, no reset in between", before);
}

/* real data never reaches the clamp, so drive a saturating frame as well */
static int test_corner_frame(void)
{
    int before = g_fail;

    fc_param_t     p[3]    = {v[0].p, v[1].p, v[2].p};
    const int16_t *rom[3]  = {v[0].rom, v[1].rom, v[2].rom};
    const int32_t *bias[3] = {v[0].bias, v[1].bias, v[2].bias};

    static uint16_t x[FC_MAX_N_IN];
    for (uint16_t i = 0; i < p[0].n_in; i++)
        x[i] = 32767; /* largest activation code */

    fc_top_init(&dut, p, rom, bias);
    fc_top_reset(&dut);

    static int16_t got1[FC_MAX_N_OUT], got2[FC_MAX_N_OUT], got3[FC_MAX_N_OUT];
    int n1, n2;
    int n3 = run_frame(p, x, 70, got1, got2, got3, &n1, &n2);

    /* expected values from the plain integer reference, layer by layer */
    static int64_t  sum[FC_MAX_N_OUT];
    static int16_t  exp1[FC_MAX_N_OUT], exp2[FC_MAX_N_OUT], exp3[FC_MAX_N_OUT];
    static uint16_t act[FC_MAX_N_IN];

    ref_layer(&p[0], rom[0], bias[0], x, sum);
    for (uint8_t n = 0; n < p[0].n_out; n++)
    {
        exp1[n] = ref_quant(sum[n], p[0].scale_exp, 1);
        act[n]  = (uint16_t)exp1[n];
    }
    ref_layer(&p[1], rom[1], bias[1], act, sum);
    for (uint8_t n = 0; n < p[1].n_out; n++)
    {
        exp2[n] = ref_quant(sum[n], p[1].scale_exp, 1);
        act[n]  = (uint16_t)exp2[n];
    }
    ref_layer(&p[2], rom[2], bias[2], act, sum);
    for (uint8_t n = 0; n < p[2].n_out; n++)
        exp3[n] = ref_quant(sum[n], p[2].scale_exp, 0);

    compare("corner FC1", got1, exp1, p[0].n_out, n1);
    compare("corner FC2", got2, exp2, p[1].n_out, n2);
    compare("corner FC3", got3, exp3, p[2].n_out, n3);

    uint32_t sat = dut.l[0].u_relu_quant.dbg_sat_cnt + dut.l[1].u_relu_quant.dbg_sat_cnt
                   + dut.l[2].u_quant_signed.dbg_sat_cnt;
    CHECK(sat > 0, "the corner frame should hit the clamp at least once");

    for (int i = 0; i < 3; i++)
        CHECK(dut.l[i].u_mac.dbg_ch_ovf_cnt == 0 && dut.l[i].u_output_buffer.dbg_acc_ovf_cnt == 0,
              "FC%d width overflow on the corner frame", i + 1);

    printf("    corner frame: %u clamped values, widths still fit\n", sat);
    return end_test("corner frame: all inputs 32767", before);
}

int main(int argc, char **argv)
{
    const char *dir = (argc > 1) ? argv[1] : "vectors";

    for (int i = 0; i < 3; i++)
        if (read_vec(dir, i + 1, &v[i]) != 0)
            return 1;

    printf("Fully Connected golden model\n");
    for (int i = 0; i < 3; i++)
        printf("  FC%d: %u->%u, L=%u, chunks=%u, scale_exp=%u, relu=%u\n", i + 1,
               v[i].p.n_in, v[i].p.n_out, v[i].p.lanes, v[i].p.num_chunk,
               v[i].p.scale_exp, v[i].p.relu);

    srand(1);

    test_quant_signed();
    test_staging();
    test_ctrl();
    test_mac();
    test_python_vectors(100);
    test_python_vectors(40);
    test_two_frames();
    test_corner_frame();

    printf("%s (%d failures)\n", g_fail ? "FAIL" : "PASS", g_fail);
    return g_fail ? 1 : 0;
}
