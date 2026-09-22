#include <string.h>
#include "pool_l1_ctrl.h"

void pool_l1_ctrl_reset(pool_l1_ctrl_t *m)
{
    memset(m, 0, sizeof(*m));
}

/* always @(*) */
void pool_l1_ctrl_comb(pool_l1_ctrl_t *m, const pool_l1_ctrl_in_t *in, pool_l1_ctrl_out_t *out)
{
    uint8_t row_last = (m->row_cnt == POOL_L1_IN_H - 1);
    uint8_t col_last = (m->col_cnt == POOL_L1_IN_W - 1);

    // ========== Output Logic ==========
    out->row_odd   = m->row_cnt & 1; // LSB of cnt value
    out->col_odd   = m->col_cnt & 1;
    out->win_valid = out->row_odd & out->col_odd;

    out->out_ready    = in->pool_ready | !out->win_valid;
    out->pixel_valid  = in->out_valid & out->out_ready;
    out->pool_valid   = in->out_valid & out->win_valid;
    out->pool_ch_done = out->pool_valid & row_last & col_last;

    out->pool_mem_addr = m->col_cnt >> 1;
    out->prev_we       = out->pixel_valid & !out->col_odd;
    out->mem_we        = out->pixel_valid & out->col_odd & !out->row_odd;

    out->ch_err = in->ch_done & out->pixel_valid & !(row_last & col_last); // just for debug

    // ========== Next State Logic ==========
    m->row_cnt_next  = m->row_cnt;
    m->col_cnt_next  = m->col_cnt;
    m->pass_cnt_next = m->pass_cnt;

    if (out->pixel_valid)
    {
        if (col_last)
        {
            m->col_cnt_next = 0;
            m->row_cnt_next = row_last ? 0 : m->row_cnt + 1;
            if (row_last)
                m->pass_cnt_next = m->pass_cnt + 1;
        }
        else
        {
            m->col_cnt_next = m->col_cnt + 1;
        }
    }

    m->w_ch_err = out->ch_err;
}

/* always @(posedge clk) */
void pool_l1_ctrl_seq(pool_l1_ctrl_t *m)
{
    m->row_cnt  = m->row_cnt_next;
    m->col_cnt  = m->col_cnt_next;
    m->pass_cnt = m->pass_cnt_next;

    m->dbg_ch_err_cnt += m->w_ch_err;
}
