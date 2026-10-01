#ifndef HAL_CONSOLE_H
#define HAL_CONSOLE_H
#include <stddef.h>
/* The BSP selects the physical stdout UART. */
void hal_console_write(const char *text);
/* Never waits for FIFO space. One foreground writer only. */
size_t hal_console_try_write(const char *data, size_t count);
#endif
