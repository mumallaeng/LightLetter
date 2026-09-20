/* Lane Packer: gathers PACK quantized values into one FIFO entry */
#ifndef LANE_PACKER_H
#define LANE_PACKER_H

#include "ob_common.h"

#define LP_MAX_PACK 3

/* input ports */
typedef struct
{
    uint16_t q_in;
    uint8_t  q_valid;
    uint8_t  q_done; /* last-pixel flag */
} lane_packer_in_t;

/* output ports */
typedef struct
{
    uint64_t pack_data; /* {done, v[PACK-1], ..., v[0]}, first value in [15:0] */
    uint8_t  pack_valid;
} lane_packer_out_t;

typedef struct
{
    uint8_t pack; /* parameter PACK (1..3) */

    /* registers: reg / reg_next */
    uint8_t  cnt,                   cnt_next;
    uint16_t hold[LP_MAX_PACK - 1], hold_next[LP_MAX_PACK - 1];
} lane_packer_t;

void lane_packer_init(lane_packer_t *m, uint8_t pack);
void lane_packer_reset(lane_packer_t *m);
void lane_packer_comb(lane_packer_t *m, const lane_packer_in_t *in, lane_packer_out_t *out);
void lane_packer_seq(lane_packer_t *m);

#endif
