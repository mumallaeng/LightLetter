#include "capture_ctrl.h"

#include "xgpio.h"
#include "xil_printf.h"
#include "xparameters.h"
#include "xstatus.h"
#include "xil_cache.h"
#include "xtime_l.h"

#include "../roi_dma/roi_dma.h"
#include "../cpu_cnn/cpu_bench.h"
#include "../cnn_ctrl/cnn_ctrl.h"
#include "../aruco/aruco_crop.h"

#define CAPTURE_GPIO_ID       XPAR_AXI_GPIO_0_DEVICE_ID
#define CAPTURE_REQ_CHANNEL   1u
#define START_BTN_CHANNEL     2u
#define GPIO_BIT_0            0x01u
#define DEBOUNCE_TICKS        (COUNTS_PER_SECOND / 50u) /* 20 ms */
#define FB_BYTES_PER_PIXEL    3u
#define CNN_WAIT_TICKS        ((COUNTS_PER_SECOND / 1000000u) * 100000u) /* 100 ms per cell */

static XGpio     capture_gpio;
static XAxiVdma *capture_vdma;
static UINTPTR   fb_base;
static u32       fb_stride;
static u32       fb_frame_bytes;
static int       ready;
static int       candidate_state;
static int       stable_state;
static XTime     candidate_since;
static aruco_cells_t  aruco_cells;
static aruco_result_t aruco_res;
static int       seq_active;     /* a 5-cell sequence is in flight */
static int       cell_next;      /* next cell to send */
static XTime     cell_sent_at;

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

/* Send cell_next to the PL. cnn_ip has one result register, so the next cell
 * is only sent after cnn_ctrl_poll has read this one (see capture_ctrl_on_result). */
static void capture_send_cell(void)
{
    if (roi_dma_send_image((const u8 *)aruco_cells.pix[cell_next]) != XST_SUCCESS) {
        seq_active = 0;
        return;
    }
    XTime_GetTime(&cell_sent_at);
    cell_next++;
    /* Same 112x112 bytes through the CPU model while the PL computes. */
    cpu_bench_roi(roi_dma_frame());
}

/* cnn_ctrl hook: runs in the main loop right after a result was read */
static void capture_ctrl_on_result(void)
{
    if (!seq_active) {
        return;
    }
    if (cell_next < ARUCO_CELL_COUNT) {
        capture_send_cell();
    } else {
        seq_active = 0;
    }
}

void capture_ctrl_trigger(void)
{
    aruco_frame_t f;
    const u8 *frame;
    XTime t0;
    XTime t1;
    u32 us;
    int rc;

    if (!ready) {
        return;
    }
    if (seq_active) {
        xil_printf("capture: previous cells still running, ignored\r\n");
        return;
    }

    /* End-to-end timing starts here (before the log print below). */
    roi_dma_mark_request();
    xil_printf("capture: camera frame -> ArUco crop -> ROI DMA\r\n");

    /* VDMA wrote the frame store, so drop stale cache lines before the CPU reads it all. */
    frame = capture_last_frame();
    Xil_DCacheInvalidateRange((INTPTR)frame, fb_frame_bytes);

    f.rgb    = frame;
    f.width  = (int)(fb_stride / FB_BYTES_PER_PIXEL);
    f.height = (int)(fb_frame_bytes / fb_stride);
    f.stride = (int)fb_stride;
    XTime_GetTime(&t0);
    rc = aruco_crop_run(&f, &aruco_cells, &aruco_res);
    XTime_GetTime(&t1);
    us = (u32)(((t1 - t0) * 1000000ULL) / COUNTS_PER_SECOND);
    if (rc != ARUCO_OK) {
        xil_printf("capture: ArUco crop failed (%d) after %u.%03u ms\r\n",
                   rc, (unsigned)(us / 1000u), (unsigned)(us % 1000u));
        return;
    }
    xil_printf("capture: ArUco %d markers, fit %d.%02d px, %u.%03u ms\r\n",
               aruco_res.n_markers, (int)aruco_res.fit_rms_px,
               (int)(aruco_res.fit_rms_px * 100.0) % 100,
               (unsigned)(us / 1000u), (unsigned)(us % 1000u));

    cell_next  = 0;
    seq_active = 1;
    capture_send_cell();
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

    cnn_ctrl_set_done_hook(capture_ctrl_on_result);

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

    /* The PL never answered, give up on this sequence. */
    if (seq_active) {
        XTime_GetTime(&now);
        if ((now - cell_sent_at) >= CNN_WAIT_TICKS) {
            xil_printf("capture: cell %d: no cnn_done\r\n", cell_next - 1);
            seq_active = 0;
        }
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
