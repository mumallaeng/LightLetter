#include "power_meas.h"

#include <string.h>

#include "xparameters.h"
#include "xil_types.h"
#include "xil_io.h"
#include "xil_printf.h"
#include "xil_exception.h"
#include "xil_cache.h"
#include "xstatus.h"
#include "xscugic.h"
#include "xscutimer.h"
#include "xadcps.h"
#include "xaxivdma.h"
#include "xuartps_hw.h"
#include "xtime_l.h"
#include "sleep.h"

#include "../cam_gpio/cam_gpio.h"
#include "../ov5640/OV5640.h"
#include "../ov5640/OV5640_REG.h"
#include "../mipi_rx/mipi_rx.h"
#include "../display_ctrl_hdmi/display_ctrl.h"
#include "../roi_dma/roi_dma.h"
#include "../cnn_ctrl/cnn_ctrl.h"
#include "../cpu_cnn/cpu_cnn.h"
#include "../cpu_cnn/cpu_preprocess.h"
#include "../cpu_cnn/cpu_bench.h"
#include "../cpu_cnn/cpu_cnn_testset.h"

/* main.c 의 영상 경로 인스턴스 (정지할 때만 씀) */
extern XAxiVdma    vdma;
extern DisplayCtrl dispCtrl;

/*===========================================================================
 *  측정 조건
 *===========================================================================*/
#ifndef POWER_MEAS_VIN_MV
#define POWER_MEAS_VIN_MV        5000u   /* 보드 입력 전압. DMM 으로 잰 값으로 바꿀 것 */
#endif
#define POWER_MEAS_SAMPLE_HZ     1000u   /* XADC 읽기 주기 (SCU private timer ISR) */
#define POWER_MEAS_SETTLE_MS     1000u   /* 상태 전환 직후 버리는 구간 (XADC 256 평균 지연 포함) */
#define POWER_MEAS_STATE_MS      10000u  /* 상태 하나의 측정 구간 */
#define POWER_MEAS_WARMUP_MS     20000u  /* 영상 경로 정지 후 열 안정화 (WFI, 기록 안 함) */
#define POWER_MEAS_ROUNDS        3u
#define POWER_MEAS_TRACE_MS      100u    /* TRC 줄 하나가 평균하는 시간 */
#define POWER_MEAS_PL_TIMEOUT_MS 100u

#define SMP_PER_MS      (POWER_MEAS_SAMPLE_HZ / 1000u)
#define TRACE_N         (POWER_MEAS_TRACE_MS * SMP_PER_MS)
#define TRACE_MAX       (POWER_MEAS_STATE_MS / POWER_MEAS_TRACE_MS)

/* cnn_ctrl.c 와 같은 레지스터 맵 (SR 은 읽으면 done_flag 가 지워짐) */
#define CNN_BASEADDR        XPAR_CNN_IP_0_S00_AXI_BASEADDR
#define CNN_IRQ_ID          XPAR_FABRIC_CNN_IP_0_INTR_INTR
#define CNN_SR_OFFSET       0x00u
#define CNN_RDR_OFFSET      0x04u
#define CNN_SR_DONE         0x01u
#define CNN_RESULT_MASK     0x1Fu

#define GIC_CPU_BASEADDR    XPAR_SCUGIC_0_CPU_BASEADDR
#define GIC_DIST_BASEADDR   XPAR_SCUGIC_0_DIST_BASEADDR
#define TIMER_IRQ_ID        XPAR_SCUTIMER_INTR
#define TIMER_PRIORITY      0xA8u   /* cnn (0xA0) 보다 낮게 */
#define GIC_TRIGGER_RISING  0x3u

typedef enum {
    ST_WFI = 0,
    ST_BUSY,
    ST_CPU_CNN,
    /* 아래 둘은 매회 memcpy 37 KB + cache flush + DMA kick (roi_dma_send_image) */
    ST_PL_CNN,      /* cnn IRQ 결과를 CPU 가 깨어서 기다림 (busy-wait)              */
    ST_PL_CNN_WFI,  /* cnn IRQ 결과를 WFI 로 잠들어 기다림                          */
    /* flush 는 상태 시작에 한 번만, 매회 같은 버퍼를 DMA kick (roi_dma_start_image)
     * -> 시간 측정의 "PL total (kick)" 과 같은 범위 : DMA + preprocess + CNN    */
    ST_PL_KICK_WFI,
    ST_NUM
} Meas_state;

static const char *const st_name[ST_NUM] = {
    "WFI", "BUSY", "CPU_CNN", "PL_CNN", "PL_CNN_WFI", "PL_KICK_WFI"
};

