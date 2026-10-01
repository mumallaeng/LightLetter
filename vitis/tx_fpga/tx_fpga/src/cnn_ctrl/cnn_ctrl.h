#ifndef SRC_CNN_CTRL_CNN_CTRL_H_
#define SRC_CNN_CTRL_CNN_CTRL_H_

#include "xil_types.h"

#define CNN_NUM_CLASS 26u

int  cnn_ctrl_init(void);
void cnn_ctrl_poll(void);
/* Called from cnn_ctrl_poll (main loop, not the ISR) after each result has been read. */
void cnn_ctrl_set_done_hook(void (*hook)(void));
int  cnn_ctrl_get_result(u8 *cls);

#endif
