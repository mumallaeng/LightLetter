#ifndef DRV_BFSK_RX_H
#define DRV_BFSK_RX_H
#include <stdint.h>
typedef struct {
    uint32_t raw;
    uint8_t frame_id;
    uint8_t data;
    uint8_t received_crc;
    uint8_t error;
    uint8_t overrun;
} bfsk_packet_t;
int drv_bfsk_rx_init(void);
/* 1: packet copied, ACK started. 0: no packet or ACK pending.
 * -1: ACK timeout once; wait for ready to drop before accepting another.
 * Nonblocking, single caller. Initialize first; packet must be non-null. */
int drv_bfsk_rx_poll(bfsk_packet_t *packet);
#endif