/* 문서 14절 순서. PL 앞뒤에 WFI 를 끼워 인접 idle 대비 증가분을 볼 수 있게 함 */
static const Meas_state round_seq[] = {
    ST_WFI, ST_BUSY, ST_CPU_CNN, ST_WFI, ST_PL_CNN, ST_PL_CNN_WFI, ST_PL_KICK_WFI, ST_WFI
};
#define ROUND_LEN   (sizeof(round_seq) / sizeof(round_seq[0]))

typedef struct {
    u32 n;          /* settle 이후 XADC 샘플 수 */
    u64 sum;        /* raw16 합 */
    u64 sq;         /* raw16^2 합 */
    u32 min;
    u32 max;
    u32 infer;      /* 측정 구간 inference 횟수 */
    u64 ticks;      /* 측정 구간 길이 (XTime) */
    u32 cls_err;    /* 기준 class 와 다른 결과, DMA 실패, timeout */
} Meas_run;

typedef struct {
    u32 runs;
    u64 n;
    u64 sum;
    u64 infer;
    u64 ticks;
    u32 cls_err;
} Meas_agg;

typedef enum { PL_DONE_IRQ, PL_DONE_POLL } Pl_done_mode;

static XScuTimer meas_timer;
static XAdcPs    meas_xadc;
static int       hw_ready;
static int       video_stopped;

/* 입력 70 장 (112x112 G,B,R). 37632 B 는 64 의 배수라 모든 장이 64 B 정렬 -> DMA 가 바로 읽음 */
#define TS_SCALE    (ROI_DMA_WIDTH / CPU_CNN_IMG_W)     /* 28 -> 112 : 4 */
static u8  ts_img[TESTSET_N][ROI_DMA_FRAME_BYTES] __attribute__((aligned(64)));
static u8  cpu_ref[TESTSET_N];      /* 같은 112 이미지로 돌린 CPU 모델 class */
static u8  pl_ref[TESTSET_N];       /* PL class (확인 단계)                  */
static u32 ts_match;                /* cpu_ref == pl_ref 인 장 수            */
static u32 ts_cpu_hit;              /* cpu_ref == testset_label              */
static u32 ts_pl_hit;               /* pl_ref  == testset_label              */
static s16 cpu_in[CPU_CNN_IMG_PIX];
static Pl_done_mode pl_mode;

/* timer ISR <-> 전경 */
static volatile int smp_active;
static volatile u32 smp_idx;
static volatile u32 smp_settle;
static volatile u32 smp_total;
static volatile u64 acc_sum;
static volatile u64 acc_sq;
static volatile u32 acc_n;
static volatile u32 acc_min;
static volatile u32 acc_max;
static volatile u32 trc_sum;
static volatile u32 trc_cnt;
static volatile u32 trc_n;
static u32 trc_buf[TRACE_MAX + 1u];

/*===========================================================================
 *  XADC 샘플링 (1 kHz)
 *
 *  모든 상태에서 같은 ISR 이 같은 주기로 돈다. WFI 상태를 깨워 샘플을 얻는 것도
 *  이 ISR 이라, ISR 자체의 부하는 모든 상태에 똑같이 들어가고 차이에서 빠진다.
 *  settle 구간에도 XADC 를 읽어 (버리기만 함) ISR 부하를 일정하게 유지한다.
 *===========================================================================*/
static void meas_timer_isr(void *ref)
{
    u32 raw;

    (void)ref;
    XScuTimer_ClearInterruptStatus(&meas_timer);

    if (!smp_active || smp_idx >= smp_total) {
        return;
    }

    raw = XAdcPs_GetAdcData(&meas_xadc, XADCPS_CH_VPVN);

    if (smp_idx >= smp_settle) {
        acc_sum += raw;
        acc_sq  += (u64)raw * raw;
        acc_n++;
        if (raw < acc_min) acc_min = raw;
        if (raw > acc_max) acc_max = raw;

        trc_sum += raw;
        if (++trc_cnt == TRACE_N) {
            if (trc_n < TRACE_MAX) {
                trc_buf[trc_n++] = trc_sum;
            }
            trc_sum = 0;
            trc_cnt = 0;
        }
    }
    smp_idx++;
}

