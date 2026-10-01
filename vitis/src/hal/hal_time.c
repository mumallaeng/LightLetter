#include "hal_time.h"
#include "xtime_l.h"
uint32_t hal_time_ms(void)
{
    XTime ticks;
    XTime_GetTime(&ticks);
    return (uint32_t)(ticks / (COUNTS_PER_SECOND / 1000U));
}
