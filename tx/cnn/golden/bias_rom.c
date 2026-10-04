#include <string.h>
#include "bias_rom.h"

void bias_rom_load(bias_rom_t *m, const int32_t *data, uint8_t depth)
{
    memset(m, 0, sizeof(*m));
    m->depth = depth;
    memcpy(m->mem, data, sizeof(int32_t) * depth);
}

/* always @(*) */
void bias_rom_comb(const bias_rom_t *m, const bias_rom_in_t *in, bias_rom_out_t *out)
{
    out->rdata = m->mem[in->addr];
}
