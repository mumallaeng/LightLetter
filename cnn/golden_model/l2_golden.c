#include <string.h>
#include "l2_golden.h"

const char *total_state_name(total_state_t s)
{
    static const char *name[] = {
        "IDLE", "CH02_IMG_IN", "WAIT_MAC_02", "WAIT_LB_RST",
        "CH35_IMG_IN", "WAIT_MAC_35", "STOP"
    };
    return name[s];
}

const char *wac_state_name(wac_state_t s)
{
    return s == W_IDLE ? "IDLE" : "WEIGHT_CAL";
}

void l2_core_reset(l2_core_t *c,
                   const wgt_t weight[L2_OUT_CH][L2_IN_CH][L2_K][L2_K])
{
    memset(c, 0, sizeof(*c));

    for (int oc = 0; oc < L2_OUT_CH; oc++)
    {
        for (int grp = 0; grp < L2_NUM_PASSES; grp++)
        {
            for (int lane = 0; lane < L2_LANES; lane++)
            {
                int ic = grp * L2_LANES + lane;

                for (int ky = 0; ky < L2_K; ky++)
                {
                    for (int kx = 0; kx < L2_K; kx++)
                    {
                        c->rom_data[oc][grp][lane * 9 + ky * 3 + kx] =
                            weight[oc][ic][ky][kx];
                    }
                }
            }
        }
    }
}

/* ================================================================
 * Combinational logic (assign 문)
 * ================================================================ */

/* out_ready = (IDLE | CH02_IMG_IN | CH35_IMG_IN) & ~win_valid */
static uint8_t comb_out_ready(const l2_core_t *c)
{
    total_state_t s = c->tfsm.state;
    uint8_t accept_state =
        (s == T_IDLE) || (s == T_CH02_IMG_IN) || (s == T_CH35_IMG_IN);

#if CFG_READY_BLOCK_ON_CLEAR
    accept_state = accept_state && !c->tfsm.phase_clear;
#endif

    return accept_state && !c->lb.win_valid;
}

/* Weight ROM 출력 + out_ch_sel MUX. valid/och/pass 는 MAC 정렬용 태그. */
static void comb_weight_src(const l2_core_t *c, weight_bus_t *w)
{
    uint8_t och = c->wac.out_ch_sel;

    w->valid = (c->wac.state == W_WEIGHT_CAL);
    w->och   = och;
    w->pass  = c->rom.grp_q;
    memcpy(w->weight, c->rom_data[och][c->rom.grp_q], sizeof(w->weight));
}

static psum_t comb_mac27(const l2_core_t *c, const weight_bus_t *w)
{
    psum_t acc = 0;

    for (int lane = 0; lane < L2_LANES; lane++)
    {
        for (int ky = 0; ky < L2_K; ky++)
        {
            for (int kx = 0; kx < L2_K; kx++)
            {
                acc += (psum_t)((int32_t)c->lb.win[lane][ky][kx] *
                                (int32_t)w->weight[lane * 9 + ky * 3 + kx]);
            }
        }
    }

    return acc;
}

void l2_core_comb(const l2_core_t *c, const l2_in_t *in, l2_out_t *out)
{
    out->out_ready   = comb_out_ready(c);
    out->pixel_valid = in->out_valid && out->out_ready;

#if CFG_CH35_EN_LATCH
    out->rom_grp_in = c->ch3_5_en_q;
#else
    out->rom_grp_in = in->ch3_5_en;
#endif

#if CFG_WEIGHT_PIPE == 0
    comb_weight_src(c, &out->mac_in);
#else
    out->mac_in = c->wpipe[CFG_WEIGHT_PIPE - 1];
#endif

    out->psum = c->macp[CFG_MAC_PIPE - 1];
}

/* ================================================================
 * Sequential logic (always @(posedge clk))
 * 모든 next 값은 현재 레지스터(c)와 조합 신호(o)로만 계산한다.
 * ================================================================ */

static void seq_total_fsm(const l2_core_t *c, const l2_in_t *in,
                          const l2_out_t *o, total_fsm_t *n)
{
    const total_fsm_t *t = &c->tfsm;

    switch (t->state)
    {
    case T_IDLE:
        n->mac_start   = 0;
        n->phase_clear = 0;
        if (in->out_valid)
            n->state = T_CH02_IMG_IN;
        break;

    case T_CH02_IMG_IN:
        if (c->lb.win_valid)
        {
            n->mac_start = 1;
            n->state     = T_WAIT_MAC_02;
        }
        break;

    case T_WAIT_MAC_02:
        n->mac_start = 0;
        if (c->wac.mac_done)
        {
            if (t->ch_count == 1)
            {
                n->phase_clear = 1;
                n->state       = T_WAIT_LB_RST;
            }
            else
            {
                n->state = T_CH02_IMG_IN;
            }
        }
        break;

    case T_WAIT_LB_RST:
        n->phase_clear = 0;
        n->state       = T_CH35_IMG_IN;
        break;

    case T_CH35_IMG_IN:
        if (c->lb.win_valid)
        {
            n->mac_start = 1;
            n->state     = T_WAIT_MAC_35;
        }
        break;

    case T_WAIT_MAC_35:
        n->mac_start = 0;
        if (c->wac.mac_done)
            n->state = (t->ch_count == 2) ? T_STOP : T_CH35_IMG_IN;
        break;

    case T_STOP:
        n->phase_clear = 1;
        n->state       = T_IDLE;
        break;
    }

    if (t->state == T_STOP)
        n->ch_count = 0;
    else if (in->ch_done && o->pixel_valid)
        n->ch_count = t->ch_count + 1;
}

