#ifndef BFSK_TX_HAL_H
#define BFSK_TX_HAL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 플랫폼 구현 계약. 앱은 bfsk_tx.h만 사용한다.
 * read/write는 순서가 보장되는 32-bit device MMIO 접근이어야 한다.
 * tick은 실행 중인 단조 증가 64-bit 타이머이며 unsigned wrap을 허용한다.
 * ticks_from_us는 같은 tick 단위로 올림 변환한다(양수 us => 양수 tick).
 * HAL은 PL reset이나 전역 타이머 재설정을 수행하지 않는다.
 */
uintptr_t bfsk_tx_hal_default_base_address(void);
uint32_t bfsk_tx_hal_read32(uintptr_t base_address, uint32_t offset);
void bfsk_tx_hal_write32(uintptr_t base_address, uint32_t offset, uint32_t value);
uint64_t bfsk_tx_hal_get_ticks(void);
uint64_t bfsk_tx_hal_ticks_from_us(uint32_t microseconds);

#ifdef __cplusplus
}
#endif
#endif
