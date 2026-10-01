#include <string.h>
#include "fc_staging.h"

void fc_staging_init(fc_staging_t *m, const fc_param_t *p)
{
    m->p = *p;
    fc_staging_reset(m);
}

void fc_staging_reset(fc_staging_t *m)
{
    memset(m->buf, 0, sizeof(m->buf));
    memset(m->buf_next, 0, sizeof(m->buf_next));

    m->full[0]    = 0;  m->full_next[0]    = 0;
    m->full[1]    = 0;  m->full_next[1]    = 0;
    m->fill_sel   = 0;  m->fill_sel_next   = 0;
    m->calc_sel   = 0;  m->calc_sel_next   = 0;
    m->fill_cnt   = 0;  m->fill_cnt_next   = 0;
    m->fill_chunk = 0;  m->fill_chunk_next = 0;

    m->w_dbg_drop   = 0;
    m->dbg_drop_cnt = 0;
}

/* always @(*) */
void fc_staging_comb(fc_staging_t *m, const fc_staging_in_t *in, fc_staging_out_t *out)
{
    // ========== Next State / Counter Logic ==========
    memcpy(m->buf_next, m->buf, sizeof(m->buf));
    m->full_next[0]    = m->full[0];
    m->full_next[1]    = m->full[1];
    m->fill_sel_next   = m->fill_sel;
    m->calc_sel_next   = m->calc_sel;
    m->fill_cnt_next   = m->fill_cnt;
    m->fill_chunk_next = m->fill_chunk;

    uint8_t len      = fc_chunk_len(&m->p, m->fill_chunk);
    uint8_t in_ready = !m->full[m->fill_sel];
    uint8_t take     = in->in_valid && in_ready;

    if (take)
    {
        m->buf_next[m->fill_sel][m->fill_cnt] = in->in_data;

        if (m->fill_cnt + 1 == len)
        {
            /* zero the lanes past the end of a short chunk */
            for (uint8_t i = len; i < m->p.lanes; i++)
                m->buf_next[m->fill_sel][i] = 0;

            m->full_next[m->fill_sel] = 1;
            m->fill_sel_next          = (uint8_t)(m->fill_sel ^ 1);
            m->fill_cnt_next          = 0;
            m->fill_chunk_next        = (uint8_t)((m->fill_chunk + 1 == m->p.num_chunk)
                                                  ? 0 : m->fill_chunk + 1);
        }
        else
        {
            m->fill_cnt_next = (uint8_t)(m->fill_cnt + 1);
        }
    }

    if (in->chunk_done)
    {
        m->full_next[m->calc_sel] = 0;
        m->calc_sel_next          = (uint8_t)(m->calc_sel ^ 1);
    }

    // ========== Output Logic ==========
    out->in_ready  = in_ready;
    out->calc_full = m->full[m->calc_sel];
    out->next_full = m->full[m->calc_sel ^ 1];
    memcpy(out->x, m->buf[m->calc_sel], sizeof(out->x));

    /* backpressure is normal (ready = 0); nothing is dropped as long as the sender holds
       the value, so this counts only lanes written past the end of a chunk */
    m->w_dbg_drop = 0;
}

/* always @(posedge clk) */
void fc_staging_seq(fc_staging_t *m)
{
    memcpy(m->buf, m->buf_next, sizeof(m->buf));
    m->full[0]    = m->full_next[0];
    m->full[1]    = m->full_next[1];
    m->fill_sel   = m->fill_sel_next;
    m->calc_sel   = m->calc_sel_next;
    m->fill_cnt   = m->fill_cnt_next;
    m->fill_chunk = m->fill_chunk_next;

    m->dbg_drop_cnt += m->w_dbg_drop;
}
