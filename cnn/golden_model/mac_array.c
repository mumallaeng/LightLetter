#include <string.h>
#include "mac_array.h"

void mac_array_reset(mac_array_t *m)
{
    memset(m, 0, sizeof(*m));
}

static psum_t mac27(const act_t win[L2_LANES][L2_K][L2_K], const wgt_t *weight)
{
    psum_t acc = 0;

    for (int lane = 0; lane < L2_LANES; lane++)
        for (int ky = 0; ky < L2_K; ky++)
            for (int kx = 0; kx < L2_K; kx++)
                acc += (psum_t)((int32_t)win[lane][ky][kx] *
                                (int32_t)weight[lane * 9 + ky * 3 + kx]);
    return acc;
}

/* always @(*) */
void mac_array_comb(mac_array_t *m, const mac_array_in_t *in,
                    mac_array_out_t *out)
{
    weight_bus_t w_in;
    weight_bus_t mac_in;

    w_in.valid = in->valid;
    w_in.och   = in->och;
    w_in.pass  = in->pass;
    memcpy(w_in.weight, in->weight, sizeof(w_in.weight));

    /* ---------------- assign ---------------- */
#if CFG_WEIGHT_PIPE == 0
    mac_in = w_in;
#else
    mac_in = m->wpipe[CFG_WEIGHT_PIPE - 1];
#endif
    out->psum = m->macp[CFG_MAC_PIPE - 1];

    /* ---------------- next state ---------------- */
#if CFG_WEIGHT_PIPE > 0
    m->wpipe_next[0] = w_in;
    for (int i = 1; i < CFG_WEIGHT_PIPE; i++)
        m->wpipe_next[i] = m->wpipe[i - 1];
#endif

    m->macp_next[0].valid = mac_in.valid;
    m->macp_next[0].och   = mac_in.och;
    m->macp_next[0].pass  = mac_in.pass;
    m->macp_next[0].data  = mac_in.valid ? mac27(in->win, mac_in.weight) : 0;
    for (int i = 1; i < CFG_MAC_PIPE; i++)
        m->macp_next[i] = m->macp[i - 1];
}

/* always @(posedge clk) */
void mac_array_seq(mac_array_t *m)
{
    for (int i = 0; i < CFG_WEIGHT_PIPE; i++)
        m->wpipe[i] = m->wpipe_next[i];

    for (int i = 0; i < CFG_MAC_PIPE; i++)
        m->macp[i] = m->macp_next[i];
}

int mac_array_busy(const mac_array_t *m)
{
    int busy = 0;

    for (int i = 0; i < CFG_WEIGHT_PIPE; i++)
        busy |= m->wpipe[i].valid;
    for (int i = 0; i < CFG_MAC_PIPE; i++)
        busy |= m->macp[i].valid;
    return busy;
}
