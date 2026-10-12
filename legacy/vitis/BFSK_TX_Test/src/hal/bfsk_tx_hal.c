/* Zynq-7000 standalone HAL. Xilinx BSP 의존성은 이 파일에만 둔다. */
#include "bfsk_tx_hal.h"
#include "xparameters.h"
#include "xil_io.h"
#include "xtime_l.h"

#ifndef BFSK_TX_BASEADDR
#define BFSK_TX_BASEADDR XPAR_OPTICAL_TX_AXI_TOP_0_BASEADDR
#endif

uintptr_t bfsk_tx_hal_default_base_address(void)
{
    return (uintptr_t)BFSK_TX_BASEADDR;
}

uint32_t bfsk_tx_hal_read32(uintptr_t base_address, uint32_t offset)
{
    return Xil_In32(base_address + offset);
}

void bfsk_tx_hal_write32(uintptr_t base_address, uint32_t offset, uint32_t value)
{
    Xil_Out32(base_address + offset, value);
}

uint64_t bfsk_tx_hal_get_ticks(void)
{
    XTime ticks;
    XTime_GetTime(&ticks);
    return (uint64_t)ticks;
}

uint64_t bfsk_tx_hal_ticks_from_us(uint32_t microseconds)
{
    /* 64-bit 곱셈과 올림으로 overflow/짧은 timeout의 절삭을 방지한다. */
    return ((uint64_t)COUNTS_PER_SECOND * microseconds + 999999U) / 1000000U;
}
