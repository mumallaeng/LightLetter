#include "fc_vec_io.h"
#include <stdio.h>
#include <string.h>

int fc_read_vec(const char *dir, int layer, fc_vec_t *out)
{
    char path[512];
    snprintf(path, sizeof path, "%s/fc%d.txt", dir, layer);
    FILE *f = fopen(path, "r");
    if (!f)
    {
        perror(path);
        return -1;
    }
    unsigned lay, n_in, n_out, lanes, chunk, scale_exp, relu;
    int ok = fscanf(f, "%u %u %u %u %u %u %u", &lay, &n_in, &n_out, &lanes, &chunk, &scale_exp, &relu) == 7
             && n_in <= FC_MAX_N_IN && n_out <= FC_MAX_N_OUT;
    if (ok)
    {
        memset(out, 0, sizeof *out);
        out->n_in = (uint16_t)n_in; out->n_out = (uint8_t)n_out;
        out->scale_exp = (uint8_t)scale_exp; out->relu = (uint8_t)relu;
    }
    long v;
    for (unsigned i = 0; ok && i < n_out; i++) { ok = fscanf(f, "%ld", &v) == 1; out->bias[i] = (int32_t)v; }
    for (unsigned i = 0; ok && i < n_in; i++)  { ok = fscanf(f, "%ld", &v) == 1; out->x[i] = (uint16_t)v; }
    for (unsigned r = 0; ok && r < chunk * n_out; r++)
        for (unsigned i = 0; ok && i < lanes; i++)
        {
            ok = fscanf(f, "%ld", &v) == 1;
            unsigned g = r / n_out, n = r % n_out, col = g * lanes + i;
            if (col < n_in)
                out->w[n][col] = (int16_t)v;
        }
    for (unsigned i = 0; ok && i < n_out; i++) { ok = fscanf(f, "%ld", &v) == 1; out->expected[i] = (int16_t)v; }
    fclose(f);
    if (!ok)
        fprintf(stderr, "%s: malformed vector file\n", path);
    return ok ? 0 : -1;
}

int fc_rom_build(fc_rom_image_t *img, const fc_vec_t v[FC_LAYERS])
{
    memset(img, 0, sizeof *img);
    for (int l = 0; l < FC_LAYERS; l++)
    {
        const fc_layer_cfg_t *c = &FC_CFG[l];
        if (v[l].n_in != c->n_in || v[l].n_out != c->n_out)
        {
            fprintf(stderr, "fc%d: vectors are %ux%u, FC_CFG says %ux%u\n", l + 1,
                    v[l].n_in, v[l].n_out, c->n_in, c->n_out);
            return -1;
        }
        for (uint8_t g = 0; g < c->groups; g++)
        {
            for (int lane = 0; lane < FC_P; lane++)
            {
                unsigned n = (unsigned)g * FC_P + lane;
                if (n >= c->n_out) continue;
                img->b[c->bias_base + g][lane] = v[l].bias[n];
                for (uint16_t i = 0; i < c->n_in; i++)
                    img->w[c->rom_base + (uint16_t)g * c->n_in + i][lane] = v[l].w[n][i];
            }
        }
    }
    return 0;
}

void fc_ref_layer(const fc_vec_t *v, const uint16_t *x, fc_acc_t *sum)
{
    for (uint8_t n = 0; n < v->n_out; n++)
    {
        fc_acc_t s = v->bias[n];
        for (uint16_t i = 0; i < v->n_in; i++)
            s += (fc_acc_t)x[i] * v->w[n][i];
        sum[n] = s;
    }
}
