/*
 * Runs the C golden model on vectors/ob_conv{1,2}.txt and writes $readmemh files:
 *
 *   <mem>/convN_bias.mem   32-bit bias per output channel - the ROM contents used by the RTL itself
 *
 * and, for the RTL testbench (tb/cnn/tb_output_buffer.v):
 *
 *   <out>/convN_stim.mem   {ch_result2, ch_result1, ch_result0}, 36 bits each, arrival order
 *   <out>/convN_sum.mem    golden Output Buffer stream: sum_data[39:0]
 *   <out>/convN_out.mem    golden output entries (group-major): {out_ch_done, out_data2, out_data1, out_data0}
 *   <out>/convN_params.txt  parameters and entry counts
 *
 * Frame 1 is the real image from the Python golden model. Frame 2 is synthetic and hits what real
 * data does not: negatives, exact .5 rounding ties, the 32767 clamp, and the widest legal inputs.
 *
 *   make -f output_buffer.mk rtl-vectors
 */
#include <stdio.h>
#include <stdlib.h>
#include "output_buffer.h"
#include "relu_quant.h"

#define MAX_STIM (2 * 2 * 676 * 16)   /* two frames */

static output_buffer_t ob;
static relu_quant_t    rq;
static int64_t         stim[MAX_STIM][3];

/* frame 2: choose the completed sum of each (pixel, channel) slot directly */
static int64_t stress_target(int i, int shift)
{
    const int64_t one = (int64_t)1 << shift, half = one >> 1;
    const int64_t fixed[] = {
        0, -1, -one, 1, half - 1, half, half + 1,                 /* around 0.5            */
        one + half, 2 * one + half, 3 * one + half,               /* 1.5, 2.5, 3.5 ties    */
        32766 * one + half, 32767 * one, 32767 * one + half - 1,  /* just below the clamp  */
        32767 * one + half, 32768 * one, (int64_t)1 << 38,        /* clamp                 */
        -((int64_t)1 << 38),
    };
    const int nfixed = (int)(sizeof fixed / sizeof fixed[0]);
    if (i < nfixed)
        return fixed[i];
    int64_t v = (((int64_t)rand() << 31) ^ ((int64_t)rand() << 15) ^ rand()) & (((int64_t)1 << (shift + 17)) - 1);
    return (rand() % 3 == 0) ? -v : v;                            /* about 1/3 negative, some above the clamp */
}

static void put_hex(FILE *f, uint64_t hi, uint64_t lo, int bits)   /* value = hi:lo, lo holds 64 bits */
{
    int digits = (bits + 3) / 4;
    for (int d = digits - 1; d >= 0; d--)
    {
        unsigned nib = d >= 16 ? (unsigned)((hi >> (4 * (d - 16))) & 0xF) : (unsigned)((lo >> (4 * d)) & 0xF);
        fputc("0123456789abcdef"[nib], f);
    }
    fputc('\n', f);
}

static FILE *open_out(const char *dir, int layer, const char *suffix)
{
    char path[512];
    snprintf(path, sizeof path, "%s/conv%d_%s", dir, layer, suffix);
    FILE *f = fopen(path, "w");
    if (!f)
    {
        perror(path);
        exit(1);
    }
    return f;
}

