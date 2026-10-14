#include "fc_top.h"

void fc_top_init(fc_top_t *m, const fc_param_t p[3], const int16_t *const rom[3],
                 const int32_t *const bias[3])
{
    for (int i = 0; i < 3; i++)
        fc_layer_init(&m->l[i], &p[i], rom[i], bias[i]);

    fc_top_reset(m);
}

void fc_top_reset(fc_top_t *m)
{
    for (int i = 0; i < 3; i++)
        fc_layer_reset(&m->l[i]);

    m->w_l1_fire = m->w_l2_fire = 0;
    m->w_l1_data = m->w_l2_data = 0;
}

/* always @(*) — two passes so the ready/valid chain settles (comb is idempotent) */
void fc_top_comb(fc_top_t *m, const fc_top_in_t *in, fc_top_out_t *out)
{
    fc_layer_in_t  li[3];
    fc_layer_out_t lo[3];

    for (int i = 0; i < 3; i++)
    {
        li[i].in_data   = 0;
        li[i].in_valid  = 0;
        li[i].out_ready = 0;
        fc_layer_comb(&m->l[i], &li[i], &lo[i]);
    }

    for (int pass = 0; pass < 2; pass++)
    {
        li[0].in_data   = in->fc_in_data;
        li[0].in_valid  = in->fc_in_valid;
        li[0].out_ready = lo[1].in_ready;

        li[1].in_data   = (uint16_t)lo[0].out_data;
        li[1].in_valid  = lo[0].out_valid;
        li[1].out_ready = lo[2].in_ready;

        li[2].in_data   = (uint16_t)lo[1].out_data;
        li[2].in_valid  = lo[1].out_valid;
        li[2].out_ready = in->logit_ready;

        for (int i = 2; i >= 0; i--)
            fc_layer_comb(&m->l[i], &li[i], &lo[i]);
    }

    out->fc_in_ready = lo[0].in_ready;
    out->logit_data  = lo[2].out_data;
    out->logit_valid = lo[2].out_valid;

    m->w_l1_fire = lo[0].out_valid && li[0].out_ready;
    m->w_l1_data = lo[0].out_data;
    m->w_l2_fire = lo[1].out_valid && li[1].out_ready;
    m->w_l2_data = lo[1].out_data;
}

/* always @(posedge clk) */
void fc_top_seq(fc_top_t *m)
{
    for (int i = 0; i < 3; i++)
        fc_layer_seq(&m->l[i]);
}