static void seq_weight_addr_ctrl(const l2_core_t *c, weight_addr_ctrl_t *n)
{
    const weight_addr_ctrl_t *w = &c->wac;

    switch (w->state)
    {
    case W_IDLE:
        n->out_ch_sel = 0;
        n->mac_done   = 0;
        if (c->tfsm.mac_start)
            n->state = W_WEIGHT_CAL;
        break;

    case W_WEIGHT_CAL:
        if (w->out_ch_sel == L2_OUT_CH - 1)
        {
            n->mac_done = 1;
            n->state    = W_IDLE;
        }
        else
        {
            n->out_ch_sel = w->out_ch_sel + 1;
        }
        break;
    }
}

/* phase_clear 가 pixel_valid 보다 우선 (카운터/valid 만 리셋, 메모리 유지) */
static void seq_line_buffer(const l2_core_t *c, const l2_in_t *in,
                            const l2_out_t *o, line_buffer_t *n)
{
    const line_buffer_t *lb = &c->lb;

    if (c->tfsm.phase_clear)
    {
        n->col       = 0;
        n->row       = 0;
        n->win_valid = 0;
        return;
    }

    if (!o->pixel_valid)
    {
        n->win_valid = 0;
        return;
    }

    uint16_t col = lb->col;

    for (int lane = 0; lane < L2_LANES; lane++)
    {
        act_t   top = lb->line[lane][1][col];   /* row-2 */
        act_t   mid = lb->line[lane][0][col];   /* row-1 */
        act_t   bot = in->pixel_in[lane];       /* row   */

        for (int ky = 0; ky < L2_K; ky++)
        {
            n->win[lane][ky][0] = lb->win[lane][ky][1];
            n->win[lane][ky][1] = lb->win[lane][ky][2];
        }
        n->win[lane][0][2] = top;
        n->win[lane][1][2] = mid;
        n->win[lane][2][2] = bot;

        n->line[lane][1][col] = mid;
        n->line[lane][0][col] = bot;
    }

    n->win_valid = (lb->row >= L2_K - 1) && (lb->col >= L2_K - 1);

    if (lb->col == L2_IN_W - 1)
    {
        n->col = 0;
        n->row = (lb->row == L2_IN_H - 1) ? 0 : lb->row + 1;
    }
    else
    {
        n->col = lb->col + 1;
    }
}

static void seq_datapath(const l2_core_t *c, const l2_in_t *in,
                         const l2_out_t *o, l2_core_t *n)
{
    /* ch3_5_en latch (CFG_CH35_EN_LATCH=1 일 때만 ROM 에 연결됨) */
    if (o->pixel_valid)
        n->ch3_5_en_q = in->ch3_5_en;

    /* Weight ROM sync read: 주소 레지스터 */
    n->rom.grp_q = o->rom_grp_in;

    /* MUX 출력 파이프라인 */
#if CFG_WEIGHT_PIPE > 0
    comb_weight_src(c, &n->wpipe[0]);
    for (int i = 1; i < CFG_WEIGHT_PIPE; i++)
        n->wpipe[i] = c->wpipe[i - 1];
#endif

    /* MAC Array: 입력 단에서 현재 window 를 샘플링 */
    n->macp[0].valid = o->mac_in.valid;
    n->macp[0].och   = o->mac_in.och;
    n->macp[0].pass  = o->mac_in.pass;
    n->macp[0].data  = o->mac_in.valid ? comb_mac27(c, &o->mac_in) : 0;
    for (int i = 1; i < CFG_MAC_PIPE; i++)
        n->macp[i] = c->macp[i - 1];
}

void l2_core_step(l2_core_t *c, const l2_in_t *in)
{
    l2_out_t  o;
    l2_core_t n = *c;

    l2_core_comb(c, in, &o);

    seq_total_fsm(c, in, &o, &n.tfsm);
    seq_weight_addr_ctrl(c, &n.wac);
    seq_line_buffer(c, in, &o, &n.lb);
    seq_datapath(c, in, &o, &n);

    *c = n;
}
