/* fc_bias_rom: P INT32 biases per row, one row per (layer, group), asynchronous read. */
#ifndef FC_BIAS_ROM_H
#define FC_BIAS_ROM_H

#include "fc_common.h"

typedef struct
{
    uint8_t addr;
} fc_bias_rom_in_t;

typedef struct
{
    int32_t b[FC_P];
} fc_bias_rom_out_t;

typedef struct
{
    const int32_t (*rom)[FC_P];
} fc_bias_rom_t;

void fc_bias_rom_init(fc_bias_rom_t *m, const int32_t (*rom)[FC_P]);
void fc_bias_rom_comb(fc_bias_rom_t *m, const fc_bias_rom_in_t *in, fc_bias_rom_out_t *out);

#endif
