#include "roi_dma.h"

#include "xaxidma.h"
#include "xil_cache.h"
#include "xil_printf.h"
#include "xparameters.h"
#include "xtime_l.h"

#define ROI_DMA_TIMEOUT 100000000U

static XAxiDma roi_dma;
static u8 roi_frame[ROI_DMA_FRAME_BYTES] __attribute__((aligned(64)));
static int roi_dma_ready;
static Roi_dma_times roi_times;

/* Source column/row sampled for each ROI pixel (pixel-centre mapping). */
static u16 roi_x_map[ROI_DMA_WIDTH];
static u16 roi_y_map[ROI_DMA_HEIGHT];

/*
 * Build the same representative 40x80 glyph used by the RTL testbench.
 * The 32-to-24 bit AXIS converter preserves byte order, so DDR bytes are
 * stored G, B, R to produce img_preprocess tdata={R,B,G}.
 */
static void roi_dma_make_test_frame(void)
{
    u32 x;
    u32 y;
    u32 offset;
    u8 r;
    u8 g;
    u8 b;

    for (y = 0; y < ROI_DMA_HEIGHT; ++y) {
        for (x = 0; x < ROI_DMA_WIDTH; ++x) {
            if ((x >= 30U) && (x <= 69U) &&
                (y >= 16U) && (y <= 95U)) {
                r = 20U;
                g = 30U;
                b = 40U;
            } else {
                r = 255U;
                g = 255U;
                b = 255U;
            }

            offset = ((y * ROI_DMA_WIDTH) + x) * ROI_DMA_RGB_BYTES;
            roi_frame[offset + 0U] = g;
            roi_frame[offset + 1U] = b;
            roi_frame[offset + 2U] = r;
        }
    }
}

/*
 * src = ROI_SRC_* + (2i + 1) * ROI_SRC_SIZE / (2 * 112)
 * samples the centre of each ROI_SRC_SIZE/112-pixel cell, so the grid is
 * symmetric inside the crop (224 crop: X 529..751, Y 249..471).
 */
static void roi_dma_make_maps(void)
{
    u32 i;

    for (i = 0; i < ROI_DMA_WIDTH; ++i) {
        roi_x_map[i] = (u16)(ROI_SRC_X0 +
            ((2U * i + 1U) * ROI_SRC_SIZE) / (2U * ROI_DMA_WIDTH));
    }
    for (i = 0; i < ROI_DMA_HEIGHT; ++i) {
        roi_y_map[i] = (u16)(ROI_SRC_Y0 +
            ((2U * i + 1U) * ROI_SRC_SIZE) / (2U * ROI_DMA_HEIGHT));
    }
}

/* Push roi_frame to img_preprocess as one TLAST-terminated packet. */
static int roi_dma_send_frame(void)
{
    u32 timeout;
    int status;

    XTime_GetTime(&roi_times.flush);
    Xil_DCacheFlushRange((UINTPTR)roi_frame, ROI_DMA_FRAME_BYTES);

    XTime_GetTime(&roi_times.kick);
    status = XAxiDma_SimpleTransfer(&roi_dma,
                                    (UINTPTR)roi_frame,
                                    ROI_DMA_FRAME_BYTES,
                                    XAXIDMA_DMA_TO_DEVICE);
    if (status != XST_SUCCESS) {
        xil_printf("ROI DMA: transfer start failed (%d)\r\n", status);
        return XST_FAILURE;
    }

    timeout = ROI_DMA_TIMEOUT;
    while (XAxiDma_Busy(&roi_dma, XAXIDMA_DMA_TO_DEVICE) && timeout != 0U)
        --timeout;
    XTime_GetTime(&roi_times.dma_done);

    if (timeout == 0U) {
        xil_printf("ROI DMA: TIMEOUT (downstream TREADY may be low)\r\n");
        return XST_FAILURE;
    }

    return XST_SUCCESS;
}

const Roi_dma_times *roi_dma_last_times(void)
{
    return &roi_times;
}

void roi_dma_mark_request(void)
{
    XTime_GetTime(&roi_times.request);
}

const u8 *roi_dma_frame(void)
{
    return roi_frame;
}

