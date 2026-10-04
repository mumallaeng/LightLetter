#ifndef APP_DASHBOARD_H
#define APP_DASHBOARD_H
#include <stdint.h>
#include "../drv/drv_bfsk_rx.h"
void app_dashboard_init(int rx_status, int fft_status);
void app_dashboard_packet(const bfsk_packet_t *packet);
void app_dashboard_rx_timeout(void);
void app_dashboard_fft_timeout(void);
int app_dashboard_fft_room(void);
int app_dashboard_fft(const uint64_t *bins);
void app_dashboard_poll(uint32_t now);
#endif
