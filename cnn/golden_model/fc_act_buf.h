/* fc_act_buf: one layer's activations. Written one value per clock, read combinationally
 * (distributed RAM in the RTL), so the value at raddr is available in the same cycle. */
#ifndef FC_ACT_BUF_H
#define FC_ACT_BUF_H

#include "fc_common.h"

typedef struct
{
    uint8_t  we;
    uint16_t waddr;
    uint16_t wdata;
    uint16_t raddr;
} fc_act_buf_in_t;

typedef struct
{
    uint16_t rdata;
} fc_act_buf_out_t;

typedef struct
{
    uint16_t depth;
    uint16_t mem[FC_ACT_IN];

    /* pending write (reg / reg_next) */
    uint8_t  we_next;
    uint16_t waddr_next, wdata_next;
} fc_act_buf_t;

void fc_act_buf_init(fc_act_buf_t *m, uint16_t depth);
void fc_act_buf_reset(fc_act_buf_t *m);
void fc_act_buf_comb(fc_act_buf_t *m, const fc_act_buf_in_t *in, fc_act_buf_out_t *out);
void fc_act_buf_seq(fc_act_buf_t *m);

#endif
