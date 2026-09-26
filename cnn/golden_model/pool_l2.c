#include <string.h>
#include "pool_l2.h"

/* ================= datapath (max_logic + pool_buf) ================= */

void pool_l2_datapath_reset(pool_l2_datapath_t *m)
{
    memset(m, 0, sizeof(*m));   /* RTL: prev_reg <= 0, pool_buf 는 reset 없음 (읽기 전에 항상 쓰인다) */
}

/* always @(*) */
void pool_l2_datapath_comb(pool_l2_datapath_t *m, const pool_l2_datapath_in_t *in,
                           pool_l2_datapath_out_t *out)
{
    // ========== Max Logic ==========
    out->pair       = (in->pool_in >= m->prev_reg) ? in->pool_in : m->prev_reg;
    out->rd_oob     = in->pool_addr >= POOL_L2_MEM_SIZE;
    out->pool_rdata = out->rd_oob ? 0 : m->ram[in->pool_addr];
    out->pool_data  = (out->pool_rdata >= out->pair) ? out->pool_rdata : out->pair;

    // ========== Next State Logic ==========
    m->prev_next    = in->prev_we ? in->pool_in : m->prev_reg;
    m->w_pool_we    = in->pool_we;
    m->w_pool_addr  = in->pool_addr;
    m->w_pool_wdata = out->pair;
}

/* always @(posedge clk) */
void pool_l2_datapath_seq(pool_l2_datapath_t *m)
{
    m->prev_reg = m->prev_next;
    if (m->w_pool_we && m->w_pool_addr < POOL_L2_MEM_SIZE)
        m->ram[m->w_pool_addr] = m->w_pool_wdata;
}

/* ================= top ================= */

void pool_l2_init(pool_l2_t *m)
{
    memset(m, 0, sizeof(*m));
    pool_l1_ctrl_init(&m->ctrl, POOL_L2_IN_H, POOL_L2_IN_W);
    pool_l2_datapath_reset(&m->dp);
}

void pool_l2_reset(pool_l2_t *m)
{
    pool_l1_ctrl_reset(&m->ctrl);
    pool_l2_datapath_reset(&m->dp);
    m->dbg_oob_rd   = 0;
    m->dbg_oob_used = 0;
}

/* 배선: ctrl 이 만든 pool_addr / prev_we / pool_we 를 datapath 가 쓴다 (RTL pool_l2.v) */
void pool_l2_comb(pool_l2_t *m, const pool_l2_in_t *in, pool_l2_out_t *out)
{
    pool_l1_ctrl_in_t     ctrl_i;
    pool_l2_datapath_in_t dp_i;

    /* Controller (pool_ctrl_l2) */
    ctrl_i.out_valid  = in->out_valid;
    ctrl_i.ch_done    = in->ch_done;
    ctrl_i.pool_ready = in->pool_ready;
    pool_l1_ctrl_comb(&m->ctrl, &ctrl_i, &m->ctrl_o);

    /* Datapath (pool_datapath_l2) */
    dp_i.prev_we   = m->ctrl_o.prev_we;
    dp_i.pool_in   = in->out_data;
    dp_i.pool_addr = m->ctrl_o.pool_mem_addr;
    dp_i.pool_we   = m->ctrl_o.mem_we;
    pool_l2_datapath_comb(&m->dp, &dp_i, &m->dp_o);

    /* top outputs */
    out->out_ready    = m->ctrl_o.out_ready;
    out->pool_valid   = m->ctrl_o.pool_valid;
    out->pool_ch_done = m->ctrl_o.pool_ch_done;
    out->pool_data    = m->dp_o.pool_data;

    /* debug: 범위 밖 읽기는 받은 픽셀 (pixel_valid) 기준으로 센다 */
    m->w_oob_rd   = m->ctrl_o.pixel_valid & m->dp_o.rd_oob;
    m->w_oob_used = m->ctrl_o.pool_valid & m->dp_o.rd_oob;
}

void pool_l2_seq(pool_l2_t *m)
{
    pool_l2_datapath_seq(&m->dp);
    pool_l1_ctrl_seq(&m->ctrl);
    m->dbg_oob_rd   += m->w_oob_rd;
    m->dbg_oob_used += m->w_oob_used;
}
