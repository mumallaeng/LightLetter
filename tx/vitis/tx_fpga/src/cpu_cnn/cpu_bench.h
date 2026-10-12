#ifndef SRC_CPU_CNN_CPU_BENCH_H_
#define SRC_CPU_CNN_CPU_BENCH_H_

#include "xil_types.h"
#include "xtime_l.h"

/*
 * ARM 에서 CPU 경로 시간 측정 (global timer, XTime).
 *
 *   cpu_bench_testset() : 내장 70 장 (cpu_cnn_testset.h) 으로 bit-exact 확인 + 1 장당 min/avg/max
 *   cpu_bench_ticks()   : 한 장을 한 번 돌린 시간 (XTime tick, COUNTS_PER_SECOND 단위)
 *   cpu_bench_print_us(): tick -> "123.45 us" 출력
 *
 *   cpu_bench_roi()     : PL 로 보낸 것과 같은 112x112 roi_frame 으로
 *                         cpu_preprocess + cpu_cnn_run 을 돌리고 시간을 기록
 *   cpu_bench_take()    : 마지막 cpu_bench_roi() 결과를 한 번 꺼냄 (없으면 0)
 */

typedef struct {
    int   cls;
    XTime pre_ticks;    /* cpu_preprocess : 112x112 -> 28x28 cnn 입력 */
    XTime cnn_ticks;    /* cpu_cnn_run    : 28x28 -> class            */
} Cpu_bench_result;

void cpu_bench_testset(void);
u64  cpu_bench_ticks(const s16 *img, int *cls);
void cpu_bench_print_us(u64 ticks);

void cpu_bench_roi(const u8 *roi);
int  cpu_bench_take(Cpu_bench_result *res);

#endif /* SRC_CPU_CNN_CPU_BENCH_H_ */
