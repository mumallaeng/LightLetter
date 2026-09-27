/*
 * Fully Connected golden model test (step 2, shared engine)
 *   unit tests: quantizer, act_buf, fc_ctrl order and gating, fc_mac_acc latency, drain backpressure
 *   frame tests: Python vectors (bit-exact), two frames back to back, synthetic corner-case frame
 *
 *   ./test_fc [vector_dir]
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fc_top.h"
#include "fc_vec_io.h"

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

static fc_vec_t       v[FC_LAYERS];
static fc_rom_image_t img;
static fc_top_t       dut;

/* rint() rounds half to even, same rule as the quantizers */
static int16_t ref_quant(fc_acc_t x, int shift, int relu)
{
    if (relu && x < 0) x = 0;
    double q = rint(ldexp((double)x, -shift));
    if (q > 32767.0) return 32767;
    if (!relu && q < -32768.0) return -32768;
    if (relu && q < 0.0) return 0;
    return (int16_t)q;
}

/* -------------------------------------------------------------- quantizer */
static int test_quant(void)
{
    int before = g_fail;
    uint8_t sat = 0;
    CHECK(fc_quant_value(-7, 1, 0, NULL) == -4, "-3.5 should round to -4");
    CHECK(fc_quant_value(-5, 1, 0, NULL) == -2, "-2.5 should round to -2");
    CHECK(fc_quant_value(5, 1, 0, NULL) == 2, "2.5 should round to 2");
    CHECK(fc_quant_value(7, 1, 0, NULL) == 4, "3.5 should round to 4");
    CHECK(fc_quant_value(-16, 2, 1, &sat) == 0 && !sat, "ReLU zeroes a negative sum");
    CHECK(fc_quant_value((fc_acc_t)40000 << 3, 3, 1, &sat) == 32767 && sat, "unsigned clamp at 32767");
    sat = 0;
    CHECK(fc_quant_value(-((fc_acc_t)40000 << 3), 3, 0, &sat) == -32768 && sat, "signed clamp at -32768");
    CHECK(fc_quant_value(-1234, 0, 0, NULL) == -1234, "shift 0 keeps the value");
    for (int i = 0; i < 2000; i++)
    {
        fc_acc_t x = ((fc_acc_t)rand() << 20) ^ rand();
        if (rand() & 1) x = -x;
        int s = rand() % 20, relu = rand() & 1;
        CHECK(fc_quant_value(x, (uint8_t)s, (uint8_t)relu, NULL) == ref_quant(x, s, relu),
              "quant(%lld, %d, %d) differs from rint()", (long long)x, s, relu);
    }
    return end_test("quantizer: round-half-even, ReLU / signed clamp", before);
}

/* ---------------------------------------------------------------- act_buf */
static int test_act_buf(void)
{
    int before = g_fail;
    fc_act_buf_t buf;
    fc_act_buf_init(&buf, FC_ACT_B);
    fc_act_buf_in_t in; fc_act_buf_out_t out;
    for (uint16_t i = 0; i < FC_ACT_B; i++)
    {
        in.we = 1; in.waddr = i; in.wdata = (uint16_t)(i * 7 + 1); in.raddr = i;
        fc_act_buf_comb(&buf, &in, &out);
        CHECK(out.rdata == 0, "a write must not be visible in its own cycle (addr %u)", i);
        fc_act_buf_seq(&buf);
    }
    for (uint16_t i = 0; i < FC_ACT_B; i++)
    {
        in.we = 0; in.raddr = i;
        fc_act_buf_comb(&buf, &in, &out);
        CHECK(out.rdata == (uint16_t)(i * 7 + 1), "addr %u reads %u", i, out.rdata);
        fc_act_buf_seq(&buf);
    }
    return end_test("act_buf: write one per clock, asynchronous read", before);
}

