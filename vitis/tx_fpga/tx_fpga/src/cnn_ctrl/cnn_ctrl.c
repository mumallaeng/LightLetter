#include "cnn_ctrl.h"

#include "xparameters.h"
#include "xscugic.h"
#include "xil_exception.h"
#include "xil_io.h"
#include "xil_printf.h"
#include "xstatus.h"
#include "xtime_l.h"

#include "../roi_dma/roi_dma.h"
#include "../cpu_cnn/cpu_bench.h"

#define CNN_BASEADDR        XPAR_CNN_IP_0_S00_AXI_BASEADDR
#define CNN_IRQ_ID          XPAR_FABRIC_CNN_IP_0_INTR_INTR
#define GIC_DEVICE_ID       XPAR_PS7_SCUGIC_0_DEVICE_ID

#define CNN_RDR_OFFSET      0x04u
#define CNN_RESULT_MASK     0x1Fu

#define GIC_PRIORITY        0xA0u
#define GIC_TRIGGER_RISING  0x3u

static XScuGic gic;
static int ready;
static volatile u8 latest_result;
static volatile int result_pending;
static volatile u32 done_count;
static volatile u32 overrun_count;
static volatile XTime irq_time;
static XTime result_time;
static void (*done_hook)(void);

static void cnn_isr(void *ref)
{
    XTime now;

    (void)ref;

    /* First thing in the ISR: closest point to cnn_done on the CPU side. */
    XTime_GetTime(&now);
    irq_time = now;

    if (result_pending) {
        overrun_count++;
    }
    latest_result = (u8)(Xil_In32(CNN_BASEADDR + CNN_RDR_OFFSET)
                         & CNN_RESULT_MASK);
    result_pending = 1;
    done_count++;
}

int cnn_ctrl_init(void)
{
    XScuGic_Config *cfg;
    int status;

    cfg = XScuGic_LookupConfig(GIC_DEVICE_ID);
    if (cfg == NULL) {
        xil_printf("cnn: XScuGic_LookupConfig failed\r\n");
        return XST_FAILURE;
    }

    status = XScuGic_CfgInitialize(&gic, cfg, cfg->CpuBaseAddress);
    if (status != XST_SUCCESS) {
        xil_printf("cnn: XScuGic_CfgInitialize failed\r\n");
        return status;
    }

    Xil_ExceptionInit();
    Xil_ExceptionRegisterHandler(XIL_EXCEPTION_ID_INT,
                                 (Xil_ExceptionHandler)XScuGic_InterruptHandler,
                                 &gic);
    XScuGic_SetPriorityTriggerType(&gic, CNN_IRQ_ID,
                                   GIC_PRIORITY, GIC_TRIGGER_RISING);

    status = XScuGic_Connect(&gic, CNN_IRQ_ID,
                             (Xil_InterruptHandler)cnn_isr, NULL);
    if (status != XST_SUCCESS) {
        xil_printf("cnn: XScuGic_Connect failed\r\n");
        return status;
    }

    result_pending = 0;
    done_count = 0;
    overrun_count = 0;
    XScuGic_Enable(&gic, CNN_IRQ_ID);
    Xil_ExceptionEnable();
    ready = 1;

    xil_printf("cnn: IRQ %d ready (result @ 0x%08X)\r\n",
               (int)CNN_IRQ_ID, (unsigned)(CNN_BASEADDR + CNN_RDR_OFFSET));
    return XST_SUCCESS;
}

int cnn_ctrl_get_result(u8 *cls)
{
    int has_result;

    if (!ready) {
        return 0;
    }

    XScuGic_Disable(&gic, CNN_IRQ_ID);
    has_result = result_pending;
    if (has_result) {
        *cls = latest_result;
        result_time = irq_time;
        result_pending = 0;
    }
    XScuGic_Enable(&gic, CNN_IRQ_ID);
    return has_result;
}

void cnn_ctrl_set_done_hook(void (*hook)(void))
{
    done_hook = hook;
}

/* Global-timer ticks (CPU clock / 2, 3 ns) -> "us.nnn" */
static void cnn_print_us(const char *label, XTime from, XTime to)
{
    u32 ns = (u32)(((to - from) * 1000000000ULL) / COUNTS_PER_SECOND);

    xil_printf("  %s %6u.%03u us\r\n", label,
               (unsigned)(ns / 1000u), (unsigned)(ns % 1000u));
}