static int convert(const char *in_path, const char *out_dir, const char *mem_dir)
{
    int layer, n, co, groups, pack, shift, nstim = 0;
    int32_t bias[OB_MAX_C_OUT];

    FILE *f = fopen(in_path, "r");
    if (!f)
    {
        perror(in_path);
        return 1;
    }
    int ok = fscanf(f, "%d %d %d %d %d %d", &layer, &n, &co, &groups, &pack, &shift) == 6;
    for (int i = 0; ok && i < co; i++)
        ok = fscanf(f, "%d", &bias[i]) == 1;
    ok = ok && fscanf(f, "%d", &nstim) == 1 && nstim <= MAX_STIM;
    for (int i = 0; ok && i < nstim; i++)
        ok = fscanf(f, "%lld %lld %lld", (long long *)&stim[i][0], (long long *)&stim[i][1],
                    (long long *)&stim[i][2]) == 3;
    fclose(f);
    if (!ok || 2 * nstim > MAX_STIM)
    {
        fprintf(stderr, "%s: malformed vector file\n", in_path);
        return 1;
    }

    /* frame 2: the first group carries (target - bias) on lane 0, everything else is 0 */
    srand(20260920 + layer);
    for (int i = 0; i < nstim; i++)
        stim[nstim + i][0] = stim[nstim + i][1] = stim[nstim + i][2] = 0;
    for (int i = 0; i < n * co; i++)
        stim[nstim + i][0] = stress_target(i, shift) - bias[i % co];
    nstim *= 2;

    FILE *fb = open_out(mem_dir, layer, "bias.mem");
    FILE *fs = open_out(out_dir, layer, "stim.mem");
    FILE *fg = open_out(out_dir, layer, "sum.mem");
    FILE *fo = open_out(out_dir, layer, "out.mem");

    for (int i = 0; i < co; i++)
        put_hex(fb, 0, (uint32_t)bias[i], 32);

    const uint64_t m36 = ((uint64_t)1 << OB_CH_W) - 1;
    for (int i = 0; i < nstim; i++)
    {
        uint64_t c0 = (uint64_t)stim[i][0] & m36, c1 = (uint64_t)stim[i][1] & m36, c2 = (uint64_t)stim[i][2] & m36;
        uint64_t lo = c0 | (c1 << 36);                  /* bits 0..63   */
        uint64_t hi = (c1 >> 28) | (c2 << 8);           /* bits 64..107 */
        put_hex(fs, hi, lo, 3 * OB_CH_W);
    }

    ob_param_t op = {(uint8_t)layer, (uint16_t)n, (uint8_t)co, (uint8_t)groups, OB_ACC_W};
    rq_param_t rp = {(uint16_t)n, (uint8_t)co, (uint8_t)pack, (uint8_t)shift};
    output_buffer_init(&ob, &op, bias, (uint8_t)co);
    relu_quant_init(&rq, &rp);

    output_buffer_in_t  oin = {0};
    output_buffer_out_t oout;
    relu_quant_in_t     rin = {0};
    relu_quant_out_t    rout;
    int fed = 0, nsum = 0, nout = 0, idle = 0;
    int per_frame_stim = groups * n * co, per_frame_out = n * co / pack;

    while (idle < 8)                                    /* run until the pipeline drains */
    {
        /* a new frame starts only after the previous one has been fully read out */
        int frame_gap = (fed % per_frame_stim == 0) && (nout < (fed / per_frame_stim) * per_frame_out);
        int fire = (ob.state != OB_IDLE) && fed < nstim && !frame_gap;
        oin.mac_valid  = (uint8_t)fire;
        oin.ch_result0 = fire ? stim[fed][0] : 0;
        oin.ch_result1 = fire ? stim[fed][1] : 0;
        oin.ch_result2 = fire ? stim[fed][2] : 0;
        output_buffer_comb(&ob, &oin, &oout);

        rin.sum_data  = oout.sum_data;
        rin.sum_valid = oout.sum_valid;
        rin.out_ready = 1;
        relu_quant_comb(&rq, &rin, &rout);

        if (oout.sum_valid)
        {
            put_hex(fg, 0, (uint64_t)oout.sum_data & (((uint64_t)1 << OB_ACC_W) - 1), OB_ACC_W);
            nsum++;
        }
        if (rout.out_valid)
        {
            uint64_t v = rout.out_data0;
            if (pack > 1) v |= (uint64_t)rout.out_data1 << 16;
            if (pack > 2) v |= (uint64_t)rout.out_data2 << 32;
            v |= (uint64_t)rout.out_ch_done << (16 * pack);
            put_hex(fo, 0, v, 16 * pack + 1);
            nout++;
        }
        idle = (fed >= nstim && !oout.sum_valid && !rout.out_valid) ? idle + 1 : 0;
        fed += fire;
        output_buffer_seq(&ob);
        relu_quant_seq(&rq);
    }
    fclose(fb); fclose(fs); fclose(fg); fclose(fo);

    FILE *fp = open_out(out_dir, layer, "params.txt");
    fprintf(fp, "N=%d C_OUT=%d NUM_GROUPS=%d PACK=%d SCALE_EXP=%d NSTIM=%d NSUM=%d NOUT=%d\n",
            n, co, groups, pack, shift, nstim, nsum, nout);
    fclose(fp);

    printf("conv%d: N=%d C_OUT=%d NUM_GROUPS=%d PACK=%d SCALE_EXP=%d | stim %d, sum %d, out %d\n",
           layer, n, co, groups, pack, shift, nstim, nsum, nout);
    return 0;
}

int main(int argc, char **argv)
{
    const char *in_dir  = argc > 1 ? argv[1] : "vectors";
    const char *out_dir = argc > 2 ? argv[2] : "../../tb/cnn/vectors";
    const char *mem_dir = argc > 3 ? argv[3] : "../../rtl/cnn/mem";
    char path[512];
    int bad = 0;

    snprintf(path, sizeof path, "%s/ob_conv1.txt", in_dir);
    bad |= convert(path, out_dir, mem_dir);
    snprintf(path, sizeof path, "%s/ob_conv2.txt", in_dir);
    bad |= convert(path, out_dir, mem_dir);
    return bad;
}
