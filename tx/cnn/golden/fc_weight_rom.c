#include <string.h>
#include "fc_weight_rom.h"

/* rows: (num_chunk * n_out) x lanes, row-major */
void fc_weight_rom_init(fc_weight_rom_t *m, const fc_param_t *p, const int16_t *rows)
{
    m->p = *p;
    memset(m->mem, 0, sizeof(m->mem));

    uint16_t depth = (uint16_t)((uint16_t)p->num_chunk * p->n_out);
    for (uint16_t r = 0; r < depth; r++)
        for (uint8_t i = 0; i < p->lanes; i++)
            m->mem[r][i] = rows[(size_t)r * p->lanes + i];

    fc_weight_rom_reset(m);
}

/* no reset on the BRAM read register in RTL; the model clears it for a defined start */
void fc_weight_rom_reset(fc_weight_rom_t *m)
{
    memset(m->q, 0, sizeof(m->q));
}

/* always @(*) */
void fc_weight_rom_comb(fc_weight_rom_t *m, const fc_weight_rom_in_t *in, fc_weight_rom_out_t *out)
{
    memcpy(m->q_next, m->mem[in->addr], sizeof(m->q_next));
    memcpy(out->w, m->q, sizeof(out->w));
}

/* always @(posedge clk) */
void fc_weight_rom_seq(fc_weight_rom_t *m)
{
    memcpy(m->q, m->q_next, sizeof(m->q));
}