static int meas_hw_init(void)
{
    XAdcPs_Config *acfg;
    XScuTimer_Config *tcfg;

    acfg = XAdcPs_LookupConfig(XPAR_XADCPS_0_DEVICE_ID);
    if (acfg == NULL ||
        XAdcPs_CfgInitialize(&meas_xadc, acfg, acfg->BaseAddress) != XST_SUCCESS) {
        xil_printf("power: XADC init failed\r\n");
        return XST_FAILURE;
    }

    /* VP/VN (IMON) 하나만 연속 변환 + 256 평균. 설정은 safe mode 에서 바꿔야 함 */
    XAdcPs_SetSequencerMode(&meas_xadc, XADCPS_SEQ_MODE_SAFE);
    XAdcPs_SetAvg(&meas_xadc, XADCPS_AVG_256_SAMPLES);
    XAdcPs_SetSeqInputMode(&meas_xadc, 0u);     /* unipolar : IMON 은 0 ~ 1 V */
    XAdcPs_SetSeqAvgEnables(&meas_xadc, XADCPS_SEQ_CH_VPVN);
    XAdcPs_SetSeqChEnables(&meas_xadc, XADCPS_SEQ_CH_VPVN);
    XAdcPs_SetSequencerMode(&meas_xadc, XADCPS_SEQ_MODE_CONTINPASS);

    tcfg = XScuTimer_LookupConfig(XPAR_XSCUTIMER_0_DEVICE_ID);
    if (tcfg == NULL) {
        xil_printf("power: SCU timer config not found\r\n");
        return XST_FAILURE;
    }
    XScuTimer_CfgInitialize(&meas_timer, tcfg, tcfg->BaseAddr);
    XScuTimer_Stop(&meas_timer);
    /* private timer 클럭 = CPU / 2 */
    XScuTimer_LoadTimer(&meas_timer,
        (XPAR_CPU_CORTEXA9_0_CPU_CLK_FREQ_HZ / 2u) / POWER_MEAS_SAMPLE_HZ - 1u);
    XScuTimer_EnableAutoReload(&meas_timer);
    XScuTimer_EnableInterrupt(&meas_timer);

    /*
     * GIC 인스턴스는 cnn_ctrl.c 안에 있으므로, 같은 설정 테이블에 핸들러만 추가한다.
     * (XScuGic_CfgInitialize 를 다시 부르면 cnn 핸들러 설정을 건드릴 수 있음)
     */
    XScuGic_RegisterHandler(GIC_CPU_BASEADDR, TIMER_IRQ_ID,
                            (Xil_InterruptHandler)meas_timer_isr, &meas_timer);
    XScuGic_SetPriTrigTypeByDistAddr(GIC_DIST_BASEADDR, TIMER_IRQ_ID,
                                     TIMER_PRIORITY, GIC_TRIGGER_RISING);
    XScuGic_EnableIntr(GIC_DIST_BASEADDR, TIMER_IRQ_ID);
    Xil_ExceptionEnable();
    XScuTimer_Start(&meas_timer);

    hw_ready = 1;
    return XST_SUCCESS;
}

static void meas_begin(u32 settle_ms, u32 meas_ms)
{
    Xil_ExceptionDisable();
    acc_sum = 0;
    acc_sq  = 0;
    acc_n   = 0;
    acc_min = 0xFFFFFFFFu;
    acc_max = 0;
    trc_sum = 0;
    trc_cnt = 0;
    trc_n   = 0;
    smp_settle = settle_ms * SMP_PER_MS;
    smp_total  = smp_settle + meas_ms * SMP_PER_MS;
    smp_idx    = 0;
    smp_active = 1;
    Xil_ExceptionEnable();
}

static inline int meas_done(void)
{
    return smp_idx >= smp_total;
}

static inline int meas_in_window(void)
{
    return smp_idx >= smp_settle;
}

/*===========================================================================
 *  영상 경로 정지 (생산자 -> 소비자)
 *===========================================================================*/
static void meas_stop_video(void)
{
    OV5640_WriteSCCB(REG_SYS_CTRL0, REG_SYS_CTRL0_PWDN);   /* 센서 스트리밍 정지 */
    cam_gpio_set(0);                                        /* PWDN 핀 : 센서 power down */
    mipi_rx_reset();                                        /* D-PHY / CSI-2 리셋 유지 */
    XAxiVdma_DmaStop(&vdma, XAXIVDMA_WRITE);               /* 카메라 -> DDR */
    XAxiVdma_DmaStop(&vdma, XAXIVDMA_READ);                /* DDR -> HDMI */
    DisplayStop(&dispCtrl);                                 /* VTC 정지 */
    DDynClk_Disable(&dispCtrl.dynClk);                      /* 픽셀 클럭 MMCM 정지 */

    video_stopped = 1;
    xil_printf("power: camera / MIPI / VDMA / HDMI stopped (reset the board to restore)\r\n");
}

/*===========================================================================
 *  PL done 대기
 *
 *  PL_DONE_IRQ  : cnn_ctrl 의 ISR 이 결과를 받고, cnn_ctrl_get_result() 로 꺼냄
 *  PL_DONE_POLL : IRQ 가 안 올 때의 대안. GIC 에서 cnn IRQ 를 막고 SR 을 직접 읽음
 *===========================================================================*/
