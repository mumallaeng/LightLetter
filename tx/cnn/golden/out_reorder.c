#include "out_reorder.h"

void out_reorder_init(out_reorder_t *m, uint16_t n, uint8_t groups)
{
    m->n      = n;
    m->groups = groups;
    out_reorder_reset(m);
}

void out_reorder_reset(out_reorder_t *m)
{
    m->wr_cnt   = 0;  m->wr_cnt_next   = 0;
    m->rd_grp   = 0;  m->rd_grp_next   = 0;
    m->rd_pix   = 0;  m->rd_pix_next   = 0;
    m->rd_addr  = 0;  m->rd_addr_next  = 0;
    m->bram_q   = 0;  m->bram_q_next   = 0;
    m->byp_en   = 0;  m->byp_en_next   = 0;
    m->byp_data = 0;  m->byp_data_next = 0;

    m->w_we    = 0;
    m->w_wdata = 0;

    m->w_dbg_overrun   = 0;
    m->dbg_overrun_cnt = 0;
    m->dbg_max_fill    = 0;
}

/* always @(*) */
void out_reorder_comb(out_reorder_t *m, const out_reorder_in_t *in, out_reorder_out_t *out)
{
    uint16_t total    = (uint16_t)(m->n * m->groups);
    uint8_t  avail    = (m->wr_cnt > m->rd_addr);
    uint8_t  pop      = in->rd_en && avail;
    uint8_t  pix_last = (m->rd_pix == m->n - 1);
    uint8_t  grp_last = (m->rd_grp == m->groups - 1);
    uint8_t  rd_done  = pop && pix_last && grp_last;          /* last entry of the frame leaves */

    // ========== Output Logic ==========
    out->dout       = m->byp_en ? m->byp_data : m->bram_q;
    out->avail      = avail;
    out->last_pixel = pix_last;

    // ========== Read Pointer Logic ==========
    m->rd_grp_next  = m->rd_grp;
    m->rd_pix_next  = m->rd_pix;
    m->rd_addr_next = m->rd_addr;
    if (pop)
    {
        if (pix_last)
        {
            m->rd_pix_next  = 0;
            m->rd_grp_next  = grp_last ? 0 : m->rd_grp + 1;
            m->rd_addr_next = grp_last ? 0 : m->rd_grp + 1;      /* pixel 0 of the next group */
        }
        else
        {
            m->rd_pix_next  = m->rd_pix + 1;
            m->rd_addr_next = m->rd_addr + m->groups;
        }
    }

    // ========== Write Logic ==========
    /* the frame's slots are released only when its last entry has been read */
    uint8_t frame_full = (m->wr_cnt == total);
    uint8_t we         = in->push && !frame_full;

    m->wr_cnt_next = rd_done ? 0 : (we ? m->wr_cnt + 1 : m->wr_cnt);

    /* sync read of the next address; bypass when that slot is being written this cycle */
    m->bram_q_next   = m->mem[m->rd_addr_next];
    m->byp_en_next   = we && (m->wr_cnt == m->rd_addr_next);
    m->byp_data_next = in->din;

    m->w_we    = we;
    m->w_wdata = in->din;

    m->w_dbg_overrun = in->push && frame_full;
}

/* always @(posedge clk) */
void out_reorder_seq(out_reorder_t *m)
{
    if (m->w_we)
        m->mem[m->wr_cnt] = m->w_wdata;

    m->wr_cnt   = m->wr_cnt_next;
    m->rd_grp   = m->rd_grp_next;
    m->rd_pix   = m->rd_pix_next;
    m->rd_addr  = m->rd_addr_next;
    m->bram_q   = m->bram_q_next;
    m->byp_en   = m->byp_en_next;
    m->byp_data = m->byp_data_next;

    m->dbg_overrun_cnt += m->w_dbg_overrun;
    if (m->wr_cnt > m->dbg_max_fill)
        m->dbg_max_fill = m->wr_cnt;
}
