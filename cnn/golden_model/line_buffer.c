#include <string.h>
#include "line_buffer.h"

void line_buffer_reset(line_buffer_t *m)
{
    memset(m, 0, sizeof(*m));
}

/* always @(*) */
void line_buffer_comb(line_buffer_t *m, const line_buffer_in_t *in,
                      line_buffer_out_t *out)
{
    /* ---------------- assign ---------------- */
    out->win_valid = m->win_valid;
    memcpy(out->win, m->win, sizeof(m->win));

    /* ---------------- next state ---------------- */
    memcpy(m->win_next, m->win, sizeof(m->win));
    m->col_next       = m->col;
    m->row_next       = m->row;
    m->win_valid_next = 0;          /* 1clk pulse */
    m->mem_we         = 0;
    m->mem_waddr      = m->col;

    if (in->phase_clear)
    {
        m->col_next = 0;
        m->row_next = 0;
    }
    else if (in->pixel_valid)
    {
        for (int lane = 0; lane < L2_LANES; lane++)
        {
            act_t top = m->mem[lane][1][m->col];    /* row-2 */
            act_t mid = m->mem[lane][0][m->col];    /* row-1 */
            act_t bot = in->pixel_in[lane];         /* row   */

            /* window 왼쪽으로 shift, 오른쪽 열에 새 column */
            for (int ky = 0; ky < L2_K; ky++)
            {
                m->win_next[lane][ky][0] = m->win[lane][ky][1];
                m->win_next[lane][ky][1] = m->win[lane][ky][2];
            }
            m->win_next[lane][0][2] = top;
            m->win_next[lane][1][2] = mid;
            m->win_next[lane][2][2] = bot;

            m->mem_wdata[lane][0] = bot;
            m->mem_wdata[lane][1] = mid;
        }
        m->mem_we = 1;

        m->win_valid_next = (m->row >= L2_K - 1) && (m->col >= L2_K - 1);

        if (m->col == L2_IN_W - 1)
        {
            m->col_next = 0;
            m->row_next = (m->row == L2_IN_H - 1) ? 0 : m->row + 1;
        }
        else
        {
            m->col_next = m->col + 1;
        }
    }
}

/* always @(posedge clk) */
void line_buffer_seq(line_buffer_t *m)
{
    if (m->mem_we)
    {
        for (int lane = 0; lane < L2_LANES; lane++)
        {
            m->mem[lane][0][m->mem_waddr] = m->mem_wdata[lane][0];
            m->mem[lane][1][m->mem_waddr] = m->mem_wdata[lane][1];
        }
    }

    memcpy(m->win, m->win_next, sizeof(m->win));
    m->col       = m->col_next;
    m->row       = m->row_next;
    m->win_valid = m->win_valid_next;
}
