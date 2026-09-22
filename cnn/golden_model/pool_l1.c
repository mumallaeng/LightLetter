#include "pool_l1.h"

void pool_l1_reset(pool_l1_t *m)
{
    pool_l1_ctrl_reset(&m->ctrl);
    pool_l1_datapath_reset(&m->dp);
}

/* 배선: ctrl 이 만든 addr / we 를 datapath 가 쓴다 */
void pool_l1_comb(pool_l1_t *m, const pool_l1_in_t *in, pool_l1_out_t *out)
{
    pool_l1_ctrl_in_t     ctrl_i;
    pool_l1_datapath_in_t dp_i;

    /* Controller */
    ctrl_i.out_valid  = in->out_valid;
    ctrl_i.ch_done    = in->ch_done;
    ctrl_i.pool_ready = in->pool_ready;
    pool_l1_ctrl_comb(&m->ctrl, &ctrl_i, &m->ctrl_o);

    /* Datapath */
    for (int l = 0; l < POOL_L1_LANES; l++)
        dp_i.data[l] = in->out_data[l];
    dp_i.pool_mem_addr = m->ctrl_o.pool_mem_addr;
    dp_i.prev_we       = m->ctrl_o.prev_we;
    dp_i.mem_we        = m->ctrl_o.mem_we;
    pool_l1_datapath_comb(&m->dp, &dp_i, &m->dp_o);

    /* top outputs */
    out->out_ready    = m->ctrl_o.out_ready;
    out->pool_valid   = m->ctrl_o.pool_valid;
    out->pool_ch_done = m->ctrl_o.pool_ch_done;
    for (int l = 0; l < POOL_L1_LANES; l++)
        out->pool_data[l] = m->dp_o.pool_data[l];
}

void pool_l1_seq(pool_l1_t *m)
{
    pool_l1_datapath_seq(&m->dp);
    pool_l1_ctrl_seq(&m->ctrl);
}
