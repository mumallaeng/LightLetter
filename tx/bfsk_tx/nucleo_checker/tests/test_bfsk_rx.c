#include "bfsk_rx.h"
#include <assert.h>
#include <stdio.h>

static bfsk_rx rx;
static bfsk_rx_frame last;
static unsigned frames, errors;

static void edge(uint32_t time)
{
    bfsk_rx_event event = bfsk_rx_edge(&rx, time, &last);
    if (event == BFSK_RX_FRAME) ++frames;
    else if (event != BFSK_RX_NONE) ++errors;
}

/* RTL 모델: 매 symbol carrier_count=0, 첫 상승은 반주기 뒤,
 * symbol 160000 FPGA clocks + handshake gap(8 clocks). MSB first.
 * scale: 10000=nominal, 10400=receiver clock +4%; jitter: 0/1 us.
 */
static void waveform(uint32_t origin, uint8_t sfd, uint8_t id, uint8_t data,
                     uint8_t crc, unsigned scale, unsigned jitter,
                     unsigned skip_us, int bad_symbol, unsigned symbols)
{
    const uint8_t raw[4] = {sfd, id, data, crc};
    unsigned sym, serial = 0;
    uint64_t start = 0;
    for (sym = 0; sym < symbols; ++sym) {
        unsigned period = 4000;
        uint64_t t;
        if (sym >= 4) {
            const unsigned bit = sym - 4;
            period = (raw[bit / 8] & (0x80U >> (bit % 8))) ? 5000 : 10000;
        }
        if ((int)sym == bad_symbol) period = 4000;
        for (t = start + period / 2; t < start + 160000; t += period) {
            uint32_t us = (uint32_t)((t * scale + 500000U) / 1000000U);
            int noise = jitter ? (int)(serial++ % 3U) - 1 : 0;
            if (us >= skip_us) edge(origin + us + (uint32_t)noise);
        }
        start += 160008;
    }
}

static void reset(void)
{
    bfsk_rx_init(&rx);
    frames = errors = 0;
}

int main(void)
{
    unsigned data;
    assert(bfsk_rx_crc(0, 0x41) == 0xC0);
    reset();
    waveform(0, 0xD5, 0, 0x41, 0xC0, 10000, 0, 0, -1, 36);
    assert(frames == 1 && errors == 0 && last.sfd_ok && last.crc_ok);
    assert(last.raw[2] == 0x41 && last.raw[1] == 0);
    assert(last.hz[0] > 9900 && last.hz[0] < 10100);
    assert(last.hz[1] > 19800 && last.hz[1] < 20200);
    assert(last.hz[2] > 24700 && last.hz[2] < 25300);

    for (data = 0; data < 256; ++data) {
        reset();
        waveform(1234, 0xD5, (uint8_t)(255 - data), (uint8_t)data,
                 bfsk_rx_crc((uint8_t)(255 - data), (uint8_t)data),
                 10000, 1, 0, -1, 36);
        assert(frames == 1 && errors == 0 && last.sfd_ok && last.crc_ok);
        assert(last.raw[2] == data && last.raw[1] == 255 - data);
    }
    reset();
    waveform(UINT32_MAX - 1000, 0xD5, 0, 0x41, 0xC0, 10400, 1, 0, -1, 36);
    assert(frames == 1 && last.crc_ok && last.sfd_ok);
    reset();
    waveform(0, 0xD5, 0, 0x41, 0xC0, 9600, 1, 0, -1, 36);
    assert(frames == 1 && last.crc_ok && last.sfd_ok);
    reset();
    waveform(0, 0xD5, 0, 0x41, 0xC1, 10000, 0, 0, -1, 36);
    assert(frames == 1 && !last.crc_ok && last.sfd_ok);
    reset();
    waveform(0, 0xD4, 0, 0x41, 0xC0, 10000, 0, 0, -1, 36);
    assert(frames == 1 && last.crc_ok && !last.sfd_ok);
    reset();
    waveform(0, 0xD5, 0, 0x41, 0xC0, 10000, 0, 0, 12, 36);
    assert(frames == 0 && errors > 0);
    waveform(1000000, 0xD5, 1, 0x41, bfsk_rx_crc(1, 0x41), 10000, 0, 0, -1, 36);
    assert(frames == 1 && last.crc_ok && last.raw[1] == 1);
    reset();
    waveform(0, 0xD5, 0, 0x41, 0xC0, 10000, 0, 2000, -1, 36);
    assert(frames == 0);
    waveform(1000000, 0xD5, 0, 0x41, 0xC0, 10000, 0, 0, -1, 36);
    assert(frames == 1 && last.crc_ok);
    reset();
    waveform(0, 0xD5, 0, 0x41, 0xC0, 10000, 0, 0, -1, 20);
    waveform(1000000, 0xD5, 0, 0x41, 0xC0, 10000, 0, 0, -1, 36);
    assert(frames == 1 && errors > 0 && last.crc_ok);
    /* 송신 간 1초 idle이 없는 연속 프레임도 재동기화한다. */
    waveform(1057610, 0xD5, 0, 0x41, 0xC0, 10000, 0, 0, -1, 36);
    assert(frames == 2 && last.crc_ok);
    puts("PASS: 256 payloads, CRC/SFD rejection, jitter, +/-4% clock scale,");
    puts("      uint32 timer wrap, malformed/truncated frames, late join, back-to-back recovery.");
    return 0;
}
