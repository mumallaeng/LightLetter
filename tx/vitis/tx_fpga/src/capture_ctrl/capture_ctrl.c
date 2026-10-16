#include "capture_ctrl.h"

#include "xgpio.h"
#include "xil_printf.h"
#include "xparameters.h"
#include "xstatus.h"
#include "xil_cache.h"
#include "xil_exception.h"
#include "xtime_l.h"

#include "../roi_dma/roi_dma.h"
#include "../cnn_ctrl/cnn_ctrl.h"
#include "../cpu_cnn/cpu_bench.h"
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
/* The DMA reads each cell in place: 64-byte aligned base, and 37632 is a multiple of 64. */
static aruco_cells_t  aruco_cells __attribute__((aligned(64)));
static aruco_result_t aruco_res;

/* Shared with capture_on_cnn_done (cnn_isr context). */
static volatile int   seq_active;    /* a 5-cell sequence is in flight          */
static volatile int   seq_done;      /* every cell answered, or a DMA start failed */
static volatile int   seq_failed;    /* stopped early: next cell DMA start failed */
static volatile u32   cell_done;     /* results received so far                 */
static volatile u8    cell_cls[ARUCO_CELL_COUNT];
static volatile XTime cell_kick[ARUCO_CELL_COUNT];  /* DMA start of each cell */
static volatile XTime cell_irq[ARUCO_CELL_COUNT];   /* cnn_done of each cell  */
static volatile XTime cell_sent_at;
static XTime          seq_request;   /* button / 'c'                            */
static XTime          seq_flush;     /* ArUco done, cache flush of the 5 cells  */

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

/*
 * cnn_isr hook (interrupt context). Store the result and hand the next cell
 * to the PL straight away. cnn_done means img_preprocess has already taken the
 * whole previous frame, so MM2S is idle, and cnn_isr has just read the single
 * result register, so nothing can be overwritten. The five cells therefore run
 * back to back without the main loop, and BFSK TX starts only after all five.
 */
static int capture_on_cnn_done(u8 cls, XTime done_at)
{
    XTime now;

    if (!seq_active || seq_done) {
        return 0;
    }

    cell_cls[cell_done] = cls;
    cell_irq[cell_done] = done_at;
    cell_done++;

    if (cell_done < ARUCO_CELL_COUNT) {
        XTime_GetTime(&now);
        cell_kick[cell_done] = now;
        cell_sent_at = now;
        if (roi_dma_start_image(&aruco_cells.pix[cell_done][0][0][0]) != XST_SUCCESS) {
            seq_failed = 1;
            seq_done   = 1;
        }
    } else {
        seq_done = 1;
    }
    return 1;
}

static void print_ratio(const char *label, XTime num, XTime den)
{
    u32 ratio = (den != 0) ? (u32)((num * 100u) / den) : 0u;

    xil_printf("  %s %u.%02u x\r\n", label,
               (unsigned)(ratio / 100u), (unsigned)(ratio % 100u));
}

/*
 * Main loop: all cells are in. Run the CPU model on the same cells for
 * comparison, print PL / CPU timing, then print and send the letters.
 *
 * In this path the next cell is started from cnn_isr without waiting for the
 * DMA, so there is no dma_done stamp: the PL time per cell is
 * DMA kick -> cnn_done (DMA + img_preprocess + cnn_ip). The 'd' key still
 * gives the split DMA / preprocess+CNN times.
 */
