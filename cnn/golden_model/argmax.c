#include "argmax.h"

void argmax_init(argmax_t *m, uint8_t num_class)
{
    m->num_class = num_class;
    argmax_reset(m);
}

/* negedge rst_n */
void argmax_reset(argmax_t *m)
{
    m->idx_cnt      = 0;  m->idx_cnt_next  = 0;
    m->max_data_reg = 0;  m->max_data_next = 0;
    m->max_idx      = 0;  m->max_idx_next  = 0;
    m->cnn_done     = 0;  m->cnn_done_next = 0;
}

/* always @(*) */
void argmax_comb(argmax_t *m, const argmax_in_t *in, argmax_out_t *out)
{
    // Handshake
    uint8_t logit_ready  = 1;
    uint8_t argmax_valid = in->logit_valid && logit_ready;

    uint8_t first_logit = (m->idx_cnt == 0);
    uint8_t last_logit  = (m->idx_cnt == m->num_class - 1);

    // ----- Index count Logic -----
    m->idx_cnt_next = m->idx_cnt;
    if (argmax_valid)
        m->idx_cnt_next = last_logit ? 0 : (uint8_t)(m->idx_cnt + 1);

    // ----- Argmax Logic -----
    uint8_t diff_logic = (in->logit_data > m->max_data_reg); /* signed compare */

    m->max_data_next = m->max_data_reg;
    m->max_idx_next  = m->max_idx;
    if (argmax_valid)
    {
        if (first_logit)
        {
            m->max_data_next = in->logit_data;
            m->max_idx_next  = 0;
        }
        else if (diff_logic)
        {
            m->max_data_next = in->logit_data;
            m->max_idx_next  = m->idx_cnt;
        }
    }

    // ----- Output Logic -----
    m->cnn_done_next = argmax_valid && last_logit; /* 1 clk pulse */

    out->logit_ready = logit_ready;
    out->cnn_result  = m->max_idx;
    out->cnn_done    = m->cnn_done;
}

/* always @(posedge clk) */
void argmax_seq(argmax_t *m)
{
    m->idx_cnt      = m->idx_cnt_next;
    m->max_data_reg = m->max_data_next;
    m->max_idx      = m->max_idx_next;
    m->cnn_done     = m->cnn_done_next;
}

uint8_t argmax_ref(const int16_t *logit, uint8_t n)
{
    uint8_t best = 0;
    for (uint8_t i = 1; i < n; i++)
        if (logit[i] > logit[best])
            best = i;
    return best;
}
