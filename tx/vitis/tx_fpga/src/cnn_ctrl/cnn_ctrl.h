#ifndef SRC_CNN_CTRL_CNN_CTRL_H_
#define SRC_CNN_CTRL_CNN_CTRL_H_

#include "xil_types.h"
#include "xtime_l.h"

#define CNN_NUM_CLASS 26u

int  cnn_ctrl_init(void);
/* Single results (the 'd' test frame, a late cell): PL / CPU timing, then report. */
void cnn_ctrl_poll(void);
/*
 * Called inside cnn_isr (interrupt context) with every result and the global
 * timer value taken on ISR entry (= cnn_done). Return non-zero to take the
 * result; it is then not queued for cnn_ctrl_poll / cnn_ctrl_get_result.
 * Keep it short: no printing, no waiting.
 */
void cnn_ctrl_set_isr_hook(int (*hook)(u8 cls, XTime done_at));
int  cnn_ctrl_get_result(u8 *cls);
/* cnn_done (cnn_isr entry) of the result the last cnn_ctrl_get_result() returned. */
XTime cnn_ctrl_result_time(void);
/* Prints one result (log line + recognition JSON) and sends it over BFSK TX.
 * Blocks until the BFSK frame is out, so call it from the main loop only. */
void cnn_ctrl_report(u8 cls);
/* "  <label> 123.456 us" for the XTime span from -> to. */
void cnn_ctrl_print_us(const char *label, XTime from, XTime to);

#endif
