/*
 * Input staging: ping-pong buffers of LANES values, filled from the input stream and read
 * by fc_mac. The calc buffer feeds the current chunk while the fill buffer collects the next.
 */
#ifndef FC_STAGING_H
#define FC_STAGING_H

#include "fc_common.h"

/* input ports */
typedef struct
{
    uint16_t in_data;
    uint8_t  in_valid;
    uint8_t  chunk_done; /* <- fc_ctrl: this chunk is done, swap fill/calc */
} fc_staging_in_t;

/* output ports */
typedef struct
{
    uint8_t  in_ready;
    uint8_t  calc_full;               /* the calc buffer holds a whole chunk */
    uint8_t  next_full;               /* the fill buffer already holds the next chunk */
    uint16_t x[FC_MAX_LANES];         /* calc buffer contents */
} fc_staging_out_t;

typedef struct
{
    fc_param_t p;

    /* registers: reg / reg_next */
    uint16_t buf[2][FC_MAX_LANES], buf_next[2][FC_MAX_LANES];
    uint8_t  full[2],   full_next[2];
    uint8_t  fill_sel,  fill_sel_next;
    uint8_t  calc_sel,  calc_sel_next;
    uint8_t  fill_cnt,  fill_cnt_next;
    uint8_t  fill_chunk, fill_chunk_next; /* chunk index being collected */

    /* debug: reserved for write errors (must stay 0); backpressure itself is not an error */
    uint8_t  w_dbg_drop;
    uint32_t dbg_drop_cnt;
} fc_staging_t;

void fc_staging_init(fc_staging_t *m, const fc_param_t *p);
void fc_staging_reset(fc_staging_t *m);
void fc_staging_comb(fc_staging_t *m, const fc_staging_in_t *in, fc_staging_out_t *out);
void fc_staging_seq(fc_staging_t *m);

#endif
