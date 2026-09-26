/* Partial Sum: 3-channel sum + cross-group accumulate + bias (combinational) */
#ifndef PARTIAL_SUM_H
#define PARTIAL_SUM_H

#include "ob_common.h"

/* input ports */
typedef struct
{
    ob_ch_t  ch_result0;
    ob_ch_t  ch_result1;
    ob_ch_t  ch_result2;
    uint8_t  mac_valid;
    uint8_t  first_phase; /* group_cnt == 0 */
    uint8_t  last_phase;  /* group_cnt == NUM_GROUPS-1 */
    ob_acc_t buf_rdata;
    int32_t  bias_rdata;
    uint8_t  acc_w;       /* RTL parameter ACC_W of this instance (0 = OB_ACC_W) */
} partial_sum_in_t;

/* output ports */
typedef struct
{
    ob_acc_t sum; /* -> Buffer Controller */
    uint8_t  we;
    ob_acc_t sum_data; /* final value */
    uint8_t  sum_valid;

    /* debug: width overflow flags */
    uint8_t dbg_ch_ovf;
    uint8_t dbg_acc_ovf;
} partial_sum_out_t;

void partial_sum_comb(const partial_sum_in_t *in, partial_sum_out_t *out);

#endif
