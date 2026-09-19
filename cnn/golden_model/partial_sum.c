#include "partial_sum.h"

/* always @(*) */
void partial_sum_comb(const partial_sum_in_t *in, partial_sum_out_t *out)
{
    int64_t raw_full = ob_sext(in->ch_result0, OB_CH_W) +
                       ob_sext(in->ch_result1, OB_CH_W) +
                       ob_sext(in->ch_result2, OB_CH_W);
    ob_acc_t raw_sum = ob_sext(raw_full, OB_ACC_W);

    /* first group starts from bias, later groups add to the stored value */
    int64_t sum_full;
    if (in->first_phase)
        sum_full = raw_sum + (ob_acc_t)in->bias_rdata;
    else
        sum_full = in->buf_rdata + raw_sum;

    out->sum = ob_sext(sum_full, OB_ACC_W);
    out->we  = in->mac_valid;

    out->sum_data  = in->last_phase ? out->sum : 0;
    out->sum_valid = in->last_phase && in->mac_valid;

    out->dbg_ch_ovf  = !ob_fits(in->ch_result0, OB_CH_W) ||
                       !ob_fits(in->ch_result1, OB_CH_W) ||
                       !ob_fits(in->ch_result2, OB_CH_W);
    out->dbg_acc_ovf = !ob_fits(raw_full, OB_ACC_W) || !ob_fits(sum_full, OB_ACC_W);
}
