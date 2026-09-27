#include "bfsk_tx.h"
#include "../hal/bfsk_tx_hal.h"

static int valid_device(const bfsk_tx *tx)
{
    return tx != NULL && tx->base_address != 0U &&
           (tx->base_address & 3U) == 0U && tx->timeout_us != 0U;
}

static int wait_status(bfsk_tx *tx, uint32_t mask, uint32_t expected)
{
    uint64_t start, now;
    const uint64_t ticks = bfsk_tx_hal_ticks_from_us(tx->timeout_us);
    start = bfsk_tx_hal_get_ticks();
    do
    {
        tx->last_status = bfsk_tx_hal_read32(tx->base_address, BFSK_TX_STATUS_OFFSET);
        if ((tx->last_status & mask) == expected)
            return 1;
        now = bfsk_tx_hal_get_ticks();
    } while ((uint64_t)(now - start) < ticks);
    return 0;
}

bfsk_tx_result bfsk_tx_init(bfsk_tx *tx, uintptr_t base_address,
                            uint32_t timeout_us)
{
    if (tx == NULL)
        return BFSK_TX_ERR_ARGUMENT;
    /* 실패한 재초기화 뒤 이전 설정으로 송신하지 않도록 무효화한다. */
    tx->base_address = 0U;
    tx->timeout_us = 0U;
    tx->last_status = 0U;
    if (base_address == 0U || (base_address & 3U) != 0U || timeout_us == 0U)
        return BFSK_TX_ERR_ARGUMENT;
    tx->base_address = base_address;
    tx->timeout_us = timeout_us;
    return BFSK_TX_OK;
}

bfsk_tx_result bfsk_tx_init_default(bfsk_tx *tx)
{
    return bfsk_tx_init(tx, bfsk_tx_hal_default_base_address(),
                        BFSK_TX_DEFAULT_TIMEOUT_US);
}

bfsk_tx_result bfsk_tx_get_status(bfsk_tx *tx, uint32_t *status)
{
    if (!valid_device(tx) || status == NULL)
        return BFSK_TX_ERR_ARGUMENT;
    tx->last_status = bfsk_tx_hal_read32(tx->base_address, BFSK_TX_STATUS_OFFSET);
    *status = tx->last_status;
    return BFSK_TX_OK;
}

bfsk_tx_result bfsk_tx_send_byte(bfsk_tx *tx, uint8_t data)
{
    if (!valid_device(tx))
        return BFSK_TX_ERR_ARGUMENT;
    if (!wait_status(tx, BFSK_TX_READY | BFSK_TX_BUSY, BFSK_TX_READY))
        return BFSK_TX_ERR_READY_TIMEOUT;
    bfsk_tx_hal_write32(tx->base_address, BFSK_TX_DATA_OFFSET, (uint32_t)data);
    /* START 전에 DATA[7:0] 쓰기 반영 확인. */
    if ((bfsk_tx_hal_read32(tx->base_address, BFSK_TX_DATA_OFFSET) & 0xFFU) != data)
        return BFSK_TX_ERR_DATA_READBACK;
    /* RTL에서 1클럭 펄스를 생성하므로 CTRL=0 쓰기는 필요 없다. */
    bfsk_tx_hal_write32(tx->base_address, BFSK_TX_CTRL_OFFSET, BFSK_TX_START);
    if (!wait_status(tx, BFSK_TX_BUSY, BFSK_TX_BUSY))
        return BFSK_TX_ERR_START_TIMEOUT;
    if (!wait_status(tx, BFSK_TX_READY | BFSK_TX_BUSY, BFSK_TX_READY))
        return BFSK_TX_ERR_DONE_TIMEOUT;
    return BFSK_TX_OK;
}

bfsk_tx_result bfsk_tx_send_buffer(bfsk_tx *tx, const uint8_t *data,
                                   size_t length, size_t *sent)
{
    size_t i;
    bfsk_tx_result result;
    if (sent != NULL)
        *sent = 0U;
    if (!valid_device(tx) || (data == NULL && length != 0U))
        return BFSK_TX_ERR_ARGUMENT;
    for (i = 0U; i < length; ++i)
    {
        result = bfsk_tx_send_byte(tx, data[i]);
        if (result != BFSK_TX_OK)
            return result;
        if (sent != NULL)
            *sent = i + 1U;
    }
    return BFSK_TX_OK;
}

const char *bfsk_tx_result_string(bfsk_tx_result result)
{
    switch (result)
    {
    case BFSK_TX_OK:
        return "OK";
    case BFSK_TX_ERR_ARGUMENT:
        return "invalid argument";
    case BFSK_TX_ERR_READY_TIMEOUT:
        return "READY timeout";
    case BFSK_TX_ERR_DATA_READBACK:
        return "DATA readback mismatch";
    case BFSK_TX_ERR_START_TIMEOUT:
        return "START timeout (BUSY not observed)";
    case BFSK_TX_ERR_DONE_TIMEOUT:
        return "DONE timeout";
    default:
        return "unknown error";
    }
}
