/*
 * conv2 Line Buffer: line_buffer_array (3개 병렬, 13 x 13, pass 당 3ch)
 *   pixel_valid 가 3개 인스턴스에 broadcast 되므로 win_valid[0..2] 는 항상 같다.
 */
#include <stdio.h>
#include <stdlib.h>

#define IMG_WIDTH 13
#include "line_buffer_array.h"
#include "ce_top.h"

static uint8_t l2_win_valid(const void *inst)
{
    return ((const line_buffer_array_t *)inst)->win_valid[0];
}

static void l2_win_out(const void *inst, int16_t win[CE_LANES][CE_KK])
{
    memcpy(win, ((const line_buffer_array_t *)inst)->win_out, sizeof(int16_t) * CE_LANES * CE_KK);
}

static void l2_step(void *inst, const act_t pixel_in[CE_LANES], uint8_t pixel_valid,
                    uint8_t phase_clear)
{
    line_buffer_array_step((line_buffer_array_t *)inst, pixel_in[0], pixel_in[1], pixel_in[2],
                           pixel_valid, phase_clear);
}

ce_lb_port_t ce_lb_l2_new(int in_w)
{
    ce_lb_port_t port = {0};

    if (in_w != IMG_WIDTH)
    {
        printf("ce_lb_l2: built for IMG_WIDTH %d, layer needs %d\n", IMG_WIDTH, in_w);
        return port;
    }

    line_buffer_array_t *arr = malloc(sizeof(*arr));
    if (!arr)
        return port;
    line_buffer_array_reset(arr);

    port.inst      = arr;
    port.win_valid = l2_win_valid;
    port.win_out   = l2_win_out;
    port.step      = l2_step;
    return port;
}
