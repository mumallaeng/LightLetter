/*
 * Output Buffer (top) : mac_array -> Output Buffer -> ReLU & Quantization
 * Order: input-channel group (outer) -> pixel -> output channel (inner)
 */
#ifndef OUTPUT_BUFFER_H
#define OUTPUT_BUFFER_H

#include "ob_common.h"
#include "partial_sum.h"
#include "buffer_ctrl.h"
#include "bias_rom.h"

typedef enum
{
    OB_IDLE = 0,
    OB_ACCUM_G0, /* first group: store (conv1 outputs here) */
    OB_ACCUM_G1  /* second group: accumulate and output */
} ob_state_t;

/* input ports */
typedef struct
{
    ob_ch_t ch_result0;
    ob_ch_t ch_result1; /* 0 in conv1 */
    ob_ch_t ch_result2; /* 0 in conv1 */
    uint8_t mac_valid;
} output_buffer_in_t;

/* output ports */
typedef struct
{
    uint8_t  ch3_5_en; /* processing the second group */
    ob_acc_t sum_data; /* registered: one clock after the mac_valid that completes it */
    uint8_t  sum_valid;
} output_buffer_out_t;

typedef struct
{
    ob_param_t p;

    /* registers: reg / reg_next */
    ob_state_t state,      state_next;
    uint8_t    out_ch_cnt, out_ch_cnt_next;
    uint16_t   pixel_cnt,  pixel_cnt_next;
    uint8_t    group_cnt,  group_cnt_next;
    uint16_t   buf_addr,   buf_addr_next;   /* = pixel_cnt * C_OUT + out_ch_cnt */

    /* output registers: cut the accumulate path from the ReLU / quantizer path */
    ob_acc_t   sum_data,   sum_data_next;
    uint8_t    sum_valid,  sum_valid_next;

    /* submodule instances */
    bias_rom_t    u_bias_rom;
    buffer_ctrl_t u_buffer_ctrl; /* only if NUM_GROUPS > 1 */

    /* debug: width overflow counts (must stay 0) */
    uint8_t  w_dbg_ch_ovf, w_dbg_acc_ovf;
    uint32_t dbg_ch_ovf_cnt;
    uint32_t dbg_acc_ovf_cnt;
} output_buffer_t;

void output_buffer_init(output_buffer_t *m, const ob_param_t *p,
                        const int32_t *bias, uint8_t bias_depth);
void output_buffer_reset(output_buffer_t *m);
void output_buffer_comb(output_buffer_t *m, const output_buffer_in_t *in,
                        output_buffer_out_t *out);
void output_buffer_seq(output_buffer_t *m);

const char *ob_state_name(ob_state_t s);

#endif
