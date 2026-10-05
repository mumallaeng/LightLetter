#ifndef SRC_CNN_CTRL_CNN_CTRL_H_
#define SRC_CNN_CTRL_CNN_CTRL_H_

#include "xil_types.h"

#define CNN_NUM_CLASS 26u

int  cnn_ctrl_init(void);
void cnn_ctrl_poll(void);
/*
 * Called inside cnn_isr (interrupt context) with every result. Return non-zero
 * to take the result; it is then not queued for cnn_ctrl_poll / cnn_ctrl_get_result.
 * Keep it short: no printing, no waiting.
 */
void cnn_ctrl_set_isr_hook(int (*hook)(u8 cls));
int  cnn_ctrl_get_result(u8 *cls);
/* Prints one result (log line + recognition JSON) and sends it over BFSK TX.
 * Blocks until the BFSK frame is out, so call it from the main loop only. */
void cnn_ctrl_report(u8 cls);

#endif
