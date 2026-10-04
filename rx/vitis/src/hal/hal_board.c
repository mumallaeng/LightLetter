#include "hal_board.h"
#include "platform.h"
void hal_board_init(void) { init_platform(); }
void hal_board_cleanup(void) { cleanup_platform(); }
