/* Bias ROM: per-output-channel bias, async read */
#ifndef BIAS_ROM_H
#define BIAS_ROM_H

#include "ob_common.h"

/* input ports */
typedef struct
{
    uint8_t addr; /* out_ch_cnt */
} bias_rom_in_t;

/* output ports */
typedef struct
{
    int32_t rdata;
} bias_rom_out_t;

typedef struct
{
    int32_t mem[OB_MAX_C_OUT];
    uint8_t depth;
} bias_rom_t;

void bias_rom_load(bias_rom_t *m, const int32_t *data, uint8_t depth); /* $readmemh */
void bias_rom_comb(const bias_rom_t *m, const bias_rom_in_t *in, bias_rom_out_t *out);

#endif
