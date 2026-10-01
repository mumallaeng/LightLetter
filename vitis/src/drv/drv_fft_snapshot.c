#include "drv_fft_snapshot.h"
#include "../hal/hal_fft_gpio.h"
enum phase { IDLE, PREPARE, CLEAN, ARM, CAPTURE, READ_LOW, READ_HIGH,
             RELEASE, DONE, FAULT };
static enum phase state;
static uint32_t entered;
static unsigned bin;
static uint64_t bins[FFT_BIN_COUNT];
static void advance(enum phase next, uint32_t now) { state=next; entered=now; }
int drv_fft_snapshot_init(void)
{
    int s = hal_fft_gpio_init();
    state = s ? FAULT : IDLE;
    return s;
}
int drv_fft_snapshot_start(uint32_t now)
{
    if (state != IDLE) return 0;
    hal_fft_control(0);
    advance(PREPARE, now);
    return 1;
}
int drv_fft_snapshot_poll(uint32_t now)
{
    uint32_t status;
    if (state == DONE) return 1;
    if (state == IDLE || state == FAULT) return 0;
    if ((uint32_t)(now-entered) >= 2000U) {
        hal_fft_control(0);
        state=FAULT; // Do not reset the shared receiver on a UI failure.
        return -1;
    }
    status=hal_fft_status();
    switch (state) {
    case PREPARE:
        if (status & 4U) { hal_fft_control(2); advance(CLEAN,now); }
        else if (status & 1U) { hal_fft_control(1); advance(ARM,now); }
        break;
    case CLEAN:
        if (status & 1U) { hal_fft_control(0); advance(PREPARE,now); }
        break;
    case ARM:
        if (status & 6U) { hal_fft_control(0); advance(CAPTURE,now); }
        break;
    case CAPTURE:
        if (status & 4U) { bin=0; hal_fft_control(0); advance(READ_LOW,now); }
        break;
    case READ_LOW:
        if (!(status & 8U)) {
            hal_fft_control((bin << 8) | 4U);
            advance(READ_HIGH,now);
        }
        break;
    case READ_HIGH:
        if ((status & 8U) && ((status >> 8) & 127U) == bin) {
            bins[bin]=hal_fft_power();
            hal_fft_control(bin << 8);
            if (++bin == FFT_BIN_COUNT) {
                hal_fft_control(2);
                advance(RELEASE,now);
            } else {
                hal_fft_control(bin << 8);
                advance(READ_LOW,now);
            }
        }
        break;
    case RELEASE:
        if (status & 1U) { hal_fft_control(0); advance(DONE,now); return 1; }
        break;
    default: break;
    }
    return 0;
}
const uint64_t *drv_fft_snapshot_bins(void) { return bins; }
void drv_fft_snapshot_consume(void) { if (state == DONE) state=IDLE; }
