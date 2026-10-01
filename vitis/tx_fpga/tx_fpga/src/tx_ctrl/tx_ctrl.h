#ifndef SRC_TX_CTRL_TX_CTRL_H_
#define SRC_TX_CTRL_TX_CTRL_H_

#include "xil_types.h"

/*
 * CNN class -> ASCII -> optical BFSK TX (drv/bfsk_tx).
 *
 *   class 0..25 = 'A'..'Z' (cnn_ip output order), one byte per BFSK frame.
 *
 * tx_ctrl_send_class() blocks until the PL has sent the frame
 * (about 57.6 ms + 10 ms gap per byte), so call it from the main loop,
 * never from the cnn_done ISR.
 */

int  tx_ctrl_init(void);
/* 'A'..'Z' for class 0..25, 0 for anything else */
char tx_ctrl_class_to_ascii(u8 cls);
/* Sends the class as one ASCII byte. Returns XST_SUCCESS, or XST_FAILURE
 * (not initialized, class out of range, driver error). No retry on error. */
int  tx_ctrl_send_class(u8 cls);

#endif