/*
 * PL timing of the last ROI hand-off, measured from the moment the PS
 * starts giving the finished 112x112 frame to the PL:
 *
 *   flush -> kick     : D-cache flush of roi_frame (PS side of the hand-off)
 *   kick  -> dma_done : DMA + width converter + img_preprocess input stage
 *   dma_done -> irq   : img_preprocess fit/output + cnn_ip, until cnn_done
 *
 * followed by the CPU model run on the same 112x112 roi_frame
 * (cpu_preprocess + cpu_cnn_run), if one was run for this frame,
 * and the end-to-end time from the capture request (button / 'c' / 'd'):
 *
 *   request -> flush  : frame-store pick + 112x112 downscale (shared)
 *   PL  path          : request -> cnn_done IRQ
 *   CPU path          : (request -> flush) + CPU preprocess + CPU CNN
 */
static void cnn_print_timing(u8 pl_cls)
{
    const Roi_dma_times *t = roi_dma_last_times();
    Cpu_bench_result cpu;
    int have_cpu;
    XTime pl_total;
    XTime cpu_total = 0;
    XTime pl_e2e;
    XTime cpu_e2e;
    u32 ratio;

    xil_printf("PL  (PS hand-off -> CNN done):\r\n");
    cnn_print_us("cache flush     :", t->flush, t->kick);
    cnn_print_us("DMA 112x112 in  :", t->kick, t->dma_done);
    cnn_print_us("preprocess+CNN  :", t->dma_done, result_time);
    cnn_print_us("PL total (kick) :", t->kick, result_time);
    cnn_print_us("total (flush)   :", t->flush, result_time);

    have_cpu = cpu_bench_take(&cpu);
    if (have_cpu) {
        pl_total  = result_time - t->kick;
        cpu_total = cpu.pre_ticks + cpu.cnn_ticks;
        ratio     = (pl_total != 0) ? (u32)((cpu_total * 100u) / pl_total) : 0u;

        xil_printf("CPU (same 112x112 ROI, ARM A9):\r\n");
        cnn_print_us("preprocess      :", 0, cpu.pre_ticks);
        cnn_print_us("CNN             :", 0, cpu.cnn_ticks);
        cnn_print_us("CPU total       :", 0, cpu_total);
        xil_printf("  PL speed-up     : %u.%02u x (CPU total / PL total)\r\n",
                   (unsigned)(ratio / 100u), (unsigned)(ratio % 100u));
        xil_printf("  class           : CPU %d, PL %d %s\r\n",
                   cpu.cls, (int)pl_cls,
                   (cpu.cls == (int)pl_cls) ? "(match)" : "(MISMATCH)");
    }

    pl_e2e = result_time - t->request;

    xil_printf("End-to-end (capture request -> class):\r\n");
    cnn_print_us("capture+ROI 112 :", t->request, t->flush);
    cnn_print_us("PL  path        :", 0, pl_e2e);
    if (have_cpu) {
        cpu_e2e = (t->flush - t->request) + cpu_total;
        ratio   = (pl_e2e != 0) ? (u32)((cpu_e2e * 100u) / pl_e2e) : 0u;

        cnn_print_us("CPU path        :", 0, cpu_e2e);
        xil_printf("  E2E speed-up    : %u.%02u x (CPU path / PL path)\r\n",
                   (unsigned)(ratio / 100u), (unsigned)(ratio % 100u));
    }
}

void cnn_ctrl_poll(void)
{
    u8 cls;

    if (!cnn_ctrl_get_result(&cls)) {
        return;
    }

    cnn_print_timing(cls);

    if (cls < CNN_NUM_CLASS) {
        xil_printf("cnn: result = %d ('%c') [done #%d]\r\n",
                   (int)cls, 'A' + cls, (int)done_count);
        xil_printf("{\"type\":\"recognition\",\"char\":\"%c\","
                   "\"class_id\":%d,\"crc_ok\":true}\r\n",
                   'A' + cls, (int)cls);
    } else {
        xil_printf("cnn: result = %d (out of range) [done #%d]\r\n",
                   (int)cls, (int)done_count);
    }

    if (overrun_count) {
        xil_printf("cnn: WARNING %d result(s) overwritten before read\r\n",
                   (int)overrun_count);
        overrun_count = 0;
    }

    if (done_hook) {
        done_hook();
    }
}
