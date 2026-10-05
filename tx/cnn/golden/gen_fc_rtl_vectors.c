/*
 * Runs the C golden model on vectors/fc{1,2,3}.txt and writes $readmemh files for the RTL
 * testbench (tx/cnn/tb/tb_fc.v):
 *
 *   <out>/fc_stim.mem    input codes fed to fc_top, arrival order (16 bits)
 *   <out>/fc1_out.mem    golden FC1 outputs as fc_quant_out writes them, neuron order (16 bits)
 *   <out>/fc2_out.mem    golden FC2 outputs (16 bits)
 *   <out>/fc3_out.mem    golden logit stream, class order (16 bits, signed)
 *   <out>/fc_params.txt  parameters and entry counts
 *
 * The ROM files come from export_fc_vectors.py (tx/cnn/rtl/mem/fc_weight.mem, fc_bias.mem).
 * Frame 1 is the real image, frame 2 has every input at the largest activation code.
 *
 *   make -f fc.mk rtl-vectors
 */
#include <stdio.h>
#include <stdlib.h>
#include "fc_top.h"
#include "fc_vec_io.h"

#define FRAMES 2

static fc_top_t       dut;
static fc_vec_t       v[FC_LAYERS];
static fc_rom_image_t img;

static void put_hex16(FILE *f, uint16_t x) { fprintf(f, "%04x\n", x); }

static FILE *open_out(const char *dir, const char *name)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "w");
    if (!f) { perror(path); exit(1); }
    return f;
}

static long run_frame(const uint16_t *x, int ready_pct, FILE *f1, FILE *f2, FILE *f3, int *n1, int *n2, int *n3)
{
    uint16_t fed = 0;
    *n1 = *n2 = *n3 = 0;
    long cycle = 0;
    for (; cycle < 400000 && *n3 < FC_CFG[2].n_out; cycle++)
    {
        fc_top_in_t  in;
        fc_top_out_t out;
        in.fc_in_data  = (fed < FC_CFG[0].n_in) ? x[fed] : 0;
        in.fc_in_valid = (fed < FC_CFG[0].n_in);
        in.logit_ready = (rand() % 100) < ready_pct;
        fc_top_comb(&dut, &in, &out);
        if (in.fc_in_valid && out.fc_in_ready) fed++;
        if (dut.w_l1_fire) { put_hex16(f1, (uint16_t)dut.w_l1_data); (*n1)++; }
        if (dut.w_l2_fire) { put_hex16(f2, (uint16_t)dut.w_l2_data); (*n2)++; }
        if (out.logit_valid && in.logit_ready) { put_hex16(f3, (uint16_t)out.logit_data); (*n3)++; }
        fc_top_seq(&dut);
    }
    return cycle;
}

int main(int argc, char **argv)
{
    const char *in_dir  = argc > 1 ? argv[1] : "vectors";
    const char *out_dir = argc > 2 ? argv[2] : "../tb/vectors";
    for (int i = 0; i < FC_LAYERS; i++)
        if (fc_read_vec(in_dir, i + 1, &v[i]) != 0) return 1;
    if (fc_rom_build(&img, v) != 0) return 1;

    static uint16_t corner[FC_MAX_N_IN];
    for (uint16_t i = 0; i < FC_CFG[0].n_in; i++) corner[i] = 32767;

    FILE *fs = open_out(out_dir, "fc_stim.mem");
    FILE *f1 = open_out(out_dir, "fc1_out.mem");
    FILE *f2 = open_out(out_dir, "fc2_out.mem");
    FILE *f3 = open_out(out_dir, "fc3_out.mem");
    for (uint16_t i = 0; i < FC_CFG[0].n_in; i++) put_hex16(fs, v[0].x[i]);
    for (uint16_t i = 0; i < FC_CFG[0].n_in; i++) put_hex16(fs, corner[i]);

    srand(20260924);
    fc_top_init(&dut, &img);
    int n1[FRAMES], n2[FRAMES], n3[FRAMES];
    long cyc[FRAMES];
    cyc[0] = run_frame(v[0].x, 80, f1, f2, f3, &n1[0], &n2[0], &n3[0]);
    cyc[1] = run_frame(corner, 80, f1, f2, f3, &n1[1], &n2[1], &n3[1]);
    fclose(fs); fclose(f1); fclose(f2); fclose(f3);

    FILE *fp = open_out(out_dir, "fc_params.txt");
    fprintf(fp, "FRAMES=%d N_IN=%u NSTIM=%u P=%d ACC_W=%d ROM_ROWS=%d BIAS_ROWS=%d\n", FRAMES, FC_CFG[0].n_in,
            FRAMES * FC_CFG[0].n_in, FC_P, FC_ACC_W, FC_ROM_ROWS, FC_BIAS_ROWS);
    for (int i = 0; i < FC_LAYERS; i++)
        fprintf(fp, "FC%d N_IN=%u N_OUT=%u GROUPS=%u ROM_BASE=%u BIAS_BASE=%u SCALE_EXP=%u RELU=%u NOUT=%d\n", i + 1,
                FC_CFG[i].n_in, FC_CFG[i].n_out, FC_CFG[i].groups, FC_CFG[i].rom_base, FC_CFG[i].bias_base,
                FC_CFG[i].scale_exp, FC_CFG[i].relu, i == 0 ? n1[0] + n1[1] : i == 1 ? n2[0] + n2[1] : n3[0] + n3[1]);
    fprintf(fp, "CYCLES frame1=%ld frame2=%ld (first input to last logit, logit_ready 80%%)\n", cyc[0], cyc[1]);
    fclose(fp);

    printf("fc vectors: stim %u, FC1 %d, FC2 %d, FC3 %d (two frames), clamped %u, cycles %ld / %ld\n",
           FRAMES * FC_CFG[0].n_in, n1[0] + n1[1], n2[0] + n2[1], n3[0] + n3[1], dut.u_quant_out.dbg_sat_cnt, cyc[0], cyc[1]);
    for (int f = 0; f < FRAMES; f++)
        if (n1[f] != FC_CFG[0].n_out || n2[f] != FC_CFG[1].n_out || n3[f] != FC_CFG[2].n_out)
        {
            fprintf(stderr, "frame %d is short: FC1 %d, FC2 %d, FC3 %d\n", f + 1, n1[f], n2[f], n3[f]);
            return 1;
        }
    return 0;
}