static int pl_wait(u8 *cls, int sleep)
{
    const XTime tmo = (XTime)(COUNTS_PER_SECOND / 1000u) * POWER_MEAS_PL_TIMEOUT_MS;
    XTime t0;
    XTime t;

    XTime_GetTime(&t0);

    if (pl_mode == PL_DONE_POLL) {
        while ((Xil_In32(CNN_BASEADDR + CNN_SR_OFFSET) & CNN_SR_DONE) == 0u) {
            XTime_GetTime(&t);
            if (t - t0 > tmo) {
                return 0;
            }
        }
        *cls = (u8)(Xil_In32(CNN_BASEADDR + CNN_RDR_OFFSET) & CNN_RESULT_MASK);
        return 1;
    }

    if (!sleep) {
        while (!cnn_ctrl_get_result(cls)) {
            XTime_GetTime(&t);
            if (t - t0 > tmo) {
                return 0;
            }
        }
        return 1;
    }

    /*
     * CPU 에서 IRQ 를 막은 채로 확인하고 WFI. 마스크된 IRQ 도 WFI 는 깨우므로
     * 확인과 WFI 사이에 done 이 와도 놓치지 않는다. 깨어난 뒤 잠깐 IRQ 를 열어
     * cnn_isr (또는 timer ISR) 를 돌린다.
     */
    Xil_ExceptionDisable();
    while (!cnn_ctrl_get_result(cls)) {
        XTime_GetTime(&t);
        if (t - t0 > tmo) {
            Xil_ExceptionEnable();
            return 0;
        }
        __asm__ volatile ("wfi");
        Xil_ExceptionEnable();
        Xil_ExceptionDisable();
    }
    Xil_ExceptionEnable();
    return 1;
}

static void pl_drain(void)
{
    u8 c;

    while (cnn_ctrl_get_result(&c)) {
    }
    (void)Xil_In32(CNN_BASEADDR + CNN_SR_OFFSET);   /* 남은 done_flag 정리 */
}


/*===========================================================================
 *  상태 하나 실행
 *===========================================================================*/
static void meas_run_state(Meas_state st, u32 settle_ms, u32 meas_ms, Meas_run *r)
{
    XTime t0 = 0;
    XTime t1;
    u32 n = 0;
    u32 n0 = 0;
    u32 k = 0;          /* 70 장을 순서대로 돌림 */
    int started = 0;
    u8 c;

    memset(r, 0, sizeof(*r));
    meas_begin(settle_ms, meas_ms);

    switch (st) {
    case ST_WFI:
        while (!meas_done()) {
            __asm__ volatile ("wfi");
        }
        break;

    case ST_BUSY:
        while (!meas_done()) {
        }
        break;

    case ST_CPU_CNN:
        while (!meas_done()) {
            if (!started && meas_in_window()) {
                XTime_GetTime(&t0);
                n0 = n;
                started = 1;
            }
            cpu_preprocess(ts_img[k], cpu_in);
            if (cpu_cnn_run(cpu_in, NULL) != (int)cpu_ref[k]) {
                r->cls_err++;
            }
            k = (k + 1u == TESTSET_N) ? 0u : k + 1u;
            n++;
        }
        break;

    case ST_PL_CNN:
    case ST_PL_CNN_WFI:
        while (!meas_done()) {
            if (!started && meas_in_window()) {
                XTime_GetTime(&t0);
                n0 = n;
                started = 1;
            }
            if (roi_dma_send_image(ts_img[k]) != XST_SUCCESS ||
                !pl_wait(&c, st == ST_PL_CNN_WFI)) {
                r->cls_err++;
                break;
            }
            if (c != pl_ref[k]) {
                r->cls_err++;
            }
            k = (k + 1u == TESTSET_N) ? 0u : k + 1u;
            n++;
        }
        while (!meas_done()) {          /* 실패로 빠져나온 경우에도 구간 길이는 유지 */
        }
        break;

    case ST_PL_KICK_WFI:
        /* 70 장은 측정 중 바뀌지 않으므로 flush 는 여기 한 번 (settle 구간 안) */
        Xil_DCacheFlushRange((INTPTR)ts_img, sizeof(ts_img));
        while (!meas_done()) {
            if (!started && meas_in_window()) {
                XTime_GetTime(&t0);
                n0 = n;
                started = 1;
            }
            if (roi_dma_start_image(ts_img[k]) != XST_SUCCESS || !pl_wait(&c, 1)) {
                r->cls_err++;
                break;
            }
            if (c != pl_ref[k]) {
                r->cls_err++;
            }
            k = (k + 1u == TESTSET_N) ? 0u : k + 1u;
            n++;
        }
        while (!meas_done()) {
        }
        break;

    default:
        break;
    }

    XTime_GetTime(&t1);
    smp_active = 0;

    r->n   = acc_n;
    r->sum = acc_sum;
    r->sq  = acc_sq;
    r->min = acc_min;
    r->max = acc_max;
    if (started) {
        r->infer = n - n0;
        r->ticks = t1 - t0;
    }
}