/* ---------------------------------------------------------------- fc_ctrl */
static int test_ctrl(void)
{
    int before = g_fail;
    fc_ctrl_t c;
    fc_ctrl_init(&c);
    fc_ctrl_in_t  in = {0, 0, 1};
    fc_ctrl_out_t out;

    /* nothing arrived: IDLE, no issue */
    for (int i = 0; i < 3; i++)
    {
        fc_ctrl_comb(&c, &in, &out);
        CHECK(!out.mac_en && c.state == FC_IDLE, "must idle with an empty act_in");
        fc_ctrl_seq(&c);
    }

    /* feed 400 inputs one per clock while the engine runs: FC1 group 0 follows the arrivals */
    int issues[4] = {0}, firsts[4] = {0}, stalls_g0 = 0;
    uint16_t fed = 0;
    long cycles = 0;
    uint8_t seen_layer3_flush = 0;
    for (; cycles < 20000 && !seen_layer3_flush; cycles++)
    {
        in.fc_in_valid = (fed < FC_ACT_IN);
        fc_ctrl_comb(&c, &in, &out);
        if (out.in_we) fed++;
        if (out.mac_en)
        {
            issues[out.layer]++;
            /* a valid row is addressed one cycle earlier: check the prefetch matches this issue */
            uint16_t row = (uint16_t)(FC_CFG[out.layer - 1].rom_base + out.group * FC_CFG[out.layer - 1].n_in + out.x_raddr);
            (void)row;
        }
        if (out.mac_en && out.first) firsts[out.layer]++;
        if (c.state == FC_RUN && c.layer == 1 && c.group == 0 && !out.mac_en) stalls_g0++;
        if (c.state == FC_FLUSH && c.layer == 3) seen_layer3_flush = 1;
        fc_ctrl_seq(&c);
    }
    CHECK(issues[1] == 6 * 400 && issues[2] == 5 * 120 && issues[3] == 2 * 84,
          "issues per layer %d / %d / %d", issues[1], issues[2], issues[3]);
    CHECK(firsts[1] == 6 && firsts[2] == 5 && firsts[3] == 2, "first-input flags per layer %d / %d / %d", firsts[1], firsts[2], firsts[3]);
    CHECK(fed == FC_ACT_IN, "fed %u inputs", fed);
    CHECK(stalls_g0 <= 2, "FC1 group 0 stalled %d cycles although inputs arrive every clock", stalls_g0);
    CHECK(c.fill_cnt == 0, "fill_cnt must clear after FC1 (is %u)", c.fill_cnt);
    printf("    ctrl: %ld cycles from first input to the FC3 flush (hold always free)\n", cycles);

    /* the last input of a group waits for hold_free */
    fc_ctrl_reset(&c);
    c.state = FC_RUN; c.layer = 2; c.group = 1; c.i = 119; c.fill_cnt = 0;
    in.fc_in_valid = 0; in.hold_free = 0;
    fc_ctrl_comb(&c, &in, &out);
    CHECK(!out.mac_en && c.state_next == FC_RUN, "last input must wait while the drain is busy");
    in.hold_free = 1;
    fc_ctrl_comb(&c, &in, &out);
    CHECK(out.mac_en && out.last && c.state_next == FC_RUN && c.group_next == 2, "last input issues once the drain is free");

    /* FLUSH waits for the pipeline and the drain */
    fc_ctrl_reset(&c);
    c.state = FC_FLUSH; c.layer = 3;
    in.mac_busy = 1; in.hold_free = 1;
    fc_ctrl_comb(&c, &in, &out);
    CHECK(c.state_next == FC_FLUSH, "FLUSH must wait for the MAC pipeline");
    in.mac_busy = 0; in.hold_free = 0;
    fc_ctrl_comb(&c, &in, &out);
    CHECK(c.state_next == FC_FLUSH, "FLUSH must wait for the drain");
    in.hold_free = 1;
    fc_ctrl_comb(&c, &in, &out);
    CHECK(c.state_next == FC_IDLE, "after FC3 the engine returns to IDLE");

    return end_test("fc_ctrl: group/input order, first-input flags, arrival and drain gating, flush", before);
}

