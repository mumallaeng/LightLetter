/*
 * conv1 Line Buffer: line_buffer 1개 (28 x 28 x 1ch)
 *   MAC lane 1, 2 에는 0 window 를 준다 (weight 도 0, NUM_ACTIVE_CH = 1)
 */
#include <stdio.h>
#include <stdlib.h>

#define IMG_WIDTH 28
#include "line_buffer.h"
#include "ce_top.h"

static uint8_t l1_win_valid(const void *inst)
{
    return ((const line_buffer_t *)inst)->win_valid;
}

static void l1_win_out(const void *inst, int16_t win[CE_LANES][CE_KK])
{
    memset(win, 0, sizeof(int16_t) * CE_LANES * CE_KK);
    memcpy(win[0], ((const line_buffer_t *)inst)->win_out, sizeof(win[0]));
}

static void l1_step(void *inst, const act_t pixel_in[CE_LANES], uint8_t pixel_valid,
                    uint8_t phase_clear)
{
    line_buffer_step((line_buffer_t *)inst, pixel_in[0], pixel_valid, phase_clear);
}

ce_lb_port_t ce_lb_l1_new(int in_w)
{
    ce_lb_port_t port = {0};

    if (in_w != IMG_WIDTH)
    {
        printf("ce_lb_l1: built for IMG_WIDTH %d, layer needs %d\n", IMG_WIDTH, in_w);
        return port;
    }

    line_buffer_t *lb = malloc(sizeof(*lb));
    if (!lb)
        return port;
    line_buffer_reset(lb);

    port.inst      = lb;
    port.win_valid = l1_win_valid;
    port.win_out   = l1_win_out;
    port.step      = l1_step;
    return port;
}
