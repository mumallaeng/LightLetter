#include "fc_weight_rom.h"
#include <string.h>

void fc_weight_rom_init(fc_weight_rom_t *m, const int16_t (*rom)[FC_P])
{
    m->rom = rom;
    memset(m->w_reg, 0, sizeof m->w_reg);
    m->addr_next = 0;
}

/* always @(*) */
void fc_weight_rom_comb(fc_weight_rom_t *m, const fc_weight_rom_in_t *in, fc_weight_rom_out_t *out)
{
    memcpy(out->w, m->w_reg, sizeof out->w);
    m->addr_next = in->addr;
}

/* always @(posedge clk) : w_reg <= rom[addr] */
void fc_weight_rom_seq(fc_weight_rom_t *m)
{
    if (m->addr_next < FC_ROM_ROWS)
        memcpy(m->w_reg, m->rom[m->addr_next], sizeof m->w_reg);
    else
        memset(m->w_reg, 0, sizeof m->w_reg);
}
