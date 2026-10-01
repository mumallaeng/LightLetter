#ifndef HAL_GPIO_H
#define HAL_GPIO_H
#include <stdint.h>
/* Single AXI GPIO instance. Initialize before reading or writing. */
int hal_gpio_init(void);
uint32_t hal_gpio_read(void);
void hal_gpio_set_clear(int asserted);
#endif
