#include "drv_bfsk_rx.h"
#include "../hal/hal_gpio.h"
#include "../hal/hal_time.h"
#define READY_MASK UINT32_C(0x80000000)
#define ERROR_MASK UINT32_C(0x40000000)
#define OVERRUN_MASK UINT32_C(0x20000000)
static enum { RX_IDLE, RX_ACK, RX_FAULT } state;
static uint32_t ack_started;
int drv_bfsk_rx_init(void)
{
    state=RX_IDLE;
    return hal_gpio_init();
}
int drv_bfsk_rx_poll(bfsk_packet_t *packet)
{
    uint32_t word=hal_gpio_read();
    if (state != RX_IDLE) {
        if (!(word & READY_MASK)) {
            hal_gpio_set_clear(0); state=RX_IDLE;
        } else if (state == RX_ACK && (uint32_t)(hal_time_ms()-ack_started) >= 10U) {
            hal_gpio_set_clear(0); state=RX_FAULT;
            return -1;
        }
        return 0;
    }
    if (!(word & READY_MASK)) return 0;
    packet->raw=word;
    packet->frame_id=(uint8_t)(word >> 16);
    packet->data=(uint8_t)(word >> 8);
    packet->received_crc=(uint8_t)word;
    packet->error=(uint8_t)((word & ERROR_MASK) != 0U);
    packet->overrun=(uint8_t)((word & OVERRUN_MASK) != 0U);
    hal_gpio_set_clear(1);
    ack_started=hal_time_ms(); state=RX_ACK;
    return 1;
}