static void capture_finish_seq(void)
{
    u8    cls[ARUCO_CELL_COUNT];
    XTime kick[ARUCO_CELL_COUNT];
    XTime irq[ARUCO_CELL_COUNT];
    Cpu_bench_result cpu[ARUCO_CELL_COUNT];
    XTime cpu_sum = 0;
    XTime pl_e2e;
    XTime cpu_e2e;
    u32   n;
    u32   i;
    int   failed;

    Xil_ExceptionDisable();
    n      = cell_done;
    failed = seq_failed;
    for (i = 0; i < n; i++) {
        cls[i]  = cell_cls[i];
        kick[i] = cell_kick[i];
        irq[i]  = cell_irq[i];
    }
    seq_active = 0;
    Xil_ExceptionEnable();

    if (failed) {
        xil_printf("capture: cell %d: ROI DMA start failed\r\n", (int)n);
    }
    if (n == 0u) {
        return;
    }

    for (i = 0; i < n; i++) {
        cpu_bench_roi(&aruco_cells.pix[i][0][0][0]);
        cpu_bench_take(&cpu[i]);
        cpu_sum += cpu[i].pre_ticks + cpu[i].cnn_ticks;
    }

    for (i = 0; i < n; i++) {
        XTime cpu_total = cpu[i].pre_ticks + cpu[i].cnn_ticks;

        xil_printf("capture: cell %d\r\n", (int)i);
        xil_printf("PL  (DMA kick -> CNN done):\r\n");
        cnn_ctrl_print_us("DMA+prep+CNN    :", kick[i], irq[i]);
        xil_printf("CPU (same cell, ARM A9):\r\n");
        cnn_ctrl_print_us("preprocess      :", 0, cpu[i].pre_ticks);
        cnn_ctrl_print_us("CNN             :", 0, cpu[i].cnn_ticks);
        cnn_ctrl_print_us("CPU total       :", 0, cpu_total);
        print_ratio("PL speed-up     :", cpu_total, irq[i] - kick[i]);
        xil_printf("  class           : CPU %d, PL %d %s\r\n",
                   cpu[i].cls, (int)cls[i],
                   (cpu[i].cls == (int)cls[i]) ? "(match)" : "(MISMATCH)");
    }

    pl_e2e  = irq[n - 1u] - seq_request;
    cpu_e2e = (seq_flush - seq_request) + cpu_sum;

    xil_printf("capture: %d cell(s), end-to-end (request -> last class):\r\n", (int)n);
    cnn_ctrl_print_us("capture+ArUco   :", seq_request, seq_flush);
    cnn_ctrl_print_us("cache flush x5  :", seq_flush, kick[0]);
    cnn_ctrl_print_us("PL  all cells   :", kick[0], irq[n - 1u]);
    cnn_ctrl_print_us("CPU all cells   :", 0, cpu_sum);
    print_ratio("PL speed-up     :", cpu_sum, irq[n - 1u] - kick[0]);
    cnn_ctrl_print_us("PL  path        :", 0, pl_e2e);
    cnn_ctrl_print_us("CPU path        :", 0, cpu_e2e);
    print_ratio("E2E speed-up    :", cpu_e2e, pl_e2e);

    for (i = 0; i < n; i++) {
        xil_printf("capture: cell %d\r\n", (int)i);
        cnn_ctrl_report(cls[i]);
    }
}

void capture_ctrl_trigger(void)
{
    aruco_frame_t f;
    const u8 *frame;
    XTime kick0;
    int rc;

    if (!ready) {
        return;
    }
    if (seq_active) {
        xil_printf("capture: previous cells still running, ignored\r\n");
        return;
    }
    XTime_GetTime(&seq_request);

    /* VDMA wrote the frame store, so drop stale cache lines before the CPU reads it all. */
    frame = capture_last_frame();
    Xil_DCacheInvalidateRange((INTPTR)frame, fb_frame_bytes);

    f.rgb    = frame;
    f.width  = (int)(fb_stride / FB_BYTES_PER_PIXEL);
    f.height = (int)(fb_frame_bytes / fb_stride);
    f.stride = (int)fb_stride;
    rc = aruco_crop_run(&f, &aruco_cells, &aruco_res);
    if (rc != ARUCO_OK) {
        xil_printf("capture: ArUco crop failed (%d)\r\n", rc);
        return;
    }

    /* The PL reads the cells straight out of aruco_cells: write all five back once. */
    XTime_GetTime(&seq_flush);
    Xil_DCacheFlushRange((INTPTR)&aruco_cells, sizeof(aruco_cells));

    cell_done  = 0;
    seq_done   = 0;
    seq_failed = 0;
    XTime_GetTime(&kick0);
    cell_kick[0] = kick0;
    cell_sent_at = kick0;
    seq_active = 1;     /* before the kick: the first cnn_done may come right after */

    if (roi_dma_start_image(&aruco_cells.pix[0][0][0][0]) != XST_SUCCESS) {
        xil_printf("capture: ROI DMA start failed\r\n");
        seq_active = 0;
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

    cnn_ctrl_set_isr_hook(capture_on_cnn_done);

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

    if (seq_active) {
        int timed_out = 0;

        /* The PL never answered, give up on this sequence. IRQs are off so
         * cnn_isr cannot finish the cell between the check and the give-up. */
        Xil_ExceptionDisable();
        XTime_GetTime(&now);
        if (!seq_done && (now - cell_sent_at) >= CNN_WAIT_TICKS) {
            seq_active = 0;
            timed_out  = 1;
        }
        Xil_ExceptionEnable();

        if (timed_out) {
            xil_printf("capture: cell %d: no cnn_done\r\n", (int)cell_done);
        } else if (seq_done) {
            capture_finish_seq();
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
