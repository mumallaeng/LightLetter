/*
 * Runs the C golden model on vectors/fc{1,2,3}.txt and writes $readmemh files
 * for the RTL testbench (tb/cnn/tb_fc.v):
 *
 *   <out>/fc_stim.mem    input codes fed to fc_top, arrival order (16 bits)
 *   <out>/fc1_out.mem    golden FC1 output stream, neuron order (16 bits)
 *   <out>/fc2_out.mem    golden FC2 output stream (16 bits)
 *   <out>/fc3_out.mem    golden logit stream, class order (16 bits, signed)
 *   <out>/fc_params.txt  parameters and entry counts
 *
 * The weight and bias ROM files themselves come from export_fc_vectors.py
 * (rtl/cnn/mem/fc*_weight.mem, fc*_bias.mem).
 *
 * Frame 1 is the real image from the Python golden model. Frame 2 is synthetic:
 * every input at the largest activation code, which drives the accumulators past
 * the quantizer clamp that real data never reaches.
 *
 *   make -f fc.mk rtl-vectors
 */
#include <stdio.h>
#include <stdlib.h>
#include "fc_top.h"

#define FRAMES 2

static fc_top_t dut;

typedef struct
{
    fc_param_t p;
    int32_t    bias[FC_MAX_N_OUT];
    uint16_t   x[FC_MAX_N_IN];
    int16_t    rom[FC_MAX_ROM * FC_MAX_LANES];
} vec_t;

static vec_t v[3];

static int read_vec(const char *dir, int layer, vec_t *out)
{
    char path[512];
    snprintf(path, sizeof path, "%s/fc%d.txt", dir, layer);

    FILE *f = fopen(path, "r");
    if (!f)
    {
        perror(path);
        return 1;
    }

    unsigned lay, n_in, n_out, lanes, chunk, shift, relu;
    int ok = fscanf(f, "%u %u %u %u %u %u %u", &lay, &n_in, &n_out, &lanes, &chunk,
                    &shift, &relu) == 7;

    out->p.layer     = (uint8_t)lay;
    out->p.n_in      = (uint16_t)n_in;
    out->p.n_out     = (uint8_t)n_out;
    out->p.lanes     = (uint8_t)lanes;
    out->p.num_chunk = (uint8_t)chunk;
    out->p.acc_w     = (layer == 1) ? 40 : 38;
    out->p.scale_exp = (uint8_t)shift;
    out->p.relu      = (uint8_t)relu;

    for (unsigned i = 0; ok && i < n_out; i++)
    {
        long b;
        ok = fscanf(f, "%ld", &b) == 1;
        out->bias[i] = (int32_t)b;
    }
    for (unsigned i = 0; ok && i < n_in; i++)
    {
        long x;
        ok = fscanf(f, "%ld", &x) == 1;
        out->x[i] = (uint16_t)x;
    }
    for (unsigned r = 0; ok && r < chunk * n_out; r++)
        for (unsigned i = 0; ok && i < lanes; i++)
        {
            long w;
            ok = fscanf(f, "%ld", &w) == 1;
            out->rom[r * lanes + i] = (int16_t)w;
        }

    fclose(f);
    if (!ok)
    {
        fprintf(stderr, "%s: malformed vector file\n", path);
        return 1;
    }
    return 0;
}

static void put_hex16(FILE *f, uint16_t v)
{
    fprintf(f, "%04x\n", v);
}

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

/* one frame through fc_top; writes the three golden streams as they leave each layer */
static void run_frame(const fc_param_t p[3], const uint16_t *x, int ready_pct,
                      FILE *f1, FILE *f2, FILE *f3, int *n1, int *n2, int *n3)
{
    uint16_t fed = 0;
    *n1 = *n2 = *n3 = 0;

    for (long cycle = 0; cycle < 400000 && *n3 < p[2].n_out; cycle++)
    {
        fc_top_in_t  in;
        fc_top_out_t out;

        in.fc_in_data  = (fed < p[0].n_in) ? x[fed] : 0;
        in.fc_in_valid = (fed < p[0].n_in);
        in.logit_ready = (rand() % 100) < ready_pct;
        fc_top_comb(&dut, &in, &out);

        if (in.fc_in_valid && out.fc_in_ready)
            fed++;
        if (dut.w_l1_fire)
        {
            put_hex16(f1, (uint16_t)dut.w_l1_data);
            (*n1)++;
        }
        if (dut.w_l2_fire)
        {
            put_hex16(f2, (uint16_t)dut.w_l2_data);
            (*n2)++;
        }
        if (out.logit_valid && in.logit_ready)
        {
            put_hex16(f3, (uint16_t)out.logit_data);
            (*n3)++;
        }

        fc_top_seq(&dut);
    }
}

