#include "capture_ctrl.h"

#include "xgpio.h"
#include "xil_printf.h"
#include "xparameters.h"
#include "xstatus.h"
#include "xtime_l.h"
#include "sleep.h"

#define CAPTURE_GPIO_ID       XPAR_AXI_GPIO_0_DEVICE_ID
#define CAPTURE_REQ_CHANNEL   1u
#define START_BTN_CHANNEL     2u
#define GPIO_BIT_0            0x01u
#define DEBOUNCE_TICKS        (COUNTS_PER_SECOND / 50u) /* 20 ms */

static XGpio capture_gpio;
static int   ready;
static int   candidate_state;
static int   stable_state;
static XTime candidate_since;

void capture_ctrl_trigger(void)
{
    if (!ready) {
        return;
    }

    /*
     * Keep the request high long enough to cross safely from the AXI GPIO
     * register into the video clock domain.  Ten microseconds is negligible
     * compared with a video frame but is thousands of PL clock cycles.
     */
    XGpio_DiscreteWrite(&capture_gpio, CAPTURE_REQ_CHANNEL, GPIO_BIT_0);
    usleep(10u);
    XGpio_DiscreteWrite(&capture_gpio, CAPTURE_REQ_CHANNEL, 0u);
    xil_printf("capture: requested; waiting for next frame\r\n");
}

int capture_ctrl_init(void)
{
    int status;
    XTime now;
    u32 raw;

    status = XGpio_Initialize(&capture_gpio, CAPTURE_GPIO_ID);
    if (status != XST_SUCCESS) {
        xil_printf("capture: XGpio_Initialize failed\r\n");
        return status;
    }

    /* Channel 1 drives img_preprocess/capture_req. */
    XGpio_SetDataDirection(&capture_gpio, CAPTURE_REQ_CHANNEL, 0x0u);
    XGpio_DiscreteWrite(&capture_gpio, CAPTURE_REQ_CHANNEL, 0u);

    /* Channel 2 reads the physical start_btn input. */
    XGpio_SetDataDirection(&capture_gpio, START_BTN_CHANNEL, GPIO_BIT_0);

    raw = XGpio_DiscreteRead(&capture_gpio, START_BTN_CHANNEL) & GPIO_BIT_0;
    candidate_state = (raw != 0u);
    stable_state = candidate_state;
    XTime_GetTime(&now);
    candidate_since = now;
    ready = 1;

    xil_printf("capture: GPIO ready (BTN0 -> channel 2, request -> channel 1)\r\n");
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

        /* Active-high button: request once on the debounced rising edge. */
        if (stable_state) {
            capture_ctrl_trigger();
        }
    }
}
