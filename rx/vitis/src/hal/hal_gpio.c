#include "hal_gpio.h"
#include "xgpio.h"
#include "xparameters.h"
#include "xstatus.h"

#define INPUT_CHANNEL 1U
#define CLEAR_CHANNEL 2U
static XGpio packet_gpio;

int hal_gpio_init(void)
{
    int status = XGpio_Initialize(&packet_gpio, XPAR_AXI_GPIO_0_DEVICE_ID);
    if (status != XST_SUCCESS) return status;
    XGpio_SetDataDirection(&packet_gpio, INPUT_CHANNEL, 0xFFFFFFFFU);
    XGpio_SetDataDirection(&packet_gpio, CLEAR_CHANNEL, 0U);
    XGpio_DiscreteWrite(&packet_gpio, CLEAR_CHANNEL, 0U);
    return 0;
}

uint32_t hal_gpio_read(void)
{
    return (uint32_t)XGpio_DiscreteRead(&packet_gpio, INPUT_CHANNEL);
}

void hal_gpio_set_clear(int asserted)
{
    XGpio_DiscreteWrite(&packet_gpio, CLEAR_CHANNEL, asserted ? 1U : 0U);
}
