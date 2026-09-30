#ifndef ROI_DMA_H
#define ROI_DMA_H

#include "xstatus.h"

#define ROI_DMA_WIDTH       112U
#define ROI_DMA_HEIGHT      112U
#define ROI_DMA_RGB_BYTES   3U
#define ROI_DMA_FRAME_BYTES (ROI_DMA_WIDTH * ROI_DMA_HEIGHT * ROI_DMA_RGB_BYTES)

int roi_dma_init(void);
int roi_dma_send_test_frame(void);

#endif
