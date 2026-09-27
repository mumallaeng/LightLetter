/* fc_weight_rom: P weights per row, synchronous read (BRAM): the row addressed in one cycle
 * is on w_out in the next. Row = FC_CFG[l].rom_base + g * n_in + i, lane = neuron g*P + lane. */
#ifndef FC_WEIGHT_ROM_H
#define FC_WEIGHT_ROM_H

#include "fc_common.h"

typedef struct
{
    uint16_t addr;
} fc_weight_rom_in_t;

typedef struct
{
    int16_t w[FC_P];
} fc_weight_rom_out_t;

typedef struct
{
    const int16_t (*rom)[FC_P];

    /* read register (reg / reg_next) */
    int16_t  w_reg[FC_P];
    uint16_t addr_next;
} fc_weight_rom_t;

void fc_weight_rom_init(fc_weight_rom_t *m, const int16_t (*rom)[FC_P]);
void fc_weight_rom_comb(fc_weight_rom_t *m, const fc_weight_rom_in_t *in, fc_weight_rom_out_t *out);
void fc_weight_rom_seq(fc_weight_rom_t *m);

#endif
