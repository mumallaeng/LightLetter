#include "bfsk_rx.h"
#include <string.h>

void bfsk_rx_init(bfsk_rx *rx)
{
    memset(rx, 0, sizeof(*rx));
}

uint8_t bfsk_rx_crc(uint8_t frame_id, uint8_t data)
{
    uint8_t crc = 0;
    const uint8_t bytes[2] = {frame_id, data};
    unsigned i, bit;
    for (i = 0; i < 2; ++i) {
        crc ^= bytes[i];
        for (bit = 0; bit < 8; ++bit)
            crc = (uint8_t)((crc << 1) ^ ((crc & 0x80U) ? 0x07U : 0U));
    }
    return crc;
}

static int classify(uint32_t period)
{
    if (period >= 92U && period <= 108U) return 0;
    if (period >= 45U && period <= 55U) return 1;
    if (period >= 36U && period <= 44U) return 2;
    return -1;
}

bfsk_rx_event bfsk_rx_edge(bfsk_rx *rx, uint32_t timestamp_us,
                            bfsk_rx_frame *frame)
{
    uint32_t period, previous, midpoint, elapsed, begin, end;
    unsigned winner, total, i;
    int type;
    if (!rx->have_previous) {
        rx->previous = timestamp_us;
        rx->have_previous = 1;
        return BFSK_RX_NONE;
    }
    previous = rx->previous;
    period = timestamp_us - previous;
    rx->previous = timestamp_us;
    if (period > 300U || period == 0U) {
        const int was_collecting = rx->collecting;
        bfsk_rx_init(rx);
        rx->previous = timestamp_us;
        rx->have_previous = 1;
        return was_collecting ? BFSK_RX_GAP_ERROR : BFSK_RX_NONE;
    }
    type = classify(period);

    if (!rx->collecting) {
        if (type != 2) {
            rx->sync_count = 0;
            rx->sync_sum = 0;
            return BFSK_RX_NONE;
        }
        if (rx->sync_count == 0U)
            rx->sync_first = previous;
        rx->sync_sum += period;
        ++rx->sync_count;
        /* 약 5.8 ms의 연속 SYNC가 필요하다. 중간 참여 시 다음 프레임을 기다린다.
         * 평균 SYNC 주기로 symbol 길이도 추정하여 HSI 오차 누적을 줄인다.
         */
        if (rx->sync_count < 145U) return BFSK_RX_NONE;
        rx->symbol_q8 = (rx->sync_sum * 40U * 256U) / rx->sync_count;
        rx->anchor = rx->sync_first - rx->sync_sum / (2U * rx->sync_count);
        rx->collecting = 1;
        rx->bit_index = 0;
        memset(rx->raw, 0, sizeof(rx->raw));
        memset(rx->votes, 0, sizeof(rx->votes));
        memset(rx->period_sum, 0, sizeof(rx->period_sum));
        memset(rx->period_count, 0, sizeof(rx->period_count));
        rx->period_sum[2] = rx->sync_sum;
        rx->period_count[2] = rx->sync_count;
        return BFSK_RX_NONE;
    }

    if (type >= 0) {
        rx->period_sum[type] += period;
        ++rx->period_count[type];
    }
    /* 전환 경계의 혼합 주기를 피하기 위해 심볼 중앙 25~75%만 투표한다. */
    midpoint = timestamp_us - period / 2U;
    elapsed = midpoint - rx->anchor;
    begin = ((4U + rx->bit_index) * rx->symbol_q8 + rx->symbol_q8 / 4U) >> 8;
    end = ((4U + rx->bit_index) * rx->symbol_q8 + rx->symbol_q8 * 3U / 4U) >> 8;
    if (elapsed >= end) {
        total = rx->votes[0] + rx->votes[1] + rx->votes[2];
        winner = rx->votes[1] > rx->votes[0] ? 1U : 0U;
        if (rx->votes[winner] < 4U || rx->votes[winner] * 4U < total * 3U) {
            bfsk_rx_init(rx);
            return BFSK_RX_SYMBOL_ERROR;
        }
        rx->raw[rx->bit_index / 8U] =
            (uint8_t)((rx->raw[rx->bit_index / 8U] << 1) | winner);
        ++rx->bit_index;
        memset(rx->votes, 0, sizeof(rx->votes));
        if (rx->bit_index == 32U) {
            memcpy(frame->raw, rx->raw, sizeof(frame->raw));
            frame->expected_crc = bfsk_rx_crc(rx->raw[1], rx->raw[2]);
            frame->sfd_ok = (uint8_t)(rx->raw[0] == 0xD5U);
            frame->crc_ok = (uint8_t)(rx->raw[3] == frame->expected_crc);
            for (i = 0; i < 3; ++i)
                frame->hz[i] = rx->period_sum[i] ?
                    (1000000U * rx->period_count[i]) / rx->period_sum[i] : 0U;
            bfsk_rx_init(rx);
            return BFSK_RX_FRAME;
        }
        begin = ((4U + rx->bit_index) * rx->symbol_q8 + rx->symbol_q8 / 4U) >> 8;
        end = ((4U + rx->bit_index) * rx->symbol_q8 + rx->symbol_q8 * 3U / 4U) >> 8;
    }
    if (elapsed >= begin && elapsed < end)
        ++rx->votes[(type == 0 || type == 1) ? type : 2];
    return BFSK_RX_NONE;
}