int main(int argc, char **argv)
{
    const char *in_dir  = argc > 1 ? argv[1] : "vectors";
    const char *out_dir = argc > 2 ? argv[2] : "../../tb/cnn/vectors";

    for (int i = 0; i < 3; i++)
        if (read_vec(in_dir, i + 1, &v[i]) != 0)
            return 1;

    fc_param_t     p[3]    = {v[0].p, v[1].p, v[2].p};
    const int16_t *rom[3]  = {v[0].rom, v[1].rom, v[2].rom};
    const int32_t *bias[3] = {v[0].bias, v[1].bias, v[2].bias};

    /* frame 2: every input at the largest activation code, so the clamp is exercised */
    static uint16_t corner[FC_MAX_N_IN];
    for (uint16_t i = 0; i < p[0].n_in; i++)
        corner[i] = 32767;

    FILE *fs = open_out(out_dir, "fc_stim.mem");
    FILE *f1 = open_out(out_dir, "fc1_out.mem");
    FILE *f2 = open_out(out_dir, "fc2_out.mem");
    FILE *f3 = open_out(out_dir, "fc3_out.mem");

    for (uint16_t i = 0; i < p[0].n_in; i++)
        put_hex16(fs, v[0].x[i]);
    for (uint16_t i = 0; i < p[0].n_in; i++)
        put_hex16(fs, corner[i]);

    srand(20260924);
    fc_top_init(&dut, p, rom, bias);
    fc_top_reset(&dut);

    int n1[FRAMES], n2[FRAMES], n3[FRAMES];
    run_frame(p, v[0].x, 80, f1, f2, f3, &n1[0], &n2[0], &n3[0]);
    run_frame(p, corner, 80, f1, f2, f3, &n1[1], &n2[1], &n3[1]);

    fclose(fs); fclose(f1); fclose(f2); fclose(f3);

    uint32_t sat = dut.l[0].u_relu_quant.dbg_sat_cnt + dut.l[1].u_relu_quant.dbg_sat_cnt
                   + dut.l[2].u_quant_signed.dbg_sat_cnt;

    FILE *fp = open_out(out_dir, "fc_params.txt");
    fprintf(fp, "FRAMES=%d N_IN=%u NSTIM=%u\n", FRAMES, p[0].n_in, FRAMES * p[0].n_in);
    for (int i = 0; i < 3; i++)
        fprintf(fp, "FC%d N_IN=%u N_OUT=%u L=%u NUM_CHUNK=%u ACC_W=%u SCALE_EXP=%u RELU=%u NOUT=%d\n",
                i + 1, p[i].n_in, p[i].n_out, p[i].lanes, p[i].num_chunk, p[i].acc_w,
                p[i].scale_exp, p[i].relu, (i == 0 ? n1[0] + n1[1] : i == 1 ? n2[0] + n2[1]
                                                                            : n3[0] + n3[1]));
    fclose(fp);

    printf("fc vectors: stim %u, FC1 %d, FC2 %d, FC3 %d (two frames), clamped %u\n",
           FRAMES * p[0].n_in, n1[0] + n1[1], n2[0] + n2[1], n3[0] + n3[1], sat);

    for (int f = 0; f < FRAMES; f++)
        if (n1[f] != p[0].n_out || n2[f] != p[1].n_out || n3[f] != p[2].n_out)
        {
            fprintf(stderr, "frame %d is short: FC1 %d, FC2 %d, FC3 %d\n",
                    f + 1, n1[f], n2[f], n3[f]);
            return 1;
        }
    return 0;
}
