/*
 * gray_filter.h
 *
 *  Created on: 2026. 9. 10.
 *      Author: kccistc
 */

#ifndef SRC_GRAY_FILTER_GRAY_FILTER_H_
#define SRC_GRAY_FILTER_GRAY_FILTER_H_

#include "xil_types.h"

int gray_init();
void gray_set(int on);
int gray_get();
void gray_toggle();

/* Legacy main.c still has a 'b' menu entry; its implementation is a no-op. */
void bin_toggle(void);

#endif /* SRC_GRAY_FILTER_GRAY_FILTER_H_ */
