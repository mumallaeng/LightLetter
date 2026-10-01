#include "hal_fft_gpio.h"
#include "xgpio.h"
#include "xparameters.h"
static XGpio control_gpio, data_gpio;
int hal_fft_gpio_init(void)
{
    int s = XGpio_Initialize(&control_gpio, XPAR_AXI_GPIO_1_DEVICE_ID);
    if (s) return s;
    s = XGpio_Initialize(&data_gpio, XPAR_AXI_GPIO_2_DEVICE_ID);
    if (s) return s;
    XGpio_SetDataDirection(&control_gpio, 1, 0);
    XGpio_SetDataDirection(&control_gpio, 2, 0xffffffffU);
    XGpio_SetDataDirection(&data_gpio, 1, 0xffffffffU);
    XGpio_SetDataDirection(&data_gpio, 2, 0xffffffffU);
    hal_fft_control(0);
    return 0;
}
void hal_fft_control(uint32_t v) { XGpio_DiscreteWrite(&control_gpio, 1, v); }
uint32_t hal_fft_status(void) { return XGpio_DiscreteRead(&control_gpio, 2); }
uint64_t hal_fft_power(void)
{
    uint32_t lo = XGpio_DiscreteRead(&data_gpio, 1);
    uint32_t hi = XGpio_DiscreteRead(&data_gpio, 2);
    return ((uint64_t)(hi & 255U) << 32) | lo;
}
