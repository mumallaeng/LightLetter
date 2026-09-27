/*
 *  cam_gpio.c
 *
 *  Converted from Digilent's PS_GPIO<T> class template. The template
 *  parameter was an interrupt controller, used so a GPIO input could raise an
 *  interrupt. We only ever drive one output, so none of that survives.
 */

#include "cam_gpio.h"
#include "xparameters.h"
#include "xgpiops.h"
#include "xil_printf.h"

static XGpioPs gpio_inst;
static int     initialised = 0;

int cam_gpio_init(void)
{
    XGpioPs_Config *cfg;
    int status;

    cfg = XGpioPs_LookupConfig(XPAR_XGPIOPS_0_DEVICE_ID);
    if (cfg == NULL) {
        xil_printf("cam_gpio: LookupConfig failed\r\n");
        return CAM_GPIO_ERR_LOOKUP;
    }

    status = XGpioPs_CfgInitialize(&gpio_inst, cfg, cfg->BaseAddr);
    if (status != XST_SUCCESS) {
        xil_printf("cam_gpio: CfgInitialize failed\r\n");
        return CAM_GPIO_ERR_INIT;
    }

    /* Direction 1 = output, then enable the output driver. Both calls are
     * needed : setting the direction alone leaves the pad tri-stated, which
     * looks exactly like a working configuration until you measure it. */
    XGpioPs_SetDirectionPin(&gpio_inst, CAM_GPIO_PIN, 1);
    XGpioPs_SetOutputEnablePin(&gpio_inst, CAM_GPIO_PIN, 1);
    XGpioPs_WritePin(&gpio_inst, CAM_GPIO_PIN, 1);

    initialised = 1;
    return CAM_GPIO_OK;
}

void cam_gpio_set(u8 level)
{
    if (!initialised) {
        return;
    }
    XGpioPs_WritePin(&gpio_inst, CAM_GPIO_PIN, level ? 1 : 0);
}
