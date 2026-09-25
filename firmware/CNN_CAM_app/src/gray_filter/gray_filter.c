/*
 * gray_filter.c
 *
 *  Created on: 2026. 9. 10.
 *      Author: kccistc
 */


#include "gray_filter.h"
#include "xgpio.h"
#include "xparameters.h"
#include "xil_printf.h"
#include "xstatus.h"

#define GRAY_GPIO_ID	XPAR_AXI_GPIO_0_DEVICE_ID

#define CH_CTRL 1
#define BIT_FILTER_EN	0x01u

static XGpio gray_filter_gpio;
static int ready = 0;
static u32 ctrl_shadow = 0;

int gray_init()
{
	int status = 0;

	if (ready) {
		return XST_SUCCESS;
	}

	status = XGpio_Initialize(&gray_filter_gpio, GRAY_GPIO_ID);
	if (status != XST_SUCCESS) {
		xil_printf("gray_filter: XGpio_Initialize failed\r\n");
		return status;
	}

	XGpio_SetDataDirection(&gray_filter_gpio, CH_CTRL, 0x0); // all output

	ctrl_shadow = 0;
	XGpio_DiscreteWrite(&gray_filter_gpio, CH_CTRL, ctrl_shadow);

	ready = 1;
	xil_printf("gray_filter: ok\r\n");

	return XST_SUCCESS;
}

void gray_set(int on)
{
	if (!ready)
	{
		return;
	}

	if (on)
	{
		ctrl_shadow |= BIT_FILTER_EN;
	} else {
		ctrl_shadow &= ~BIT_FILTER_EN;
	}

	XGpio_DiscreteWrite(&gray_filter_gpio, CH_CTRL, ctrl_shadow);

	xil_printf("gray_filter: %s\r\n", on ? "ON (grayscale)" : "OFF (color)");
}

int gray_get()
{
	return (ctrl_shadow & BIT_FILTER_EN) ? 1 : 0;
}

void gray_toggle()
{
	gray_set(!gray_get());
}





















