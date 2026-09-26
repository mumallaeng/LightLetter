#include <string.h>
#include "cnn_top.h"

void cnn_top_init(cnn_top_t *m, const int16_t *w1, const int32_t *b1, uint8_t s1,
                  const int16_t *w2, const int32_t *b2, uint8_t s2,
                  const fc_param_t fc_p[3], const int16_t *const fc_rom[3], const int32_t *const fc_bias[3])
{
    memset(m, 0, sizeof(*m));
    chain_l1_init(w1, b1, s1);
    pool_l1_init(&m->pool1, POOL_L1_IN_H, POOL_L1_IN_W, POOL_L1_LANES);
    chain_l2_init(w2, b2, s2);
    pool_l2_init(&m->pool2);
    fc_top_init(&m->fc, fc_p, fc_rom, fc_bias);
    argmax_init(&m->am, CNN_TOP_N_LOGIT);
}

/* 배선: 평가 순서 ①~⑩ 은 cnn_top.h 참고 */
void cnn_top_comb(cnn_top_t *m, const cnn_top_in_t *in, cnn_top_out_t *out)
{
    /* ① conv_l2 : in_ready, out_valid / out_data / out_ch_done (레지스터 출력) */
    memset(&m->l2_i, 0, sizeof(m->l2_i));
    chain_l2_comb(&m->l2_i, &m->l2_o);

    /* ② conv_l1 : out_valid / out_data / out_ch_done (레지스터 출력) */
    m->l1_i.in_valid  = in->in_valid;
    m->l1_i.pixel_in  = in->pixel_in;
    m->l1_i.ch_done   = in->ch_done;
    m->l1_i.out_ready = 0;
    chain_l1_comb(&m->l1_i, &m->l1_o);

    /* ③ pool_l1 : conv_l1 -> pool_l1, conv_l2 in_ready -> pool_ready */
    m->p1_i.out_valid  = m->l1_o.out_valid;
    m->p1_i.ch_done    = m->l1_o.out_ch_done;
    m->p1_i.pool_ready = m->l2_o.in_ready;
    for (int j = 0; j < POOL_L1_LANES; j++)
        m->p1_i.out_data[j] = m->l1_o.out_data[j];
    pool_l1_comb(&m->pool1, &m->p1_i, &m->p1_o);

    /* ④ conv_l1 : pool_l1 out_ready -> conv_l1 out_ready (pop 확정) */
    m->l1_i.out_ready = m->p1_o.out_ready;
    chain_l1_comb(&m->l1_i, &m->l1_o);

    /* ⑤ argmax : logit_ready (상수 1) */
    m->am_i.logit_data  = 0;
    m->am_i.logit_valid = 0;
    argmax_comb(&m->am, &m->am_i, &m->am_o);

    /* ⑥ fc_top : fc_in_ready (staging full 레지스터) */
    m->fc_i.fc_in_data  = 0;
    m->fc_i.fc_in_valid = 0;
    m->fc_i.logit_ready = m->am_o.logit_ready;
    fc_top_comb(&m->fc, &m->fc_i, &m->fc_o);

    /* ⑦ pool_l2 : conv_l2 -> pool_l2, fc_top fc_in_ready -> pool_ready */
    m->p2_i.out_valid  = m->l2_o.out_valid;
    m->p2_i.out_data   = m->l2_o.out_data;
    m->p2_i.ch_done    = m->l2_o.out_ch_done;
    m->p2_i.pool_ready = m->fc_o.fc_in_ready;
    pool_l2_comb(&m->pool2, &m->p2_i, &m->p2_o);

    /* ⑧ conv_l2 : pool_l1 -> conv_l2 입력, pool_l2 out_ready -> conv_l2 out_ready (push / pop 확정) */
    m->l2_i.in_valid  = m->p1_o.pool_valid;
    m->l2_i.ch_done   = m->p1_o.pool_ch_done;
    m->l2_i.out_ready = m->p2_o.out_ready;
    for (int j = 0; j < POOL_L1_LANES; j++)
        m->l2_i.pixel_in[j] = (int16_t)m->p1_o.pool_data[j];
    chain_l2_comb(&m->l2_i, &m->l2_o);

    /* ⑨ fc_top : pool_l2 -> fc_top (확정) */
    m->fc_i.fc_in_data  = m->p2_o.pool_data;
    m->fc_i.fc_in_valid = m->p2_o.pool_valid;
    m->fc_i.logit_ready = m->am_o.logit_ready;
    fc_top_comb(&m->fc, &m->fc_i, &m->fc_o);

    /* ⑩ argmax : fc_top logit -> argmax (확정) */
    m->am_i.logit_data  = m->fc_o.logit_data;
    m->am_i.logit_valid = m->fc_o.logit_valid;
    argmax_comb(&m->am, &m->am_i, &m->am_o);

    /* 경계 전달 (모니터용) */
    m->tx_in    = in->in_valid & m->l1_o.in_ready;
    m->tx_l1    = m->l1_o.out_valid & m->p1_o.out_ready;
    m->tx_p1    = m->p1_o.pool_valid & m->l2_o.in_ready;
    m->tx_l2    = m->l2_o.out_valid & m->p2_o.out_ready;
    m->tx_p2    = m->p2_o.pool_valid & m->fc_o.fc_in_ready;
    m->tx_fc1   = m->fc.w_l1_fire;
    m->tx_fc2   = m->fc.w_l2_fire;
    m->tx_logit = m->fc_o.logit_valid & m->am_o.logit_ready;

    /* top outputs */
    out->in_ready   = m->l1_o.in_ready;
    out->cnn_result = m->am_o.cnn_result;
    out->cnn_done   = m->am_o.cnn_done;
}

void cnn_top_seq(cnn_top_t *m)
{
    chain_l1_seq();
    pool_l1_seq(&m->pool1);
    chain_l2_seq();
    pool_l2_seq(&m->pool2);
    fc_top_seq(&m->fc);
    argmax_seq(&m->am);
}

int cnn_top_l1_idle(void)
{
    return chain_l1_idle();
}

int cnn_top_idle(void)
{
    return chain_l1_idle() && chain_l2_idle();
}