/*===========================================================================
 *  계산 / 출력
 *===========================================================================*/

/*
 * Zybo Z7 Reference Manual 1.4 (Figure 1.4.1)
 *   I [A] = ((X * 244 / 3900) - 0.8) / 52,   X = 12-bit 코드 = raw16 >> 4
 * 평균값은 >> 4 대신 / 16 으로 나눠 소수부를 살린다.
 */
static double raw_to_amp(double raw16)
{
    double x = raw16 / 16.0;

    return ((x * 244.0 / 3900.0) - 0.8) / 52.0;
}

static double meas_sqrt(double v)
{
    double x;
    int i;

    if (v <= 0.0) {
        return 0.0;
    }
    x = (v > 1.0) ? v : 1.0;
    for (i = 0; i < 60; i++) {
        x = 0.5 * (x + v / x);
    }
    return x;
}

/* xil_printf 에는 %f 가 없어서 고정 소수점으로 찍는다 */
static void print_fix(double v, int dec)
{
    static const u32 p10[] = { 1u, 10u, 100u, 1000u, 10000u };
    u64 s;
    u32 d;
    u32 f;

    if (v < 0.0) {
        xil_printf("-");
        v = -v;
    }
    s = (u64)(v * (double)p10[dec] + 0.5);
    xil_printf("%u", (unsigned)(s / p10[dec]));
    if (dec > 0) {
        f = (u32)(s % p10[dec]);
        xil_printf(".");
        for (d = p10[dec] / 10u; d > 0u; d /= 10u) {
            xil_printf("%u", (unsigned)((f / d) % 10u));
        }
    }
}

static u32 ticks_to_ms(u64 t)
{
    return (u32)(t / (COUNTS_PER_SECOND / 1000u));
}

static void print_run(u32 round, Meas_state st, u32 t_ms, const Meas_run *r)
{
    double avg = r->n ? (double)r->sum / r->n : 0.0;
    double var = r->n ? (double)r->sq / r->n - avg * avg : 0.0;
    double amp = raw_to_amp(avg);
    double us  = (double)r->ticks * 1e6 / COUNTS_PER_SECOND;
    u32 k;

    for (k = 0; k < trc_n; k++) {
        xil_printf("TRC,%u,%s,%u,", (unsigned)round, st_name[st],
                   (unsigned)(t_ms + POWER_MEAS_SETTLE_MS + (k + 1u) * POWER_MEAS_TRACE_MS));
        print_fix((double)trc_buf[k] / TRACE_N, 2);
        xil_printf("\r\n");
    }

    xil_printf("RUN,%u,%s,%u,%u,", (unsigned)round, st_name[st],
               (unsigned)t_ms, (unsigned)r->n);
    print_fix(avg, 2);              xil_printf(",");
    print_fix(meas_sqrt(var), 2);   xil_printf(",%u,%u,", (unsigned)r->min, (unsigned)r->max);
    print_fix(amp * 1e3, 3);        xil_printf(",");
    print_fix(amp * POWER_MEAS_VIN_MV, 2);
    xil_printf(",%u,", (unsigned)r->infer);
    print_fix(us, 0);               xil_printf(",");
    print_fix(r->infer ? us / r->infer : 0.0, 2);
    xil_printf(",%u\r\n", (unsigned)r->cls_err);
}

