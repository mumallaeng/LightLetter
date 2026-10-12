#include "hal/hal_board.h"
#include "hal/hal_time.h"
#include "drv/drv_bfsk_rx.h"
#include "drv/drv_fft_snapshot.h"
#include "app/app_dashboard.h"
int main(void)
{
    bfsk_packet_t packet;
    uint32_t last_capture, now;
    int rx_status, fft_status, result;
    hal_board_init();
    rx_status=drv_bfsk_rx_init();
    fft_status=drv_fft_snapshot_init();
    app_dashboard_init(rx_status,fft_status);
    last_capture=hal_time_ms()-500U;
    for (;;) {
        now=hal_time_ms();
        if (!rx_status) {
            result=drv_bfsk_rx_poll(&packet);
            if (result > 0) app_dashboard_packet(&packet);
            else if (result < 0) app_dashboard_rx_timeout();
        }
        if (!fft_status) {
            result=drv_fft_snapshot_poll(now);
            if (result > 0 && app_dashboard_fft(drv_fft_snapshot_bins()))
                drv_fft_snapshot_consume();
            else if (result < 0) app_dashboard_fft_timeout();
            if ((uint32_t)(now-last_capture) >= 500U && app_dashboard_fft_room()) {
                if (drv_fft_snapshot_start(now)) last_capture=now;
            }
        }
        app_dashboard_poll(now);
    }
}
