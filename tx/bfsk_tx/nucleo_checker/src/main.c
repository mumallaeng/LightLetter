#include "drv/bfsk_rx.h"
#include "hal/nucleo_board.h"
#include <stdio.h>

int main(void)
{
    bfsk_rx rx;
    bfsk_rx_frame frame;
    uint32_t timestamp, last_report = 0, seen_losses = 0;
    uint32_t good = 0, bad = 0, sync_errors = 0;
    char line[200];
    board_init();
    bfsk_rx_init(&rx);
    board_write("\r\nNUCLEO-F411RE BFSK TX monitor\r\n"
                "Input: A0/PA0 TIM2_CH1; UART: 115200 8N1\r\n"
                "Expect: SYNC x4, D5 ID DATA CRC; connect GND\r\n");
    for (;;) {
        /* ISR은 timestamp만 적재한다. decode/printf는 main에서 수행한다. */
        if (board_capture_losses() != seen_losses) {
            seen_losses = board_capture_losses();
            board_capture_discard();
            bfsk_rx_init(&rx);
            board_write("CAPTURE LOSS: queue/overcapture; resync next frame\r\n");
        }
        if (board_capture_pop(&timestamp)) {
            bfsk_rx_event event = bfsk_rx_edge(&rx, timestamp, &frame);
            if (board_capture_losses() != seen_losses) continue;
            if (event == BFSK_RX_FRAME) {
                if (frame.sfd_ok && frame.crc_ok) {
                    ++good;
                    board_toggle_led();
                } else ++bad;
                (void)snprintf(line, sizeof(line),
                    "%s raw=%02X %02X %02X %02X ASCII='%c' CRC=%02X/%02X "
                    "Hz[0,1,S]=%lu,%lu,%lu\r\n",
                    (frame.sfd_ok && frame.crc_ok) ? "PASS" : "FAIL",
                    (unsigned)frame.raw[0], (unsigned)frame.raw[1],
                    (unsigned)frame.raw[2], (unsigned)frame.raw[3],
                    (frame.raw[2] >= 32 && frame.raw[2] <= 126) ? frame.raw[2] : '.',
                    (unsigned)frame.raw[3], (unsigned)frame.expected_crc,
                    (unsigned long)frame.hz[0], (unsigned long)frame.hz[1],
                    (unsigned long)frame.hz[2]);
                board_write(line);
            } else if (event != BFSK_RX_NONE) ++sync_errors;
        }
        if ((uint32_t)(board_millis() - last_report) >= 1000U) {
            last_report = board_millis();
            (void)snprintf(line, sizeof(line),
                "STATUS edges=%lu pass=%lu fail=%lu sync_errors=%lu capture_loss=%lu\r\n",
                (unsigned long)board_capture_edges(), (unsigned long)good,
                (unsigned long)bad, (unsigned long)sync_errors,
                (unsigned long)board_capture_losses());
            board_write(line);
        }
    }
}
