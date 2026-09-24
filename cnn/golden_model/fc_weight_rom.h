/* Weight ROM: one row per (chunk, neuron), LANES weights per row, synchronous read */
#ifndef FC_WEIGHT_ROM_H
#define FC_WEIGHT_ROM_H

#include "fc_common.h"

/* input ports */
typedef struct
{
    uint16_t addr;
} fc_weight_rom_in_t;

/* output ports */
typedef struct
{
    int16_t w[FC_MAX_LANES];
} fc_weight_rom_out_t;

typedef struct
{
    fc_param_t p;

    int16_t mem[FC_MAX_ROM][FC_MAX_LANES];

    /* registers: reg / reg_next (BRAM read register) */
    int16_t q[FC_MAX_LANES], q_next[FC_MAX_LANES];
} fc_weight_rom_t;

void fc_weight_rom_init(fc_weight_rom_t *m, const fc_param_t *p, const int16_t *rows);
void fc_weight_rom_reset(fc_weight_rom_t *m);
void fc_weight_rom_comb(fc_weight_rom_t *m, const fc_weight_rom_in_t *in, fc_weight_rom_out_t *out);
void fc_weight_rom_seq(fc_weight_rom_t *m);

#endif
