/*
 * Argmax golden model (rtl/cnn/argmax.v): FC logits -> class index
 *
 *   logits arrive one per handshake, class 0 first. The first logit of an image is loaded
 *   unconditionally, later ones replace the max only if strictly greater, so a tie keeps the
 *   lower index (same as numpy / torch argmax). cnn_done is a 1-cycle pulse on the cycle
 *   after the NUM_CLASS-th logit is accepted; cnn_result is final on that cycle.
 */
#ifndef ARGMAX_H
#define ARGMAX_H

#include <stdint.h>

#define ARGMAX_MAX_CLASS 64 /* idx width = $clog2(NUM_CLASS) <= 6 */

/* input ports */
typedef struct
{
    int16_t logit_data; /* signed [15:0] */
    uint8_t logit_valid;
} argmax_in_t;

/* output ports */
typedef struct
{
    uint8_t logit_ready; /* always 1 */
    uint8_t cnn_result;  /* [$clog2(NUM_CLASS)-1:0] = max_idx */
    uint8_t cnn_done;    /* registered 1-cycle pulse */
} argmax_out_t;

typedef struct
{
    uint8_t num_class; /* parameter NUM_CLASS */

    /* registers: reg / reg_next */
    uint8_t idx_cnt,      idx_cnt_next;
    int16_t max_data_reg, max_data_next;
    uint8_t max_idx,      max_idx_next;
    uint8_t cnn_done,     cnn_done_next;
} argmax_t;

void argmax_init(argmax_t *m, uint8_t num_class);
void argmax_reset(argmax_t *m);
void argmax_comb(argmax_t *m, const argmax_in_t *in, argmax_out_t *out);
void argmax_seq(argmax_t *m);

/* behavioral reference: index of the first maximum of logit[0..n-1] */
uint8_t argmax_ref(const int16_t *logit, uint8_t n);

#endif
