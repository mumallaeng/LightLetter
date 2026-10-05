#ifndef SRC_CAPTURE_CTRL_CAPTURE_CTRL_H_
#define SRC_CAPTURE_CTRL_CAPTURE_CTRL_H_

#include "xil_types.h"
#include "xaxivdma.h"

/*
 * vdma     : running VDMA instance whose S2MM channel fills the frame stores
 * fb_base  : address of S2MM frame store 0 (stores are packed back to back)
 * width    : frame width in pixels  (RGB888, 3 bytes per pixel)
 * height   : frame height in lines
 */
int  capture_ctrl_init(XAxiVdma *vdma, UINTPTR fb_base, u32 width, u32 height);
void capture_ctrl_poll(void);
void capture_ctrl_trigger(void);

#endif /* SRC_CAPTURE_CTRL_CAPTURE_CTRL_H_ */