static void print_summary(const Meas_agg *agg)
{
    double p_mw[ST_NUM];
    double us_inf[ST_NUM];
    double p_idle;
    double e_cpu = 0.0;
    double de_cpu = 0.0;
    int s;

    for (s = 0; s < ST_NUM; s++) {
        double avg = agg[s].n ? (double)agg[s].sum / agg[s].n : 0.0;

        p_mw[s]   = raw_to_amp(avg) * POWER_MEAS_VIN_MV;
        us_inf[s] = agg[s].infer
                  ? (double)agg[s].ticks * 1e6 / COUNTS_PER_SECOND / agg[s].infer : 0.0;
    }
    p_idle = p_mw[ST_WFI];

    xil_printf("\r\n#SUM,state,runs,samples,raw16_avg,current_mA,power_mW,dP_mW,"
               "infer,us_per_infer,energy_uJ,dEnergy_uJ,cls_err\r\n");
    for (s = 0; s < ST_NUM; s++) {
        double avg;

        if (agg[s].runs == 0u) {
            continue;
        }
        avg = (double)agg[s].sum / agg[s].n;
        xil_printf("SUM,%s,%u,%u,", st_name[s], (unsigned)agg[s].runs, (unsigned)agg[s].n);
        print_fix(avg, 2);                              xil_printf(",");
        print_fix(raw_to_amp(avg) * 1e3, 3);            xil_printf(",");
        print_fix(p_mw[s], 2);                          xil_printf(",");
        print_fix(p_mw[s] - p_idle, 2);
        xil_printf(",%u,", (unsigned)agg[s].infer);
        print_fix(us_inf[s], 2);                        xil_printf(",");
        print_fix(p_mw[s] * us_inf[s] / 1e3, 3);        xil_printf(",");
        print_fix((p_mw[s] - p_idle) * us_inf[s] / 1e3, 3);
        xil_printf(",%u\r\n", (unsigned)agg[s].cls_err);
    }

    if (agg[ST_CPU_CNN].infer == 0u) {
        return;
    }
    e_cpu  = p_mw[ST_CPU_CNN] * us_inf[ST_CPU_CNN] / 1e3;
    de_cpu = (p_mw[ST_CPU_CNN] - p_idle) * us_inf[ST_CPU_CNN] / 1e3;

    for (s = ST_PL_CNN; s <= ST_PL_KICK_WFI; s++) {
        double e_pl, de_pl;

        if (agg[s].infer == 0u) {
            continue;
        }
        e_pl  = p_mw[s] * us_inf[s] / 1e3;
        de_pl = (p_mw[s] - p_idle) * us_inf[s] / 1e3;
        xil_printf("CMP,CPU_CNN vs %s : speedup x", st_name[s]);
        print_fix(us_inf[ST_CPU_CNN] / us_inf[s], 2);
        xil_printf(", board energy x");
        print_fix(e_pl > 0.0 ? e_cpu / e_pl : 0.0, 2);
        xil_printf(", incremental energy x");
        print_fix(de_pl > 0.0 ? de_cpu / de_pl : 0.0, 2);
        xil_printf("\r\n");
    }
}

static int meas_abort_requested(void)
{
    return XUartPs_IsReceiveData(STDIN_BASEADDRESS) &&
           (XUartPs_RecvByte(STDIN_BASEADDRESS) == 'q');
}

/*===========================================================================
 *  입력 : RTL 검증과 같은 70 장 (cpu_cnn_testset.h, EMNIST M..Z 5 장씩)
 *
 *  testset 은 이미 전처리된 28x28 pixel_in 이라 PL 의 DMA 경로
 *  (112x112 RGB -> img_preprocess -> cnn_ip) 에 그대로 넣을 수 없다. 그래서
 *  흰 종이 위 검은 글자로 되돌린다.
 *      p = round(pixel_in * 255 / 2^14)   pixel_in = round(p / 255 * 2^14) 의 역 (정확히 복원)
 *      28x28 -> 112x112                   한 픽셀을 4x4 로
 *      R = G = B = 255 - p
 *  img_preprocess 의 grayscale (77R + 150G + 29B + 128) >> 8 은 R = G = B 이면 값이
 *  그대로이고, 반전하면 p 가 된다. 다만 threshold / bbox / 22 px 맞춤을 다시 거치므로
 *  CNN 입력은 원래 28x28 과 다르다. 그래서 기대값은 testset_class 가 아니라 같은
 *  112 이미지로 돌린 CPU 모델 결과 (cpu_ref) 이고, 정답률은 testset_label 로 본다.
 *===========================================================================*/
static void ts_build_images(void)
{
    u32 n;
    u32 x;
    u32 y;
    u8 *dst;
    u8 v;

    for (n = 0; n < TESTSET_N; n++) {
        dst = ts_img[n];
        for (y = 0; y < ROI_DMA_HEIGHT; y++) {
            for (x = 0; x < ROI_DMA_WIDTH; x++) {
                u32 pin = (u32)testset_img[n][(y / TS_SCALE) * CPU_CNN_IMG_W + (x / TS_SCALE)];

                v = (u8)(255u - ((pin * 255u + 8192u) >> 14));
                dst[0] = v;
                dst[1] = v;
                dst[2] = v;
                dst += ROI_DMA_RGB_BYTES;
            }
        }
    }
    /* CPU 가 쓴 2.6 MB 를 DDR 로. 이후로는 아무도 쓰지 않으니 DMA 가 바로 읽어도 됨 */
    Xil_DCacheFlushRange((INTPTR)ts_img, sizeof(ts_img));
}

