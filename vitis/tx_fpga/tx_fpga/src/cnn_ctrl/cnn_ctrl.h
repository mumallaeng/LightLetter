#ifndef SRC_CNN_CTRL_CNN_CTRL_H_
#define SRC_CNN_CTRL_CNN_CTRL_H_

#include "xil_types.h"

#define CNN_NUM_CLASS 26u

int  cnn_ctrl_init(void);
void cnn_ctrl_poll(void);
int  cnn_ctrl_get_result(u8 *cls);

#endif
