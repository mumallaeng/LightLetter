#ifndef SRC_CNN_CTRL_CNN_CTRL_H_
#define SRC_CNN_CTRL_CNN_CTRL_H_

#include "xil_types.h"

/*
 * cnn_ip register map (S00_AXI, cnn_ip_v1_0_S00_AXI.v)
 *   0x00 CNN_SR  : bit 0 = cnn_done (live 1-clock pulse, not sticky)
 *   0x04 CNN_RDR : bit[4:0] = cnn_result (argmax class, 0 = 'A' .. 25 = 'Z')
 *
 * The IP's intr output is cnn_done itself, wired to IRQ_F2P[0].
 */
#define CNN_NUM_CLASS   26u

int  cnn_ctrl_init(void);
void cnn_ctrl_poll(void);
int  cnn_ctrl_get_result(u8 *cls);

#endif /* SRC_CNN_CTRL_CNN_CTRL_H_ */
