/*
 * fc_ctrl: chunk (outer) / neuron (inner) counters, one mac_en per clock.
 * rom_addr leads by one: it addresses the NEXT mac_en so the registered ROM output lines up.
 */
#ifndef FC_CTRL_H
#define FC_CTRL_H

#include "fc_common.h"

typedef enum
{
    FC_IDLE = 0, /* no full calc buffer */
    FC_RUN       /* issuing one neuron per clock */
} fc_state_t;

/* input ports */
typedef struct
{
    uint8_t calc_full; /* <- fc_staging */
    uint8_t next_full; /* <- fc_staging: next chunk already in the fill buffer */
} fc_ctrl_in_t;

/* output ports */
typedef struct
{
    uint8_t  mac_en;
    uint16_t rom_addr; /* address of the next mac_en */
    uint8_t  chunk_done;  /* last neuron of this chunk: swap fill/calc */
} fc_ctrl_out_t;

typedef struct
{
    fc_param_t p;

    /* registers: reg / reg_next */
    fc_state_t state, state_next;
    uint8_t    neuron, neuron_next;
    uint8_t    chunk,  chunk_next;
} fc_ctrl_t;

void fc_ctrl_init(fc_ctrl_t *m, const fc_param_t *p);
void fc_ctrl_reset(fc_ctrl_t *m);
void fc_ctrl_comb(fc_ctrl_t *m, const fc_ctrl_in_t *in, fc_ctrl_out_t *out);
void fc_ctrl_seq(fc_ctrl_t *m);

const char *fc_state_name(fc_state_t s);

#endif
