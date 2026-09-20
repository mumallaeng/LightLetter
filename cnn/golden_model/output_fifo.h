/* Output FIFO: sync BRAM FIFO, first-word fall-through (dout valid while !empty) */
#ifndef OUTPUT_FIFO_H
#define OUTPUT_FIFO_H

#include "ob_common.h"

#define OF_DEPTH 2048 /* power of two */

/* input ports */
typedef struct
{
    uint8_t  push;
    uint64_t din; /* [FIFO_W-1:0] */
    uint8_t  rd_en; /* out_ready; pop = rd_en & !empty */
} output_fifo_in_t;

/* output ports */
typedef struct
{
    uint64_t dout;
    uint8_t  empty;
} output_fifo_out_t;

typedef struct
{
    uint64_t mem[OF_DEPTH];

    /* registers: reg / reg_next */
    uint16_t wptr,     wptr_next;
    uint16_t rptr,     rptr_next;
    uint16_t count,    count_next;    /* 0..OF_DEPTH */
    uint64_t bram_q,   bram_q_next;   /* BRAM read register */
    uint8_t  byp_en,   byp_en_next;   /* head was written last cycle, not yet readable */
    uint64_t byp_data, byp_data_next;

    /* write request carried from comb to seq */
    uint8_t  w_we;
    uint64_t w_wdata;

    /* debug: pushes dropped because the FIFO was full (must stay 0) */
    uint8_t  w_dbg_overflow;
    uint32_t dbg_overflow_cnt;
    uint16_t dbg_max_count;
} output_fifo_t;

void output_fifo_reset(output_fifo_t *m);
void output_fifo_comb(output_fifo_t *m, const output_fifo_in_t *in, output_fifo_out_t *out);
void output_fifo_seq(output_fifo_t *m);

#endif
