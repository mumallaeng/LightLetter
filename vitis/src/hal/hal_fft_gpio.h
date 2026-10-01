#ifndef HAL_FFT_GPIO_H
#define HAL_FFT_GPIO_H
#include <stdint.h>
int hal_fft_gpio_init(void);
void hal_fft_control(uint32_t control);
uint32_t hal_fft_status(void);
uint64_t hal_fft_power(void);
#endif
