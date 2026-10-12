#include "fc_mac.h"
#include <string.h>

void fc_mac_init(fc_mac_t *m)
{
    fc_mac_reset(m);
    m->dbg_acc_ovf_cnt = 0;
}

void fc_mac_reset(fc_mac_t *m)
{
    memset(&m->s1, 0, sizeof m->s1);  memset(&m->s1_next, 0, sizeof m->s1_next);
    memset(&m->s2, 0, sizeof m->s2);  memset(&m->s2_next, 0, sizeof m->s2_next);
    memset(&m->s3, 0, sizeof m->s3);  memset(&m->s3_next, 0, sizeof m->s3_next);
    memset(m->acc, 0, sizeof m->acc); memset(m->acc_next, 0, sizeof m->acc_next);
    m->w_dbg_ovf = 0;
}

static int fits_acc_w(fc_acc_t v)
{
    const fc_acc_t lim = (fc_acc_t)1 << (FC_ACC_W - 1);
    return v >= -lim && v < lim;
}

/* always @(*) */
void fc_mac_comb(fc_mac_t *m, const fc_mac_in_t *in, fc_mac_out_t *out)
{
    // ========== stage 1 next: operand registers ==========
    m->s1_next.feature = in->feature;
    memcpy(m->s1_next.weight, in->weight, sizeof m->s1_next.weight);
    m->s1_next.first = in->first && in->mac_en;
    m->s1_next.mac   = in->mac_en;
    m->s1_next.last  = in->last && in->mac_en;
    m->s1_next.layer = in->layer;
    m->s1_next.group = in->group;
    m->s1_next.valid = in->mac_en;

    // ========== stage 2 next: products ==========
    for (int l = 0; l < FC_P; l++)
        m->s2_next.prod[l] = (int32_t)m->s1.feature * (int32_t)m->s1.weight[l];
    memcpy(m->s2_next.bias, in->bias, sizeof m->s2_next.bias);   /* C register: bias is read in the operand stage */
    m->s2_next.first = m->s1.first;
    m->s2_next.mac   = m->s1.mac;
    m->s2_next.last  = m->s1.last;
    m->s2_next.layer = m->s1.layer;
    m->s2_next.group = m->s1.group;
    m->s2_next.valid = m->s1.valid;

    // ========== stage 3: acc <= bias | acc + product ==========
    m->w_dbg_ovf = 0;
    for (int l = 0; l < FC_P; l++)
    {
        fc_acc_t sum = m->acc[l];
        if (m->s2.valid && m->s2.mac)
            sum = (m->s2.first ? (fc_acc_t)m->s2.bias[l] : m->acc[l]) + m->s2.prod[l];
        m->acc_next[l] = sum;
        if (m->s2.valid && m->s2.mac && !fits_acc_w(sum))
            m->w_dbg_ovf = 1;
    }
    m->s3_next.valid = m->s2.valid && m->s2.mac && m->s2.last;
    m->s3_next.layer = m->s2.layer;
    m->s3_next.group = m->s2.group;

    // ========== Output Logic: the sums are the accumulator registers ==========
    for (int l = 0; l < FC_P; l++)
        out->sum[l] = m->acc[l];
    out->sum_valid = m->s3.valid;
    out->layer     = m->s3.layer;
    out->group     = m->s3.group;
    out->busy      = m->s1.valid || m->s2.valid || m->s3.valid;
}

/* always @(posedge clk) */
void fc_mac_seq(fc_mac_t *m)
{
    m->s1 = m->s1_next;
    m->s2 = m->s2_next;
    m->s3 = m->s3_next;
    memcpy(m->acc, m->acc_next, sizeof m->acc);
    if (m->w_dbg_ovf)
        m->dbg_acc_ovf_cnt++;
}
