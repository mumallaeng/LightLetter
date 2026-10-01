#include "cpu_bench.h"

#include "xil_printf.h"
#include "xtime_l.h"

#include "cpu_cnn.h"
#include "cpu_cnn_testset.h"
#include "cpu_preprocess.h"

#define BENCH_REPEAT    20u

static s16 roi_img[CPU_CNN_IMG_PIX];
static Cpu_bench_result roi_result;
static int roi_result_valid;

void cpu_bench_roi(const u8 *roi)
{
    XTime t0, t1, t2;

    XTime_GetTime(&t0);
    cpu_preprocess(roi, roi_img);
    XTime_GetTime(&t1);
    roi_result.cls = cpu_cnn_run(roi_img, NULL);
    XTime_GetTime(&t2);

    roi_result.pre_ticks = t1 - t0;
    roi_result.cnn_ticks = t2 - t1;
    roi_result_valid = 1;
}

int cpu_bench_take(Cpu_bench_result *res)
{
    if (!roi_result_valid) {
        return 0;
    }
    *res = roi_result;
    roi_result_valid = 0;
    return 1;
}

void cpu_bench_print_us(u64 ticks)
{
    /* 0.01 us 단위 */
    u64 cus = ticks * 100000000ull / COUNTS_PER_SECOND;

    xil_printf("%d.%02d us", (int)(cus / 100u), (int)(cus % 100u));
}

u64 cpu_bench_ticks(const s16 *img, int *cls)
{
    XTime t0, t1;
    int c;

    XTime_GetTime(&t0);
    c = cpu_cnn_run(img, NULL);
    XTime_GetTime(&t1);
    if (cls) {
        *cls = c;
    }
    return (u64)(t1 - t0);
}

void cpu_bench_testset(void)
{
    s16 lg[CPU_CNN_NUM_CLASS];
    u64 t, sum = 0, mn = ~0ull, mx = 0;
    int n, i, r, cls, same = 0, hit = 0;

    xil_printf("\r\ncpu bench: %d images, %d repeats each (ARM, cpu_cnn.c)\r\n",
               TESTSET_N, (int)BENCH_REPEAT);

    for (n = 0; n < TESTSET_N; n++) {
        int ok;

        cls = cpu_cnn_run(testset_img[n], lg);
        ok = (cls == testset_class[n]);
        for (i = 0; i < CPU_CNN_NUM_CLASS; i++) {
            if (lg[i] != testset_logit[n][i]) {
                ok = 0;
            }
        }
        same += ok;
        hit  += (cls == testset_label[n]);

        /* 첫 번은 캐시 warm-up, 이후 BENCH_REPEAT 번 평균 */
        t = 0;
        for (r = 0; r < (int)BENCH_REPEAT; r++) {
            t += cpu_bench_ticks(testset_img[n], NULL);
        }
        t /= BENCH_REPEAT;
        sum += t;
        if (t < mn) mn = t;
        if (t > mx) mx = t;
    }

    xil_printf("  bit-exact : %d / %d (class + 26 logits == integer model)\r\n", same, TESTSET_N);
    xil_printf("  accuracy  : %d / %d\r\n", hit, TESTSET_N);
    xil_printf("  latency   : min ");
    cpu_bench_print_us(mn);
    xil_printf("  avg ");
    cpu_bench_print_us(sum / TESTSET_N);
    xil_printf("  max ");
    cpu_bench_print_us(mx);
    xil_printf("  / image\r\n");
}
