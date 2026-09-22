#ifndef BFSK_RX_H
#define BFSK_RX_H
#include <stdint.h>

/* FPGA 디지털 출력의 상승 에지 시각(1 MHz, uint32 wrap)을 입력한다.
 * SYNC 25 kHz x 4, 1.6 ms symbols, MSB first, D5 ID DATA CRC.
 * ADC/FFT/광 아날로그 수신기가 아닌 TX 검사용 디코더이다.
 */
typedef struct {
    uint8_t raw[4];
    uint8_t expected_crc;
    uint8_t sfd_ok;
    uint8_t crc_ok;
    uint32_t hz[3]; /* BIT0, BIT1, SYNC: 관측 주파수; 관측 없으면 0 */
} bfsk_rx_frame;

typedef enum {
    BFSK_RX_NONE = 0,
    BFSK_RX_FRAME = 1,
    BFSK_RX_SYMBOL_ERROR = 2,
    BFSK_RX_GAP_ERROR = 3
} bfsk_rx_event;

typedef struct {
    uint32_t previous, sync_first, anchor, symbol_q8;
    uint32_t sync_sum, period_sum[3], period_count[3];
    uint16_t sync_count, votes[3];
    uint8_t have_previous, collecting, bit_index, raw[4];
} bfsk_rx;

void bfsk_rx_init(bfsk_rx *rx);
bfsk_rx_event bfsk_rx_edge(bfsk_rx *rx, uint32_t timestamp_us,
                            bfsk_rx_frame *frame);
uint8_t bfsk_rx_crc(uint8_t frame_id, uint8_t data);
#endif
