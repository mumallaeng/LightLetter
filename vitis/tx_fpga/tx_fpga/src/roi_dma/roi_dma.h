#ifndef ROI_DMA_H
#define ROI_DMA_H

#include "xil_types.h"
#include "xstatus.h"
#include "xtime_l.h"

#define ROI_DMA_WIDTH       112U
#define ROI_DMA_HEIGHT      112U
#define ROI_DMA_RGB_BYTES   3U
#define ROI_DMA_FRAME_BYTES (ROI_DMA_WIDTH * ROI_DMA_HEIGHT * ROI_DMA_RGB_BYTES)

/*
 * Camera ROI: centre 224x224 square of the 1280x720 frame
 * (X 528..751, Y 248..471). Nearest-neighbour downscale to 112x112.
 * display_python/capture_test.py ROI_X0/ROI_Y0/ROI_SIZE must match.
 */
#define ROI_SRC_X0          528U
#define ROI_SRC_Y0          248U
#define ROI_SRC_SIZE        224U

/* Global-timer timestamps of the most recent roi_frame hand-off to PL. */
typedef struct {
    XTime request;   /* capture request: debounced button press / 'c' / 'd'  */
    XTime flush;     /* cache flush starts: PS begins handing roi_frame over */
    XTime kick;      /* XAxiDma_SimpleTransfer issued                        */
    XTime dma_done;  /* MM2S idle: img_preprocess took all 112x112 pixels    */
} Roi_dma_times;

int roi_dma_init(void);
int roi_dma_send_test_frame(void);
int roi_dma_send_camera_frame(const u8 *frame, u32 stride);
/* One already-cropped 112x112 image, bytes G,B,R per pixel, copied into roi_frame and sent. */
int roi_dma_send_image(const u8 *img112);
const Roi_dma_times *roi_dma_last_times(void);
/* Stamp Roi_dma_times.request; call first thing when a capture is requested. */
void roi_dma_mark_request(void);
/* The 112x112 G,B,R frame last sent to PL (same bytes the CPU model uses). */
const u8 *roi_dma_frame(void);

#endif
