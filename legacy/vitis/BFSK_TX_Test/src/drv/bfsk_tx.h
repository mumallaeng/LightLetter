#ifndef BFSK_TX_H
#define BFSK_TX_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define BFSK_TX_DATA_OFFSET 0x00U
#define BFSK_TX_CTRL_OFFSET 0x04U
#define BFSK_TX_STATUS_OFFSET 0x08U
#define BFSK_TX_READY 0x01U
#define BFSK_TX_BUSY 0x02U
#define BFSK_TX_START 0x01U
/* 기본 프레임: (4 + 32) symbols * 1.6 ms = 약 57.6 ms */
#define BFSK_TX_DEFAULT_TIMEOUT_US 250000U

    typedef enum
    {
        BFSK_TX_OK = 0,
        BFSK_TX_ERR_ARGUMENT = -1,
        BFSK_TX_ERR_READY_TIMEOUT = -2,
        BFSK_TX_ERR_DATA_READBACK = -3,
        BFSK_TX_ERR_START_TIMEOUT = -4,
        BFSK_TX_ERR_DONE_TIMEOUT = -5
    } bfsk_tx_result;

    typedef struct
    {
        uintptr_t base_address;
        uint32_t timeout_us;
        uint32_t last_status;
    } bfsk_tx;

    /* init은 설정만 저장하며 송신/PL reset을 수행하지 않는다. */
    bfsk_tx_result bfsk_tx_init(bfsk_tx *tx, uintptr_t base_address,
                                uint32_t timeout_us);
    /* HAL이 제공하는 BSP 주소와 기본 timeout으로 초기화한다. */
    bfsk_tx_result bfsk_tx_init_default(bfsk_tx *tx);
    bfsk_tx_result bfsk_tx_get_status(bfsk_tx *tx, uint32_t *status);
    /* Blocking API. timeout_us는 READY/START/DONE 각 단계에 적용된다.
     * 같은 IP에 대한 호출은 하나의 실행 문맥에서 직렬화해야 한다.
     * 타임아웃은 PL 송신을 취소하지 않는다. 자동 재시도하지 않는다.
     */
    bfsk_tx_result bfsk_tx_send_byte(bfsk_tx *tx, uint8_t data);
    /* 바이트마다 별도 프레임. sent는 완료를 확인한 바이트 수이다.
     * 오류가 난 현재 바이트의 송신 여부는 불확실할 수 있다.
     * length=0일 때 data=NULL 허용. sent=NULL 허용.
     */
    bfsk_tx_result bfsk_tx_send_buffer(bfsk_tx *tx, const uint8_t *data,
                                       size_t length, size_t *sent);
    const char *bfsk_tx_result_string(bfsk_tx_result result);

#ifdef __cplusplus
}
#endif
#endif
