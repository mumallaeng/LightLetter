#include "fc_feature_buf.h"
#include <string.h>

void fc_feature_buf_init(fc_feature_buf_t *m, uint16_t depth)
{
    m->depth = depth;
    fc_feature_buf_reset(m);
}

void fc_feature_buf_reset(fc_feature_buf_t *m)
{
    /* the RAM itself has no reset; clearing it here only makes tests deterministic */
    memset(m->mem, 0, sizeof m->mem);
    m->we_next = 0;
    m->waddr_next = 0;
    m->wdata_next = 0;
}

/* always @(*) : asynchronous read */
void fc_feature_buf_comb(fc_feature_buf_t *m, const fc_feature_buf_in_t *in, fc_feature_buf_out_t *out)
{
    out->rdata = (in->raddr < m->depth) ? m->mem[in->raddr] : 0;

    m->we_next    = in->we;
    m->waddr_next = in->waddr;
    m->wdata_next = in->wdata;
}

/* always @(posedge clk) : write port */
void fc_feature_buf_seq(fc_feature_buf_t *m)
{
    if (m->we_next && m->waddr_next < m->depth)
        m->mem[m->waddr_next] = m->wdata_next;
    m->we_next = 0;
}