/* ------------------------------------------------------------- fc_mac_acc */
static int test_mac_acc(void)
{
    int before = g_fail;
    fc_mac_acc_t mac;
    fc_mac_acc_init(&mac);
    fc_mac_acc_in_t in; fc_mac_acc_out_t out;
    memset(&in, 0, sizeof in);

    fc_acc_t want[FC_P];
    for (int l = 0; l < FC_P; l++) { in.b[l] = 1000 * (l + 1) * ((l & 1) ? -1 : 1); want[l] = in.b[l]; }
    in.layer = 2; in.group = 3;
    /* stale accumulators must not leak into the group: the first input starts from the bias */
    for (int l = 0; l < FC_P; l++) mac.acc[l] = 123456789;

    const int N = 5;
    for (int i = 0; i < N; i++)                                        /* cycles 0..4: inputs */
    {
        in.first = (i == 0); in.mac_en = 1; in.last = (i == N - 1);
        in.x = (uint16_t)(3000 + 700 * i);
        for (int l = 0; l < FC_P; l++) { in.w[l] = (int16_t)(-2000 + 137 * l + 11 * i); want[l] += (fc_acc_t)in.x * in.w[l]; }
        fc_mac_acc_comb(&mac, &in, &out);
        CHECK(!out.sum_valid, "sum_valid must be 0 while inputs are still issued (i=%d)", i);
        fc_mac_acc_seq(&mac);
    }
    in.mac_en = 0; in.last = 0;
    fc_mac_acc_comb(&mac, &in, &out);                                  /* cycle 6: last input in stage 2? */
    CHECK(!out.sum_valid && out.busy, "one cycle after the last issue the product is still in flight");
    fc_mac_acc_seq(&mac);
    fc_mac_acc_comb(&mac, &in, &out);                                  /* cycle 7: accumulate stage sees it */
    CHECK(out.sum_valid, "sum_valid two cycles after the last issue");
    CHECK(out.layer == 2 && out.group == 3, "layer/group tags travel with the data");
    for (int l = 0; l < FC_P; l++)
        CHECK(out.sum[l] == want[l], "lane %d sum %lld, expected %lld", l, (long long)out.sum[l], (long long)want[l]);
    fc_mac_acc_seq(&mac);
    fc_mac_acc_comb(&mac, &in, &out);
    CHECK(!out.sum_valid && !out.busy, "pipeline drains");
    CHECK(mac.dbg_acc_ovf_cnt == 0, "ACC_W overflow counter is %u", mac.dbg_acc_ovf_cnt);
    return end_test("fc_mac_acc: bias on the first input, accumulate, two-cycle latency, tags", before);
}

/* --------------------------------------------------------------- fc_drain */
static int test_drain(void)
{
    int before = g_fail;
    fc_drain_t d;
    fc_drain_init(&d);
    fc_drain_in_t in; fc_drain_out_t out;
    memset(&in, 0, sizeof in);

    /* FC3 group 1: 6 logits, logit_ready toggling */
    in.sum_valid = 1; in.layer = 3; in.group = 1;
    for (int l = 0; l < FC_P; l++) in.sum[l] = (fc_acc_t)(l - 3) << FC_CFG[2].scale_exp;
    fc_drain_comb(&d, &in, &out);
    CHECK(out.hold_free, "free before the sums land");
    fc_drain_seq(&d);
    in.sum_valid = 0;
    int got = 0, cycles = 0;
    while (cycles++ < 100 && !(got == 6 && !d.valid))
    {
        in.logit_ready = (cycles % 3) != 0;
        fc_drain_comb(&d, &in, &out);
        CHECK(!out.hold_free || got == 6, "not free while draining");
        if (out.logit_valid && in.logit_ready)
        {
            CHECK(out.logit_data == got - 3, "logit %d is %d", got, out.logit_data);
            got++;
        }
        fc_drain_seq(&d);
    }
    CHECK(got == 6, "drained %d logits, expected 6", got);
    CHECK(!d.valid, "register empties after the short last group");

    /* FC1 group 2: 20 writes to act_a at neurons 40..59, one per clock, ReLU applied */
    in.sum_valid = 1; in.layer = 1; in.group = 2; in.logit_ready = 0;
    for (int l = 0; l < FC_P; l++) in.sum[l] = (fc_acc_t)((l & 1) ? -5 : 5) << FC_CFG[0].scale_exp;
    fc_drain_comb(&d, &in, &out); fc_drain_seq(&d);
    in.sum_valid = 0;
    for (int k = 0; k < FC_P; k++)
    {
        fc_drain_comb(&d, &in, &out);
        CHECK(out.act_we && out.act_layer == 1 && out.act_waddr == 40 + k, "write %d goes to act_a[%d]", k, 40 + k);
        CHECK(out.act_wdata == ((k & 1) ? 0 : 5), "write %d value %u", k, out.act_wdata);
        fc_drain_seq(&d);
    }
    fc_drain_comb(&d, &in, &out);
    CHECK(!out.act_we && out.hold_free, "done after 20 writes");
    CHECK(d.dbg_overrun_cnt == 0, "overrun counter %u", d.dbg_overrun_cnt);
    return end_test("fc_drain: logit backpressure, act writes in neuron order, ReLU", before);
}

