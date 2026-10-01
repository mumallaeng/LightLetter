/* Host-only MMIO/timer simulation. Does not validate AXI bus or optical output. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "bfsk_tx.h"
#include "bfsk_tx_hal.h"
#include "xparameters.h"
#include "xtime_l.h"

#define BFSK_TX_BASEADDR XPAR_OPTICAL_TX_AXI_TOP_0_BASEADDR

static uint32_t statuses[32], data_reg;
static size_t status_count, status_index;
static unsigned writes, starts;
static int corrupt_readback;
static XTime clock_ticks;
static uint8_t payload[32];

uint32_t Xil_In32(uintptr_t address)
{
    if (address == BFSK_TX_BASEADDR + BFSK_TX_DATA_OFFSET)
        return data_reg ^ (corrupt_readback ? 1U : 0U);
    assert(address == BFSK_TX_BASEADDR + BFSK_TX_STATUS_OFFSET);
    assert(status_count != 0U);
    if (status_index + 1U < status_count)
        return statuses[status_index++];
    return statuses[status_index];
}

void Xil_Out32(uintptr_t address, uint32_t value)
{
    ++writes;
    if (address == BFSK_TX_BASEADDR + BFSK_TX_DATA_OFFSET) {
        assert(writes == starts * 2U + 1U);
        data_reg = value;
    } else {
        assert(address == BFSK_TX_BASEADDR + BFSK_TX_CTRL_OFFSET);
        assert(value == BFSK_TX_START);
        assert(writes == starts * 2U + 2U);
        assert(starts < sizeof(payload));
        payload[starts++] = (uint8_t)data_reg;
    }
}

void XTime_GetTime(XTime *time)
{
    *time = clock_ticks;
    clock_ticks += COUNTS_PER_SECOND / 100000U; /* about 10 us per read */
}

static bfsk_tx setup(const uint32_t *sequence, size_t count)
{
    bfsk_tx tx;
    assert(count <= sizeof(statuses) / sizeof(statuses[0]));
    memcpy(statuses, sequence, count * sizeof(*sequence));
    status_count = count;
    status_index = 0U;
    writes = starts = 0U;
    clock_ticks = 0U;
    corrupt_readback = 0;
    assert(bfsk_tx_init(&tx, BFSK_TX_BASEADDR, 100U) == BFSK_TX_OK);
    return tx;
}

int main(void)
{
    const uint32_t good[] = {2, 1, 1, 2, 2, 0, 1};
    const uint32_t idle[] = {1};
    const uint32_t both[] = {3};
    const uint32_t stuck[] = {1, 2};
    const uint32_t buffer_ok[] = {1, 2, 1, 1, 2, 1, 1, 2, 1};
    const uint32_t partial[] = {1, 2, 1, 1, 2};
    const uint8_t bytes[] = {0x00, 0x80, 0xFF};
    bfsk_tx tx = setup(good, 7);
    size_t sent = 99;
    uint32_t status;

    assert(bfsk_tx_init_default(&tx) == BFSK_TX_OK);
    assert(tx.base_address == BFSK_TX_BASEADDR);
    assert(tx.timeout_us == BFSK_TX_DEFAULT_TIMEOUT_US);
    assert(writes == 0 && status_index == 0 && clock_ticks == 0);
    assert(bfsk_tx_init_default(NULL) == BFSK_TX_ERR_ARGUMENT);
    assert(bfsk_tx_hal_ticks_from_us(0) == 0);
    assert(bfsk_tx_hal_ticks_from_us(1) == 334);
    assert(bfsk_tx_hal_ticks_from_us(UINT32_MAX) ==
           ((uint64_t)COUNTS_PER_SECOND * UINT32_MAX + 999999U) / 1000000U);
    tx = setup(good, 7);

    assert(bfsk_tx_send_byte(&tx, 0x41) == BFSK_TX_OK);
    assert(starts == 1 && writes == 2 && payload[0] == 0x41);
    assert(tx.last_status == BFSK_TX_READY);

    tx = setup(both, 1);
    assert(bfsk_tx_send_byte(&tx, 0x41) == BFSK_TX_ERR_READY_TIMEOUT);
    assert(writes == 0); /* READY=1 alone must not allow a busy transmitter */
    tx = setup(idle, 1);
    assert(bfsk_tx_send_byte(&tx, 0x41) == BFSK_TX_ERR_START_TIMEOUT);
    assert(starts == 1); /* no automatic retry */
    tx = setup(stuck, 2);
    assert(bfsk_tx_send_byte(&tx, 0x41) == BFSK_TX_ERR_DONE_TIMEOUT);
    assert(starts == 1 && tx.last_status == BFSK_TX_BUSY);
    tx = setup(idle, 1);
    corrupt_readback = 1;
    assert(bfsk_tx_send_byte(&tx, 0x41) == BFSK_TX_ERR_DATA_READBACK);
    assert(starts == 0 && writes == 1);

    tx = setup(buffer_ok, 9);
    assert(bfsk_tx_send_buffer(&tx, bytes, sizeof(bytes), &sent) == BFSK_TX_OK);
    assert(sent == 3 && starts == 3 && memcmp(payload, bytes, 3) == 0);
    tx = setup(partial, 5);
    assert(bfsk_tx_send_buffer(&tx, bytes, sizeof(bytes), &sent) == BFSK_TX_ERR_DONE_TIMEOUT);
    assert(sent == 1 && starts == 2);
    tx = setup(idle, 1);
    assert(bfsk_tx_send_buffer(&tx, NULL, 0, &sent) == BFSK_TX_OK);
    assert(sent == 0 && writes == 0);
    assert(bfsk_tx_send_buffer(&tx, NULL, 1, &sent) == BFSK_TX_ERR_ARGUMENT);
    assert(bfsk_tx_send_byte(NULL, 0) == BFSK_TX_ERR_ARGUMENT);
    assert(bfsk_tx_get_status(&tx, NULL) == BFSK_TX_ERR_ARGUMENT);
    assert(bfsk_tx_get_status(&tx, &status) == BFSK_TX_OK && status == 1);
    assert(bfsk_tx_init(NULL, BFSK_TX_BASEADDR, 100) == BFSK_TX_ERR_ARGUMENT);
    assert(bfsk_tx_init(&tx, BFSK_TX_BASEADDR + 1, 100) == BFSK_TX_ERR_ARGUMENT);
    assert(bfsk_tx_send_byte(&tx, 0) == BFSK_TX_ERR_ARGUMENT);
    assert(bfsk_tx_init(&tx, BFSK_TX_BASEADDR, 0) == BFSK_TX_ERR_ARGUMENT);

    tx = setup(good, 7);
    clock_ticks = UINT64_MAX - 1000U;
    assert(bfsk_tx_send_byte(&tx, 0xFF) == BFSK_TX_OK);
    tx = setup(idle, 1);
    tx.timeout_us = 1U;
    assert(bfsk_tx_send_byte(&tx, 0) == BFSK_TX_ERR_START_TIMEOUT);
    puts("PASS: normal/delayed handshake, timeout stages, readback, buffer/partial,");
    puts("      arguments, status, timer wrap and sub-tick timeout.");
    return 0;
}
