#include "cnn_ctrl.h"

#include "xparameters.h"
#include "xscugic.h"
#include "xil_exception.h"
#include "xil_io.h"
#include "xil_printf.h"
#include "xstatus.h"

#include "../tx_ctrl/tx_ctrl.h"

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
static void (*done_hook)(void);

static void cnn_isr(void *ref)
{
    (void)ref;

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
        result_pending = 0;
    }
    XScuGic_Enable(&gic, CNN_IRQ_ID);
    return has_result;
}

void cnn_ctrl_set_done_hook(void (*hook)(void))
{
    done_hook = hook;
}

void cnn_ctrl_poll(void)
{
    u8 cls;

    if (!cnn_ctrl_get_result(&cls)) {
        return;
    }

    if (cls < CNN_NUM_CLASS) {
        xil_printf("cnn: result = %d ('%c') [done #%d]\r\n",
                   (int)cls, 'A' + cls, (int)done_count);
        xil_printf("{\"type\":\"recognition\",\"char\":\"%c\","
                   "\"class_id\":%d,\"crc_ok\":true}\r\n",
                   'A' + cls, (int)cls);

        /* Blocks until the BFSK frame is out, so the next cell (done_hook)
         * is only handed to the PL after this letter has been sent. */
        tx_ctrl_send_class(cls);
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
