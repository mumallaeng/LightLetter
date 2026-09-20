#include "lane_packer.h"

void lane_packer_init(lane_packer_t *m, uint8_t pack)
{
    m->pack = pack;
    lane_packer_reset(m);
}

void lane_packer_reset(lane_packer_t *m)
{
    m->cnt = 0;  m->cnt_next = 0;
    for (int i = 0; i < LP_MAX_PACK - 1; i++)
    {
        m->hold[i]      = 0;
        m->hold_next[i] = 0;
    }
}

/* always @(*) */
void lane_packer_comb(lane_packer_t *m, const lane_packer_in_t *in, lane_packer_out_t *out)
{
    uint8_t last = (m->cnt == m->pack - 1);

    m->cnt_next = m->cnt;
    for (int i = 0; i < LP_MAX_PACK - 1; i++)
        m->hold_next[i] = m->hold[i];

    /* held values in the low lanes, the incoming value in the top lane */
    uint64_t data = 0;
    for (int i = 0; i < m->pack - 1; i++)
        data |= (uint64_t)m->hold[i] << (16 * i);
    data |= (uint64_t)in->q_in   << (16 * (m->pack - 1));
    data |= (uint64_t)in->q_done << (16 * m->pack);

    out->pack_data  = data;
    out->pack_valid = in->q_valid && last;

    if (in->q_valid)
    {
        m->cnt_next = last ? 0 : m->cnt + 1;
        if (!last)
            m->hold_next[m->cnt] = in->q_in;
    }
}

/* always @(posedge clk) */
void lane_packer_seq(lane_packer_t *m)
{
    m->cnt = m->cnt_next;
    for (int i = 0; i < LP_MAX_PACK - 1; i++)
        m->hold[i] = m->hold_next[i];
}
