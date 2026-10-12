/* Output Buffer golden model: common definitions */
#ifndef OB_COMMON_H
#define OB_COMMON_H

#include <stdint.h>

#define OB_CH_W   36 /* ch_result width */
#define OB_ACC_W  40 /* accumulate path width */
#define OB_BIAS_W 32

typedef int64_t ob_ch_t;  /* signed [OB_CH_W-1:0] */
typedef int64_t ob_acc_t; /* signed [OB_ACC_W-1:0] */

#ifndef OB_MAX_C_OUT /* Fully Connected reuses this path with up to 120 neurons */
#define OB_MAX_C_OUT     16
#endif
#define OB_MAX_BUF_DEPTH (121 * 16) /* conv2: N x C_OUT */

/* parameter */
typedef struct
{
    uint8_t  layer;      /* 1 = conv1, 2 = conv2 */
    uint16_t n;          /* output pixels */
    uint8_t  c_out;      /* output channels */
    uint8_t  num_groups; /* input-channel groups = ceil(C_IN / 3) */
    uint8_t  acc_w;      /* accumulate path width, RTL parameter ACC_W (0 = OB_ACC_W) */
} ob_param_t;

static const ob_param_t OB_PARAM_CONV1 = {1, 676, 6, 1, OB_ACC_W};
static const ob_param_t OB_PARAM_CONV2 = {2, 121, 16, 2, OB_ACC_W};

/* wrap to signed [w-1:0], same as RTL width truncation */
static inline int64_t ob_sext(int64_t v, int w)
{
    uint64_t mask = (w >= 64) ? ~0ULL : ((1ULL << w) - 1ULL);
    uint64_t u = (uint64_t)v & mask;
    if ((u >> (w - 1)) & 1ULL)
        u |= ~mask;
    return (int64_t)u;
}

/* debug: does v fit in signed [w-1:0]? */
static inline int ob_fits(int64_t v, int w)
{
    return ob_sext(v, w) == v;
}

#endif
