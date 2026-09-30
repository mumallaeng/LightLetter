#include "roi_dma.h"

#include "xaxidma.h"
#include "xil_cache.h"
#include "xil_printf.h"
#include "xparameters.h"

#define ROI_DMA_TIMEOUT 100000000U

static XAxiDma roi_dma;
static u8 roi_frame[ROI_DMA_FRAME_BYTES] __attribute__((aligned(64)));
static int roi_dma_ready;

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
    roi_dma_make_test_frame();
    roi_dma_ready = 1;

    xil_printf("ROI DMA ready: %u bytes, device %d, base 0x%08X\r\n",
               (unsigned)ROI_DMA_FRAME_BYTES,
               (int)XPAR_AXIDMA_0_DEVICE_ID,
               (unsigned)XPAR_AXIDMA_0_BASEADDR);
    return XST_SUCCESS;
}

int roi_dma_send_test_frame(void)
{
    u32 timeout;
    int status;

    if (!roi_dma_ready) {
        xil_printf("ROI DMA: not initialized\r\n");
        return XST_FAILURE;
    }

    Xil_DCacheFlushRange((UINTPTR)roi_frame, ROI_DMA_FRAME_BYTES);

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

    if (timeout == 0U) {
        xil_printf("ROI DMA: TIMEOUT (downstream TREADY may be low)\r\n");
        return XST_FAILURE;
    }

    xil_printf("ROI DMA: sent %u bytes (112x112 RGB, one frame TLAST)\r\n",
               (unsigned)ROI_DMA_FRAME_BYTES);
    return XST_SUCCESS;
}