/* ------------------------------------------------------------ frame tests */
static long g_last_cycles;

/* drives one frame through fc_top; returns the number of logits collected */
static int run_frame(const uint16_t *x, int ready_pct, int16_t *got1, int16_t *got2, int16_t *got3, int *n1, int *n2)
{
    int n3 = 0;
    *n1 = *n2 = 0;
    uint16_t fed = 0;
    long cycle = 0;
    for (; cycle < 400000 && n3 < FC_CFG[2].n_out; cycle++)
    {
        fc_top_in_t  in;
        fc_top_out_t out;
        in.fc_in_data  = (fed < FC_CFG[0].n_in) ? x[fed] : 0;
        in.fc_in_valid = (fed < FC_CFG[0].n_in);
        in.logit_ready = (rand() % 100) < ready_pct;
        fc_top_comb(&dut, &in, &out);
        if (in.fc_in_valid && out.fc_in_ready) fed++;
        if (dut.w_l1_fire && *n1 < FC_MAX_N_OUT) got1[(*n1)++] = dut.w_l1_data;
        if (dut.w_l2_fire && *n2 < FC_MAX_N_OUT) got2[(*n2)++] = dut.w_l2_data;
        if (out.logit_valid && in.logit_ready)
        {
            if (n3 < FC_MAX_N_OUT) got3[n3] = out.logit_data;
            n3++;
        }
        fc_top_seq(&dut);
    }
    g_last_cycles = cycle;
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

static void check_counters(const char *tag)
{
    CHECK(dut.u_mac.dbg_acc_ovf_cnt == 0, "%s: ACC_W overflow %u", tag, dut.u_mac.dbg_acc_ovf_cnt);
    CHECK(dut.u_drain.dbg_overrun_cnt == 0, "%s: drain overrun %u", tag, dut.u_drain.dbg_overrun_cnt);
}

static int test_python_vectors(int ready_pct)
{
    int before = g_fail;
    fc_top_init(&dut, &img);
    static int16_t got1[FC_MAX_N_OUT], got2[FC_MAX_N_OUT], got3[FC_MAX_N_OUT];
    int n1, n2;
    int n3 = run_frame(v[0].x, ready_pct, got1, got2, got3, &n1, &n2);
    compare("FC1", got1, v[0].expected, v[0].n_out, n1);
    compare("FC2", got2, v[1].expected, v[1].n_out, n2);
    compare("FC3", got3, v[2].expected, v[2].n_out, n3);
    check_counters("python");
    char name[64];
    snprintf(name, sizeof name, "Python vectors, out_ready %d%% (%ld cycles first input -> last logit)", ready_pct, g_last_cycles);
    return end_test(name, before);
}

static int test_two_frames(void)
{
    int before = g_fail;
    fc_top_init(&dut, &img);
    static int16_t got1[FC_MAX_N_OUT], got2[FC_MAX_N_OUT], got3[FC_MAX_N_OUT];
    for (int frame = 0; frame < 2; frame++)
    {
        int n1, n2;
        int n3 = run_frame(v[0].x, 80, got1, got2, got3, &n1, &n2);
        char name[32];
        snprintf(name, sizeof name, "frame %d FC1", frame + 1); compare(name, got1, v[0].expected, v[0].n_out, n1);
        snprintf(name, sizeof name, "frame %d FC2", frame + 1); compare(name, got2, v[1].expected, v[1].n_out, n2);
        snprintf(name, sizeof name, "frame %d FC3", frame + 1); compare(name, got3, v[2].expected, v[2].n_out, n3);
    }
    check_counters("two frames");
    return end_test("two frames back to back, no reset in between", before);
}

static int test_corner_frame(void)
{
    int before = g_fail;
    static uint16_t x[FC_MAX_N_IN];
    for (uint16_t i = 0; i < FC_CFG[0].n_in; i++) x[i] = 32767;
    fc_top_init(&dut, &img);
    static int16_t got1[FC_MAX_N_OUT], got2[FC_MAX_N_OUT], got3[FC_MAX_N_OUT];
    int n1, n2;
    int n3 = run_frame(x, 70, got1, got2, got3, &n1, &n2);

    static fc_acc_t sum[FC_MAX_N_OUT];
    static int16_t  exp1[FC_MAX_N_OUT], exp2[FC_MAX_N_OUT], exp3[FC_MAX_N_OUT];
    static uint16_t act[FC_MAX_N_IN];
    fc_ref_layer(&v[0], x, sum);
    for (uint8_t n = 0; n < v[0].n_out; n++) { exp1[n] = ref_quant(sum[n], v[0].scale_exp, 1); act[n] = (uint16_t)exp1[n]; }
    fc_ref_layer(&v[1], act, sum);
    for (uint8_t n = 0; n < v[1].n_out; n++) { exp2[n] = ref_quant(sum[n], v[1].scale_exp, 1); act[n] = (uint16_t)exp2[n]; }
    fc_ref_layer(&v[2], act, sum);
    for (uint8_t n = 0; n < v[2].n_out; n++) exp3[n] = ref_quant(sum[n], v[2].scale_exp, 0);
    compare("corner FC1", got1, exp1, v[0].n_out, n1);
    compare("corner FC2", got2, exp2, v[1].n_out, n2);
    compare("corner FC3", got3, exp3, v[2].n_out, n3);
    CHECK(dut.u_drain.dbg_sat_cnt > 0, "the corner frame should hit the clamp at least once");
    check_counters("corner");
    printf("    corner frame: %u clamped values, widths still fit\n", dut.u_drain.dbg_sat_cnt);
    return end_test("corner frame: all inputs 32767", before);
}

int main(int argc, char **argv)
{
    const char *dir = (argc > 1) ? argv[1] : "vectors";
    for (int i = 0; i < FC_LAYERS; i++)
        if (fc_read_vec(dir, i + 1, &v[i]) != 0) return 1;
    if (fc_rom_build(&img, v) != 0) return 1;

    printf("Fully Connected golden model (shared engine, P=%d)\n", FC_P);
    for (int i = 0; i < FC_LAYERS; i++)
        printf("  FC%d: %u->%u, groups=%u, rom rows %u.., scale_exp=%u, relu=%u\n", i + 1, FC_CFG[i].n_in,
               FC_CFG[i].n_out, FC_CFG[i].groups, FC_CFG[i].rom_base, v[i].scale_exp, v[i].relu);
    for (int i = 0; i < FC_LAYERS; i++)
        if (v[i].scale_exp != FC_CFG[i].scale_exp || v[i].relu != FC_CFG[i].relu)
        {
            printf("FC%d: vectors say scale_exp %u relu %u, FC_CFG says %u %u -> regenerate or fix fc_common.h\n",
                   i + 1, v[i].scale_exp, v[i].relu, FC_CFG[i].scale_exp, FC_CFG[i].relu);
            return 1;
        }

    srand(1);
    test_quant();
    test_act_buf();
    test_ctrl();
    test_mac_acc();
    test_drain();
    test_python_vectors(100);
    test_python_vectors(40);
    test_two_frames();
    test_corner_frame();
    printf("%s (%d failures)\n", g_fail ? "FAIL" : "PASS", g_fail);
    return g_fail ? 1 : 0;
}
