#include "output_fifo.h"

void output_fifo_reset(output_fifo_t *m)
{
    m->wptr     = 0;  m->wptr_next     = 0;
    m->rptr     = 0;  m->rptr_next     = 0;
    m->count    = 0;  m->count_next    = 0;
    m->bram_q   = 0;  m->bram_q_next   = 0;
    m->byp_en   = 0;  m->byp_en_next   = 0;
    m->byp_data = 0;  m->byp_data_next = 0;

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
    out->dout  = m->byp_en ? m->byp_data : m->bram_q;
    out->empty = empty;

    // ========== Pointer / Count Logic ==========
    m->wptr_next  = we  ? ((m->wptr + 1) & (OF_DEPTH - 1)) : m->wptr;
    m->rptr_next  = pop ? ((m->rptr + 1) & (OF_DEPTH - 1)) : m->rptr;
    m->count_next = m->count + we - pop;

    /* sync read of the next head; bypass when that slot is being written this cycle */
    m->bram_q_next   = m->mem[m->rptr_next];
    m->byp_en_next   = we && (m->wptr == m->rptr_next);
    m->byp_data_next = in->din;

    m->w_we    = we;
    m->w_wdata = in->din;

    m->w_dbg_overflow = in->push && !we;
}

/* always @(posedge clk) */
void output_fifo_seq(output_fifo_t *m)
{
    if (m->w_we)
        m->mem[m->wptr] = m->w_wdata;

    m->wptr     = m->wptr_next;
    m->rptr     = m->rptr_next;
    m->count    = m->count_next;
    m->bram_q   = m->bram_q_next;
    m->byp_en   = m->byp_en_next;
    m->byp_data = m->byp_data_next;

    m->dbg_overflow_cnt += m->w_dbg_overflow;
    if (m->count > m->dbg_max_count)
        m->dbg_max_count = m->count;
}
