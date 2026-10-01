#include "tx_ctrl.h"

#include "xil_printf.h"
#include "xstatus.h"

#include "../drv/bfsk_tx.h"
#include "../cnn_ctrl/cnn_ctrl.h"

static bfsk_tx tx;
static int ready;

int tx_ctrl_init(void)
{
    bfsk_tx_result result;

    result = bfsk_tx_init_default(&tx);
    if (result != BFSK_TX_OK) {
        xil_printf("tx: init failed (%s)\r\n", bfsk_tx_result_string(result));
        return XST_FAILURE;
    }
    ready = 1;

    xil_printf("tx: BFSK TX ready (base 0x%08X)\r\n", (unsigned)tx.base_address);
    return XST_SUCCESS;
}

char tx_ctrl_class_to_ascii(u8 cls)
{
    if (cls >= CNN_NUM_CLASS) {
        return 0;
    }
    return (char)('A' + cls);
}

int tx_ctrl_send_class(u8 cls)
{
    bfsk_tx_result result;
    char ch;

    if (!ready) {
        return XST_FAILURE;
    }

    ch = tx_ctrl_class_to_ascii(cls);
    if (ch == 0) {
        xil_printf("tx: class %d has no letter, not sent\r\n", (int)cls);
        return XST_FAILURE;
    }

    /* The driver does not retry, and a timeout does not cancel the PL frame,
     * so a failed byte is reported and dropped rather than sent twice. */
    result = bfsk_tx_send_byte(&tx, (uint8_t)ch);
    if (result != BFSK_TX_OK) {
        xil_printf("tx: '%c' (0x%02X) failed: %s, STATUS=0x%08X\r\n",
                   ch, (unsigned)ch, bfsk_tx_result_string(result),
                   (unsigned)tx.last_status);
        return XST_FAILURE;
    }
    return XST_SUCCESS;
}
