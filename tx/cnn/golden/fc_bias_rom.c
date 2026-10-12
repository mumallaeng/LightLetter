#include "fc_bias_rom.h"
#include <string.h>

void fc_bias_rom_init(fc_bias_rom_t *m, const int32_t (*rom)[FC_P])
{
    m->rom = rom;
}

/* always @(*) */
void fc_bias_rom_comb(fc_bias_rom_t *m, const fc_bias_rom_in_t *in, fc_bias_rom_out_t *out)
{
    if (in->addr < FC_BIAS_ROWS)
        memcpy(out->b, m->rom[in->addr], sizeof out->b);
    else
        memset(out->b, 0, sizeof out->b);
}
