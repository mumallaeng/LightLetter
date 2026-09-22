#include <string.h>
#include "pool_l1_datapath.h"

static uint16_t max16(uint16_t a, uint16_t b)
{
    return a > b ? a : b;
}

void pool_l1_datapath_reset(pool_l1_datapath_t *m)
{
    memset(m, 0, sizeof(*m));
}

/* always @(*) */
void pool_l1_datapath_comb(pool_l1_datapath_t *m, const pool_l1_datapath_in_t *in,
                           pool_l1_datapath_out_t *out)
{
    for (int l = 0; l < POOL_L1_LANES; l++)
    {
        // ========== Max Logic ==========
        out->pair[l]      = max16(m->prev_reg[l], in->data[l]);
        out->mem_rd[l]    = m->pool_mem[l][in->pool_mem_addr];
        out->pool_data[l] = max16(out->mem_rd[l], out->pair[l]);

        // ========== Next State Logic ==========
        m->prev_reg_next[l] = in->prev_we ? in->data[l] : m->prev_reg[l];
        m->w_mem_wdata[l]   = out->pair[l];
    }

    m->w_mem_we   = in->mem_we;
    m->w_mem_addr = in->pool_mem_addr;
}

/* always @(posedge clk) */
void pool_l1_datapath_seq(pool_l1_datapath_t *m)
{
    for (int l = 0; l < POOL_L1_LANES; l++)
    {
        m->prev_reg[l] = m->prev_reg_next[l];
        if (m->w_mem_we)
            m->pool_mem[l][m->w_mem_addr] = m->w_mem_wdata[l];
    }
}
