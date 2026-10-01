#ifndef DRV_FFT_SNAPSHOT_H
#define DRV_FFT_SNAPSHOT_H
#include <stdint.h>
#define FFT_BIN_COUNT 128U
int drv_fft_snapshot_init(void);
int drv_fft_snapshot_start(uint32_t now);
/* One bounded handshake step; 1=ready, 0=pending/idle, -1=new fault. */
int drv_fft_snapshot_poll(uint32_t now);
const uint64_t *drv_fft_snapshot_bins(void);
void drv_fft_snapshot_consume(void);
#endif