/* 확인 단계의 PL 대기. 첫 장에서 IRQ 가 안 오면 SR polling 으로 바꾼다. */
static int ts_wait_done(u8 *cls, XTime *done)
{
    if (pl_mode == PL_DONE_IRQ) {
        if (pl_wait(cls, 0)) {
            *done = cnn_ctrl_result_time();      /* cnn_isr 진입 시각 = cnn_done */
            return 1;
        }
        if ((Xil_In32(CNN_BASEADDR + CNN_SR_OFFSET) & CNN_SR_DONE) == 0u) {
            return 0;
        }
        XTime_GetTime(done);
        *cls = (u8)(Xil_In32(CNN_BASEADDR + CNN_RDR_OFFSET) & CNN_RESULT_MASK);
        pl_mode = PL_DONE_POLL;
        XScuGic_DisableIntr(GIC_DIST_BASEADDR, CNN_IRQ_ID);
        xil_printf("power: WARNING cnn IRQ did not fire, falling back to SR polling "
                   "(PL_CNN_WFI / PL_KICK_WFI are skipped)\r\n");
        return 1;
    }
    if (pl_wait(cls, 0)) {
        XTime_GetTime(done);
        return 1;
    }
    return 0;
}

static void print_avg_us(const char *label, u64 sum_ticks)
{
    cnn_ctrl_print_us(label, 0, sum_ticks / TESTSET_N);
}

/*
 * 70 장을 한 장씩 CPU 와 PL 로 돌려 class 를 비교하고, 구간별 시간의 평균을 낸다
 * ('d' 키 출력과 같은 구간 정의). 측정 단계의 기대값 cpu_ref / pl_ref 도 여기서 정해진다.
 */
static int ts_check(void)
{
    Cpu_bench_result cb;
    const Roi_dma_times *t;
    XTime done;
    u64 s_pre   = 0;
    u64 s_cnn   = 0;
    u64 s_flush = 0;
    u64 s_dma   = 0;
    u64 s_comp  = 0;
    u32 n;
    u8 c;

    pl_drain();
    pl_mode    = PL_DONE_IRQ;
    ts_match   = 0;
    ts_cpu_hit = 0;
    ts_pl_hit  = 0;

    xil_printf("#IMG,idx,label,cpu_cls,pl_cls,cpu_us,pl_kick_us\r\n");
    for (n = 0; n < TESTSET_N; n++) {
        cpu_bench_roi(ts_img[n]);
        cpu_bench_take(&cb);
        cpu_ref[n] = (u8)cb.cls;

        if (roi_dma_send_image(ts_img[n]) != XST_SUCCESS || !ts_wait_done(&c, &done)) {
            xil_printf("power: image %d: PL CNN did not finish. Check that the "
                       "bitstream has the latched SR / intr.\r\n", (int)n);
            return XST_FAILURE;
        }
        t = roi_dma_last_times();
        pl_ref[n] = c;

        s_pre   += cb.pre_ticks;
        s_cnn   += cb.cnn_ticks;
        s_flush += t->kick - t->flush;
        s_dma   += t->dma_done - t->kick;
        s_comp  += done - t->dma_done;

        ts_match   += (cpu_ref[n] == pl_ref[n]);
        ts_cpu_hit += (cpu_ref[n] == testset_label[n]);
        ts_pl_hit  += (pl_ref[n] == testset_label[n]);

        xil_printf("IMG,%d,%c,%c,%c,", (int)n, 'A' + testset_label[n],
                   'A' + cpu_ref[n], 'A' + pl_ref[n]);
        print_fix((double)(cb.pre_ticks + cb.cnn_ticks) * 1e6 / COUNTS_PER_SECOND, 2);
        xil_printf(",");
        print_fix((double)(done - t->kick) * 1e6 / COUNTS_PER_SECOND, 2);
        xil_printf("%s\r\n", (cpu_ref[n] == pl_ref[n]) ? "" : ",MISMATCH");
    }

    xil_printf("\r\npower: testset %d images (EMNIST M..Z x 5, 28x28 -> 112x112, "
               "dark glyph on white)\r\n", TESTSET_N);
    xil_printf("  CPU vs PL class : %u / %d match\r\n", (unsigned)ts_match, TESTSET_N);
    xil_printf("  accuracy        : CPU %u / %d, PL %u / %d\r\n",
               (unsigned)ts_cpu_hit, TESTSET_N, (unsigned)ts_pl_hit, TESTSET_N);
    xil_printf("TIME (one inference per image, mean of %d images):\r\n", TESTSET_N);
    xil_printf("CPU (ARM A9):\r\n");
    print_avg_us("preprocess      :", s_pre);
    print_avg_us("CNN             :", s_cnn);
    print_avg_us("CPU total       :", s_pre + s_cnn);
    xil_printf("PL  (PS hand-off -> CNN done):\r\n");
    print_avg_us("cache flush     :", s_flush);
    print_avg_us("DMA 112x112 in  :", s_dma);
    print_avg_us("preprocess+CNN  :", s_comp);
    print_avg_us("PL total (kick) :", s_dma + s_comp);
    print_avg_us("total (flush)   :", s_flush + s_dma + s_comp);
    xil_printf("  speed-up        : x");
    print_fix((double)(s_pre + s_cnn) / (double)(s_dma + s_comp), 2);
    xil_printf(" (CPU total / PL kick), x");
    print_fix((double)(s_pre + s_cnn) / (double)(s_flush + s_dma + s_comp), 2);
    xil_printf(" (CPU total / PL flush)\r\n\r\n");
    return XST_SUCCESS;
}

