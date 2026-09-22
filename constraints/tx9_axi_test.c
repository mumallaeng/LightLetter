/*
 * TX-9 Hardware Verification
 * Zynq PS -> AXI4-Lite -> Optical TX
 *
 * Register Map
 *   BASE + 0x00 : TX_DATA   [7:0] Character ID
 *   BASE + 0x04 : TX_CTRL   bit0 START
 *   BASE + 0x08 : TX_STATUS bit0 READY, bit1 BUSY
 *
 * 사용 전:
 *   Vivado Address Editor에서 할당된 AXI Slave Base Address를
 *   TX_BASE_ADDR에 맞춰 수정한다.
 */

#include <stdint.h>
#include "xil_io.h"
#include "xil_printf.h"
#include "sleep.h"

/* Vivado Address Editor의 실제 Base Address로 맞출 것 */
#ifndef TX_BASE_ADDR
#define TX_BASE_ADDR 0x43C00000U
#endif

#define TX_DATA_OFFSET    0x00U
#define TX_CTRL_OFFSET    0x04U
#define TX_STATUS_OFFSET  0x08U

#define TX_STATUS_READY   (1U << 0)
#define TX_STATUS_BUSY    (1U << 1)
#define TX_CTRL_START     (1U << 0)

static inline uint32_t tx_read_status(void)
{
    return Xil_In32(TX_BASE_ADDR + TX_STATUS_OFFSET);
}

static void tx_send_char(uint8_t data)
{
    uint32_t status;

    /* TX가 새 Character를 받을 수 있을 때까지 대기 */
    do {
        status = tx_read_status();
    } while ((status & TX_STATUS_READY) == 0U);

    /* Character ID Write */
    Xil_Out32(TX_BASE_ADDR + TX_DATA_OFFSET, (uint32_t)data);

    /* START Event 발생 */
    Xil_Out32(TX_BASE_ADDR + TX_CTRL_OFFSET, TX_CTRL_START);

    /* 실제 송신 시작 확인 */
    do {
        status = tx_read_status();
    } while ((status & TX_STATUS_BUSY) == 0U);

    /* 마지막 Optical Symbol까지 완료될 때까지 대기 */
    do {
        status = tx_read_status();
    } while ((status & TX_STATUS_BUSY) != 0U);
}

int main(void)
{
    xil_printf("\r\n");
    xil_printf("========================================\r\n");
    xil_printf("BFSK TX-9 Hardware Verification\r\n");
    xil_printf("BASE = 0x%08lx\r\n", (unsigned long)TX_BASE_ADDR);
    xil_printf("========================================\r\n");

    /*
     * 첫 Hardware 검증 Character:
     *   DATA     = 0x41 ('A')
     *   Frame ID = 초기값 0x00
     *   Frame    = D5 00 41 C0
     *
     * 예상 Optical 출력:
     *   SYNC 25 kHz x 4
     *   이후 Data Bit에 따라 10 / 20 kHz
     */
    tx_send_char(0x41U);

    xil_printf("0x41 transmission complete\r\n");

    /*
     * 오실로스코프에서 반복 파형을 보기 쉽게 약 1초 간격으로
     * 같은 Character를 계속 송신한다.
     *
     * Frame ID는 매 송신마다 증가하므로 CRC/Data Carrier Pattern은
     * 첫 Frame 이후 달라질 수 있다.
     */
    while (1) {
        sleep(1);
        tx_send_char(0x41U);
        xil_printf("0x41 transmission complete\r\n");
    }

    return 0;
}
