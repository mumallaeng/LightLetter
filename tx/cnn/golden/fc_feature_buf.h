/* fc_feature_buf: synchronous write, asynchronous read (distributed RAM in the RTL). */
#ifndef FC_FEATURE_BUF_H
#define FC_FEATURE_BUF_H

#include "fc_common.h"

typedef struct
{
    uint8_t  we;
    uint16_t waddr;
    uint16_t wdata;
    uint16_t raddr;
} fc_feature_buf_in_t;

typedef struct
{
    uint16_t rdata;
} fc_feature_buf_out_t;

typedef struct
{
    uint16_t depth;
    uint16_t mem[FC_FC1_IN];

    /* pending write (reg / reg_next) */
    uint8_t  we_next;
    uint16_t waddr_next, wdata_next;
} fc_feature_buf_t;

void fc_feature_buf_init(fc_feature_buf_t *m, uint16_t depth);
void fc_feature_buf_reset(fc_feature_buf_t *m);
void fc_feature_buf_comb(fc_feature_buf_t *m, const fc_feature_buf_in_t *in, fc_feature_buf_out_t *out);
void fc_feature_buf_seq(fc_feature_buf_t *m);

#endif
