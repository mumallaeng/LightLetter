/* TX-9B: PS 함수 호출 예제. PL bitstream과 PS 초기화 후 실행한다. */
#include "drv/bfsk_tx.h"
#include "xil_printf.h"
#include "sleep.h"

int main(void)
{
    bfsk_tx tx;
    bfsk_tx_result result;

    result = bfsk_tx_init_default(&tx);
    if (result != BFSK_TX_OK) {
        xil_printf("BFSK init: %s\r\n", bfsk_tx_result_string(result));
        return 1;
    }
    xil_printf("\r\nBFSK TX PS API example, BASE=0x%08lx\r\n",
               (unsigned long)tx.base_address);
    /* 리셋 직후 첫 'A' 프레임: D5 00 41 C0. Frame ID/CRC는 PL이 생성한다. */
    for (;;) {
        result = bfsk_tx_send_byte(&tx, 0x41U);
        if (result != BFSK_TX_OK) {
            xil_printf("TX error: %s, STATUS=0x%08lx\r\n",
                       bfsk_tx_result_string(result),
                       (unsigned long)tx.last_status);
            /* 오류 후 중복 송신을 피하기 위해 자동 재시도하지 않는다. */
            return 1;
        }
        xil_printf("0x41 transmission complete\r\n");
        sleep(1);
    }
}