int roi_dma_init(void)
{
    XAxiDma_Config *config;
    int status;

    config = XAxiDma_LookupConfig(XPAR_AXIDMA_0_DEVICE_ID);
    if (config == NULL) {
        xil_printf("ROI DMA: configuration not found\r\n");
        return XST_FAILURE;
    }

    status = XAxiDma_CfgInitialize(&roi_dma, config);
    if (status != XST_SUCCESS) {
        xil_printf("ROI DMA: CfgInitialize failed (%d)\r\n", status);
        return XST_FAILURE;
    }

    if (XAxiDma_HasSg(&roi_dma)) {
        xil_printf("ROI DMA: simple mode required\r\n");
        return XST_FAILURE;
    }

    XAxiDma_IntrDisable(&roi_dma, XAXIDMA_IRQ_ALL_MASK,
                        XAXIDMA_DMA_TO_DEVICE);
    roi_dma_make_maps();
    roi_dma_ready = 1;

    xil_printf("ROI DMA ready: %u bytes, device %d, base 0x%08X\r\n",
               (unsigned)ROI_DMA_FRAME_BYTES,
               (int)XPAR_AXIDMA_0_DEVICE_ID,
               (unsigned)XPAR_AXIDMA_0_BASEADDR);
    return XST_SUCCESS;
}

int roi_dma_send_test_frame(void)
{
    if (!roi_dma_ready) {
        xil_printf("ROI DMA: not initialized\r\n");
        return XST_FAILURE;
    }

    /* roi_frame is shared with the camera path, so rebuild it every time. */
    roi_dma_make_test_frame();

    if (roi_dma_send_frame() != XST_SUCCESS)
        return XST_FAILURE;

    xil_printf("ROI DMA: sent %u bytes (112x112 RGB, one frame TLAST)\r\n",
               (unsigned)ROI_DMA_FRAME_BYTES);
    return XST_SUCCESS;
}

/*
 * frame  : start of one completed RGB888 frame store written by VDMA S2MM
 * stride : bytes per frame line
 *
 * VDMA stores each pixel as the bytes G, B, R, which is exactly the order
 * the DMA path expects, so pixels are copied without any colour shuffle.
 */
int roi_dma_send_camera_frame(const u8 *frame, u32 stride)
{
    const u8 *row;
    const u8 *src;
    u8 *dst;
    u32 x;
    u32 y;
    XTime t0;
    XTime t1;

    if (!roi_dma_ready) {
        xil_printf("ROI DMA: not initialized\r\n");
        return XST_FAILURE;
    }

    XTime_GetTime(&t0);

    dst = roi_frame;
    for (y = 0; y < ROI_DMA_HEIGHT; ++y) {
        row = frame + (roi_y_map[y] * stride);

        /*
         * The frame buffer is written by VDMA, not the CPU. Drop any stale
         * cache lines covering the sampled span of this row before reading.
         */
        Xil_DCacheInvalidateRange((INTPTR)(row + roi_x_map[0] * ROI_DMA_RGB_BYTES),
            (roi_x_map[ROI_DMA_WIDTH - 1U] - roi_x_map[0] + 1U) * ROI_DMA_RGB_BYTES);

        for (x = 0; x < ROI_DMA_WIDTH; ++x) {
            src = row + (roi_x_map[x] * ROI_DMA_RGB_BYTES);
            dst[0] = src[0];
            dst[1] = src[1];
            dst[2] = src[2];
            dst += ROI_DMA_RGB_BYTES;
        }
    }

    XTime_GetTime(&t1);

    if (roi_dma_send_frame() != XST_SUCCESS)
        return XST_FAILURE;

    xil_printf("ROI DMA: camera ROI (%u,%u) %ux%u -> 112x112 sent, downscale %u us\r\n",
               (unsigned)ROI_SRC_X0, (unsigned)ROI_SRC_Y0,
               (unsigned)ROI_SRC_SIZE, (unsigned)ROI_SRC_SIZE,
               (unsigned)((t1 - t0) / (COUNTS_PER_SECOND / 1000000U)));
    return XST_SUCCESS;
}
