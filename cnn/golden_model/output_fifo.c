#include "output_fifo.h"

void output_fifo_reset(output_fifo_t *m)
{
    m->wptr  = 0;  m->wptr_next  = 0;
    m->rptr  = 0;  m->rptr_next  = 0;
    m->count = 0;  m->count_next = 0;
    m->dout  = 0;  m->dout_next  = 0;

    m->w_we    = 0;
    m->w_wdata = 0;

    m->w_dbg_overflow   = 0;
    m->dbg_overflow_cnt = 0;
    m->dbg_max_count    = 0;
}

/* always @(*) */
void output_fifo_comb(output_fifo_t *m, const output_fifo_in_t *in, output_fifo_out_t *out)
{
    uint8_t empty = (m->count == 0);
    uint8_t full  = (m->count == OF_DEPTH);

    uint8_t pop = in->rd_en && !empty;
    uint8_t we  = in->push && (!full || pop);

    // ========== Output Logic ==========
    out->dout  = m->dout;
    out->empty = empty;

    // ========== Pointer / Count Logic ==========
    m->wptr_next  = we  ? ((m->wptr + 1) & (OF_DEPTH - 1)) : m->wptr;
    m->rptr_next  = pop ? ((m->rptr + 1) & (OF_DEPTH - 1)) : m->rptr;
    m->count_next = m->count + we - pop;

    /* sync read: fetch the next head; bypass when it is being written this cycle */
    if (we && m->wptr == m->rptr_next)
        m->dout_next = in->din;
    else
        m->dout_next = m->mem[m->rptr_next];

    m->w_we    = we;
    m->w_wdata = in->din;

    m->w_dbg_overflow = in->push && !we;
}

/* always @(posedge clk) */
void output_fifo_seq(output_fifo_t *m)
{
    if (m->w_we)
        m->mem[m->wptr] = m->w_wdata;

    m->wptr  = m->wptr_next;
    m->rptr  = m->rptr_next;
    m->count = m->count_next;
    m->dout  = m->dout_next;

    m->dbg_overflow_cnt += m->w_dbg_overflow;
    if (m->count > m->dbg_max_count)
        m->dbg_max_count = m->count;
}
