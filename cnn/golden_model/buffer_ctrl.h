/* Buffer Controller: BRAM for cross-group partial sums (N x C_OUT), sync read/write. conv2 only */
#ifndef BUFFER_CTRL_H
#define BUFFER_CTRL_H

#include "ob_common.h"

/* input ports */
typedef struct
{
    uint16_t raddr; /* next-cycle address (prefetch) */
    uint16_t waddr;
    ob_acc_t wdata;
    uint8_t  we;
} buffer_ctrl_in_t;

/* output ports */
typedef struct
{
    ob_acc_t rdata; /* data for the previous cycle's raddr */
} buffer_ctrl_out_t;

typedef struct
{
    ob_acc_t mem[OB_MAX_BUF_DEPTH];

    /* registers: reg / reg_next */
    ob_acc_t rdata, rdata_next;

    /* write request carried from comb to seq */
    uint16_t w_waddr;
    ob_acc_t w_wdata;
    uint8_t  w_we;
} buffer_ctrl_t;

/* debug: initial marker to catch read-before-write */
#define BUFFER_CTRL_POISON ((ob_acc_t)0x5A5A5A5A5ALL)

void buffer_ctrl_reset(buffer_ctrl_t *m);
void buffer_ctrl_comb(buffer_ctrl_t *m, const buffer_ctrl_in_t *in, buffer_ctrl_out_t *out);
void buffer_ctrl_seq(buffer_ctrl_t *m);

#endif
