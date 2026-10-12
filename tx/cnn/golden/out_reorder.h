/*
 * Output reorder buffer: BRAM that holds one layer's packed entries, written in arrival
 * order (pixel-major) and read group-major: for each group g of PACK channels, every pixel.
 *   conv1 (PACK=3, 2 groups) : channels 0-2 for all pixels, then channels 3-5
 *   conv2 (PACK=1, 16 groups): channel 0 for all pixels, ..., channel 15
 * First-word fall-through: dout is valid while avail.
 */
#ifndef OUT_REORDER_H
#define OUT_REORDER_H

#include "ob_common.h"

#define OR_MAX_ENTRIES 2048 /* >= max(676 * 2, 121 * 16) */

/* input ports */
typedef struct
{
    uint8_t  push;
    uint64_t din;   /* [WIDTH-1:0] */
    uint8_t  rd_en; /* out_ready; pop = rd_en & avail */
} out_reorder_in_t;

/* output ports */
typedef struct
{
    uint64_t dout;
    uint8_t  avail;      /* entry at the read address has been written */
    uint8_t  last_pixel; /* read address is at pixel N-1 */
} out_reorder_out_t;

typedef struct
{
    uint16_t n;      /* pixels per frame */
    uint8_t  groups; /* C_OUT / PACK */

    uint64_t mem[OR_MAX_ENTRIES];

    /* registers: reg / reg_next */
    uint16_t wr_cnt,   wr_cnt_next;   /* entries written this frame; also the write address */
    uint8_t  rd_grp,   rd_grp_next;
    uint16_t rd_pix,   rd_pix_next;
    uint16_t rd_addr,  rd_addr_next;  /* = rd_pix * groups + rd_grp */
    uint64_t bram_q,   bram_q_next;
    uint8_t  byp_en,   byp_en_next;
    uint64_t byp_data, byp_data_next;

    /* write request carried from comb to seq */
    uint8_t  w_we;
    uint64_t w_wdata;

    /* debug: a new frame started before the previous one was fully read out (must stay 0) */
    uint8_t  w_dbg_overrun;
    uint32_t dbg_overrun_cnt;
    uint16_t dbg_max_fill;
} out_reorder_t;

void out_reorder_init(out_reorder_t *m, uint16_t n, uint8_t groups);
void out_reorder_reset(out_reorder_t *m);
void out_reorder_comb(out_reorder_t *m, const out_reorder_in_t *in, out_reorder_out_t *out);
void out_reorder_seq(out_reorder_t *m);

#endif
