#ifndef NUCLEO_BOARD_H
#define NUCLEO_BOARD_H
#include <stdint.h>
void board_init(void);
int board_capture_pop(uint32_t *timestamp);
void board_capture_discard(void);
uint32_t board_capture_losses(void);
uint32_t board_capture_edges(void);
uint32_t board_millis(void);
void board_write(const char *text);
void board_toggle_led(void);
#endif
