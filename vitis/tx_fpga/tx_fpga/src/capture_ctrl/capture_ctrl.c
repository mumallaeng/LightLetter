#include "capture_ctrl.h"

#include "xgpio.h"
#include "xil_printf.h"
#include "xparameters.h"
#include "xstatus.h"
#include "xtime_l.h"

#include "../roi_dma/roi_dma.h"
#include "../cpu_cnn/cpu_bench.h"

#define CAPTURE_GPIO_ID       XPAR_AXI_GPIO_0_DEVICE_ID
#define CAPTURE_REQ_CHANNEL   1u
#define START_BTN_CHANNEL     2u
#define GPIO_BIT_0            0x01u
#define DEBOUNCE_TICKS        (COUNTS_PER_SECOND / 50u) /* 20 ms */
#define FB_BYTES_PER_PIXEL    3u

static XGpio     capture_gpio;
static XAxiVdma *capture_vdma;
static UINTPTR   fb_base;
static u32       fb_stride;
static u32       fb_frame_bytes;
static int       ready;
static int       candidate_state;
static int       stable_state;
static XTime     candidate_since;

/*
 * Pick the most recently completed S2MM frame store.
 *
 * S2MM writes the stores in a circle (0, 1, 2, 0, ...). The one it is
 * writing right now may be half old, half new, so take the one before it,
 * which is complete and will not be overwritten until two frames later.
 */
static const u8 *capture_last_frame(void)
{
    u32 cur;
    u32 n;
    u32 done;

    n    = (u32)capture_vdma->MaxNumFrames;
    cur  = XAxiVdma_CurrFrameStore(capture_vdma, XAXIVDMA_WRITE);
    done = (cur + n - 1u) % n;

    return (const u8 *)(fb_base + (done * fb_frame_bytes));
}

void capture_ctrl_trigger(void)
{
    if (!ready) {
        return;
    }

    /* End-to-end timing starts here (before the log print below). */
    roi_dma_mark_request();
    xil_printf("capture: camera frame -> ROI DMA\r\n");
    if (roi_dma_send_camera_frame(capture_last_frame(), fb_stride) == XST_SUCCESS) {
        /* Same 112x112 bytes through the CPU model while the PL computes. */
        cpu_bench_roi(roi_dma_frame());
    }
}

int capture_ctrl_init(XAxiVdma *vdma, UINTPTR base, u32 width, u32 height)
{
    int status;
    XTime now;
    u32 raw;

    status = XGpio_Initialize(&capture_gpio, CAPTURE_GPIO_ID);
    if (status != XST_SUCCESS) {
        xil_printf("capture: XGpio_Initialize failed\r\n");
        return status;
    }

    /*
     * Channel 1 still reaches img_preprocess/capture_req, but the DMA-fed
     * preprocess ignores it: a DMA packet itself starts a capture. Keep it
     * driven low.
     */
    XGpio_SetDataDirection(&capture_gpio, CAPTURE_REQ_CHANNEL, 0x0u);
    XGpio_DiscreteWrite(&capture_gpio, CAPTURE_REQ_CHANNEL, 0u);

    /* Channel 2 reads the physical start_btn input. */
    XGpio_SetDataDirection(&capture_gpio, START_BTN_CHANNEL, GPIO_BIT_0);

    capture_vdma   = vdma;
    fb_base        = base;
    fb_stride      = width * FB_BYTES_PER_PIXEL;
    fb_frame_bytes = fb_stride * height;

    raw = XGpio_DiscreteRead(&capture_gpio, START_BTN_CHANNEL) & GPIO_BIT_0;
    candidate_state = (raw != 0u);
    stable_state = candidate_state;
    XTime_GetTime(&now);
    candidate_since = now;
    ready = 1;

    xil_printf("capture: GPIO ready (external active-low button -> channel 2), "
               "%d frame stores at 0x%08X\r\n",
               capture_vdma->MaxNumFrames, (unsigned)fb_base);
    return XST_SUCCESS;
}

void capture_ctrl_poll(void)
{
    int raw_state;
    XTime now;

    if (!ready) {
        return;
    }

    raw_state = ((XGpio_DiscreteRead(&capture_gpio, START_BTN_CHANNEL)
                 & GPIO_BIT_0) != 0u);
    XTime_GetTime(&now);

    if (raw_state != candidate_state) {
        candidate_state = raw_state;
        candidate_since = now;
        return;
    }

    if ((candidate_state != stable_state) &&
        ((now - candidate_since) >= DEBOUNCE_TICKS)) {
        stable_state = candidate_state;

        /* Active-low button module: capture once on the debounced press. */
        if (!stable_state) {
            capture_ctrl_trigger();
        }
    }
}
