#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "drv/drv_bfsk_rx.h"
#include "drv/drv_fft_snapshot.h"
#include "app/app_dashboard.h"
static uint32_t now, packet_word, control, status_word;
static unsigned clear_high, hold_ack, gpio_delay, read_bin, captures, read_count;
static int freeze_fft, wrong_bin, blocked;
static char output[100000];
static size_t used;
uint32_t hal_time_ms(void) { return now; }
int hal_gpio_init(void) { clear_high=0; return 0; }
uint32_t hal_gpio_read(void) {
    if(clear_high && !hold_ack) packet_word=0;
    return packet_word;
}
void hal_gpio_set_clear(int v) { clear_high=(unsigned)v; }
int hal_fft_gpio_init(void) { control=0; status_word=1; gpio_delay=0; return 0; }
void hal_fft_control(uint32_t v) {
    unsigned rising=v & ~control;
    control=v;
    if (!(v & 4)) status_word &= ~8U;
    if ((rising & 2) && (status_word & 4)) { status_word=1; return; }
    if ((rising & 1) && (status_word & 1)) {
        ++captures; status_word=2; gpio_delay=4;
    }
    if ((rising & 4) && (status_word & 4)) {
        read_bin=(v >> 8)&127; gpio_delay=3; ++read_count;
    }
}
uint32_t hal_fft_status(void) {
    if(!freeze_fft && gpio_delay && --gpio_delay==0) {
        if(status_word & 2) status_word=4;
        else status_word=4|8|((wrong_bin ? ((read_bin+1)&127):read_bin)<<8);
    }
    return status_word;
}
uint64_t hal_fft_power(void) {
    assert((status_word & 12)==12 && (control & 4));
    return UINT64_C(0xffffffffff)-read_bin;
}
size_t hal_console_try_write(const char *p,size_t count) {
    assert(count<=16);
    if(blocked)return 0;
    if(count>3)count=3; // Partial FIFO acceptance.
    assert(used+count<sizeof output);
    memcpy(output+used,p,count);used+=count;output[used]=0;
    return count;
}
static void drain(unsigned n) { while(n--) { ++now;app_dashboard_poll(now); } }
int main(void) {
    bfsk_packet_t packet;
    unsigned i;
    uint32_t start;
    uint64_t bins[128];
    assert(drv_bfsk_rx_init()==0);
    packet_word=0x80014877U;
    assert(drv_bfsk_rx_poll(&packet)==1 && packet.data=='H' && clear_high);
    assert(drv_bfsk_rx_poll(&packet)==0 && !clear_high);
    hold_ack=1;packet_word=0x80024577U;now=UINT32_MAX-5U;
    assert(drv_bfsk_rx_poll(&packet)==1);
    now=6;
    assert(drv_bfsk_rx_poll(&packet)==-1 && !clear_high);
    assert(drv_bfsk_rx_poll(&packet)==0); // No duplicate on stuck ready.
    packet_word=0;hold_ack=0;assert(drv_bfsk_rx_poll(&packet)==0);

    assert(drv_fft_snapshot_init()==0);
    status_word=4|8; // Stale frozen snapshot from a previous PS run.
    now=UINT32_MAX-20U;assert(drv_fft_snapshot_start(now));
    for(i=0;i<1500;++i) { if(drv_fft_snapshot_poll(now++)==1)break; }
    assert(i<1500 && captures==1 && read_count==128);
    memcpy(bins,drv_fft_snapshot_bins(),sizeof bins);
    for(i=0;i<128;++i)assert(bins[i]==UINT64_C(0xffffffffff)-i);
    assert(!drv_fft_snapshot_start(now));
    drv_fft_snapshot_consume();
    assert(drv_fft_snapshot_start(now));
    freeze_fft=1;
    start=now;for(i=0;i<2100;++i) { if(drv_fft_snapshot_poll(now++)<0)break; }
    assert((uint32_t)(now-start)>=2000 && control==0);
    assert(!drv_fft_snapshot_start(now)); // Fault disables only FFT.

    freeze_fft=0;wrong_bin=1;assert(!drv_fft_snapshot_init());
    assert(drv_fft_snapshot_start(now));
    for(i=0;i<2500;++i) { if(drv_fft_snapshot_poll(now++)<0)break; }
    assert(i<2500); // A wrong echoed bin must never be published.

    app_dashboard_init(0,0);drain(200);
    assert(app_dashboard_fft(bins));
    app_dashboard_poll(++now); // Begin FFT JSON, then receive packets mid-line.
    blocked=1;
    for(i=0;i<17;++i) {
        packet.frame_id=(uint8_t)i;packet.data=(uint8_t)"HELLO BFSK WORLD\n"[i];
        // Exactly 17 bytes: explicit CRLF variant is tested separately in JS.
        if(i==16)packet.data='\n';
        packet.error=packet.overrun=0;packet.received_crc=0x55;
        app_dashboard_packet(&packet);app_dashboard_poll(++now);
    }
    blocked=0;drain(10000);
    // Exercise finite queue overflow; it must be visible, not memory corruption.
    blocked=1;
    for(i=0;i<140;++i)app_dashboard_packet(&packet);
    app_dashboard_rx_timeout();app_dashboard_fft_timeout();
    drain(100);blocked=0;drain(20000);
    puts(output);
    fprintf(stderr,"PASS: capture/ACK timeouts, rollover, 40-bit bins, queue/partial UART\n");
    return 0;
}
