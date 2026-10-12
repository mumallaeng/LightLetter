#include "app_dashboard.h"
#include "../hal/hal_console.h"
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#define QUEUE_SIZE 128U
typedef struct { bfsk_packet_t packet; uint32_t sequence; } entry_t;
static entry_t queue[QUEUE_SIZE];
static unsigned head, count;
static uint32_t sequence, dropped, overruns, rx_timeouts, fft_timeouts;
static uint32_t frame_sequence, last_status;
static uint64_t spectrum[128];
static int spectrum_pending, status_dirty;
static char tx[2048];
static size_t length, sent;

void app_dashboard_init(int rx_status, int fft_status)
{
    head=count=0; sequence=dropped=overruns=rx_timeouts=fft_timeouts=0;
    frame_sequence=last_status=0; spectrum_pending=status_dirty=0; sent=0;
    length=(size_t)snprintf(tx,sizeof tx,
        "{\"type\":\"status\",\"state\":\"ready\",\"rx_init\":%d,\"fft_init\":%d,\"fft_period_ms\":500}\n",
        rx_status,fft_status);
}
void app_dashboard_packet(const bfsk_packet_t *packet)
{
    uint32_t seq=sequence++;
    if (packet->overrun) { ++overruns; status_dirty=1; }
    if (count == QUEUE_SIZE) { ++dropped; status_dirty=1; return; }
    queue[(head+count)%QUEUE_SIZE].packet=*packet;
    queue[(head+count)%QUEUE_SIZE].sequence=seq;
    ++count;
}
void app_dashboard_rx_timeout(void) { ++rx_timeouts; status_dirty=1; }
void app_dashboard_fft_timeout(void) { ++fft_timeouts; status_dirty=1; }
int app_dashboard_fft_room(void) { return !spectrum_pending && count < 32U; }
int app_dashboard_fft(const uint64_t *bins)
{
    if (!app_dashboard_fft_room()) return 0;
    memcpy(spectrum,bins,sizeof spectrum);
    spectrum_pending=1;
    return 1;
}
void app_dashboard_poll(uint32_t now)
{
    if (sent < length) {
        size_t remaining=length-sent;
        sent+=hal_console_try_write(tx+sent, remaining > 16U ? 16U : remaining);
        return;
    }
    sent=length=0;
    if (status_dirty && (uint32_t)(now-last_status) >= 500U) {
        length=(size_t)snprintf(tx,sizeof tx,
            "{\"type\":\"status\",\"rx_queue_dropped\":%" PRIu32 ",\"packet_overruns\":%" PRIu32
            ",\"rx_ack_timeouts\":%" PRIu32 ",\"fft_timeouts\":%" PRIu32 "}\n",
            dropped,overruns,rx_timeouts,fft_timeouts);
        status_dirty=0; last_status=now;
    } else if (count) {
        entry_t *e=&queue[head];
        bfsk_packet_t *p=&e->packet;
        length=(size_t)snprintf(tx,sizeof tx,
            "{\"type\":\"rx\",\"seq\":%" PRIu32 ",\"frame_id\":%u,\"data\":%u,\"received_crc\":%u,\"crc_ok\":%s,\"overrun\":%s}\n",
            e->sequence,(unsigned)p->frame_id,(unsigned)p->data,(unsigned)p->received_crc,
            p->error ? "false":"true",p->overrun ? "true":"false");
        head=(head+1U)%QUEUE_SIZE; --count;
    } else if (spectrum_pending) {
        unsigned i;
        int n=snprintf(tx,sizeof tx,"{\"type\":\"fft\",\"seq\":%" PRIu32 ",\"bins\":[",frame_sequence++);
        length=(size_t)n;
        for (i=0;i<128U;++i) {
            n=snprintf(tx+length,sizeof tx-length,"%s%" PRIu64,
                       i ? ",":"",spectrum[i]);
            if (n < 0 || (size_t)n >= sizeof tx-length) {
                length=0; spectrum_pending=0;
                app_dashboard_fft_timeout(); return;
            }
            length+=(size_t)n;
        }
        memcpy(tx+length,"]}\n",3); length+=3;
        spectrum_pending=0;
    }
}