/*===========================================================================
 *  public
 *===========================================================================*/
void power_meas_run(void)
{
    static Meas_agg agg[ST_NUM];
    Meas_run r;
    XTime t_origin;
    XTime t_now;
    u32 round;
    u32 i;
    Meas_state st;

    xil_printf("\r\npower: board-level power measurement (IMON -> XADC VP/VN)\r\n");

    if (!hw_ready && meas_hw_init() != XST_SUCCESS) {
        return;
    }
    /* 확인 단계의 시간도 카메라 DDR 트래픽 없이 재도록 영상 경로부터 세운다 */
    if (!video_stopped) {
        meas_stop_video();
    }
    ts_build_images();
    if (ts_check() != XST_SUCCESS) {
        xil_printf("power: testset check failed, aborted\r\n");
        return;
    }

    xil_printf("CFG,vin_mV=%u,sample_hz=%u,xadc_avg=256,settle_ms=%u,state_ms=%u,"
               "rounds=%u,cpu_hz=%u,pl_done=%s,input=testset%d,match=%u,"
               "cpu_hit=%u,pl_hit=%u\r\n",
               (unsigned)POWER_MEAS_VIN_MV, (unsigned)POWER_MEAS_SAMPLE_HZ,
               (unsigned)POWER_MEAS_SETTLE_MS, (unsigned)POWER_MEAS_STATE_MS,
               (unsigned)POWER_MEAS_ROUNDS,
               (unsigned)XPAR_CPU_CORTEXA9_0_CPU_CLK_FREQ_HZ,
               (pl_mode == PL_DONE_IRQ) ? "irq" : "poll", TESTSET_N,
               (unsigned)ts_match, (unsigned)ts_cpu_hit, (unsigned)ts_pl_hit);

    xil_printf("power: warm-up %u s in WFI ... (press q between states to abort)\r\n",
               (unsigned)(POWER_MEAS_WARMUP_MS / 1000u));
    meas_run_state(ST_WFI, POWER_MEAS_WARMUP_MS, 0u, &r);

    if (pl_mode == PL_DONE_POLL) {
        XScuGic_DisableIntr(GIC_DIST_BASEADDR, CNN_IRQ_ID);
    }

    memset(agg, 0, sizeof(agg));
    XTime_GetTime(&t_origin);
    xil_printf("#RUN,round,state,t_ms,samples,raw16_avg,raw16_std,raw16_min,raw16_max,"
               "current_mA,power_mW,infer,time_us,us_per_infer,cls_err\r\n");
    xil_printf("#TRC,round,state,t_ms,raw16_avg\r\n");

    for (round = 1u; round <= POWER_MEAS_ROUNDS; round++) {
        for (i = 0; i < ROUND_LEN; i++) {
            st = round_seq[i];
            if ((st == ST_PL_CNN_WFI || st == ST_PL_KICK_WFI) && pl_mode != PL_DONE_IRQ) {
                continue;
            }
            if (meas_abort_requested()) {
                xil_printf("power: aborted by user\r\n");
                goto done;
            }

            XTime_GetTime(&t_now);
            meas_run_state(st, POWER_MEAS_SETTLE_MS, POWER_MEAS_STATE_MS, &r);
            print_run(round, st, ticks_to_ms(t_now - t_origin), &r);

            agg[st].runs++;
            agg[st].n       += r.n;
            agg[st].sum     += r.sum;
            agg[st].infer   += r.infer;
            agg[st].ticks   += r.ticks;
            agg[st].cls_err += r.cls_err;
        }
    }

done:
    if (pl_mode == PL_DONE_POLL) {
        (void)Xil_In32(CNN_BASEADDR + CNN_SR_OFFSET);
        XScuGic_EnableIntr(GIC_DIST_BASEADDR, CNN_IRQ_ID);
    }
    pl_drain();

    print_summary(agg);
    xil_printf("power: done. power_mW assumes Vin = %u mV; rescale with the DMM value.\r\n",
               (unsigned)POWER_MEAS_VIN_MV);
}
