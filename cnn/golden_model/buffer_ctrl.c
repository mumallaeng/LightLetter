#include "buffer_ctrl.h"

void buffer_ctrl_reset(buffer_ctrl_t *m)
{
    for (int i = 0; i < OB_MAX_BUF_DEPTH; i++)
        m->mem[i] = ob_sext(BUFFER_CTRL_POISON, OB_ACC_W);
    m->rdata   = 0;  m->rdata_next = 0;
    m->w_waddr = 0;
    m->w_wdata = 0;
    m->w_we    = 0;
}

/* always @(*) */
void buffer_ctrl_comb(buffer_ctrl_t *m, const buffer_ctrl_in_t *in, buffer_ctrl_out_t *out)
{
    out->rdata    = m->rdata;
    m->rdata_next = m->mem[in->raddr];

    m->w_waddr = in->waddr;
    m->w_wdata = in->wdata;
    m->w_we    = in->we;
}

/* always @(posedge clk) */
void buffer_ctrl_seq(buffer_ctrl_t *m)
{
    m->rdata = m->rdata_next;
    if (m->w_we)
        m->mem[m->w_waddr] = ob_sext(m->w_wdata, OB_ACC_W);
}
