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

static u8  meas_img[ROI_DMA_FRAME_BYTES] __attribute__((aligned(64)));
static s16 cpu_in[CPU_CNN_IMG_PIX];
static int cpu_cls;
static u8  pl_cls;
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
 *  입력 준비 + CPU / PL class 확인
 *===========================================================================*/
static int meas_prepare_input(void)
{
    u8 c;

    if (roi_dma_last_times()->kick == 0) {
        xil_printf("power: no ROI sent yet, using the synthetic test frame\r\n");
        if (roi_dma_send_test_frame() != XST_SUCCESS) {
            return XST_FAILURE;
        }
        usleep(10000);
    }
    /* 이전 측정의 PL_KICK_WFI 뒤에는 roi_dma_frame() 이 meas_img 자신을 가리킴 */
    if (roi_dma_frame() != meas_img) {
        memcpy(meas_img, roi_dma_frame(), ROI_DMA_FRAME_BYTES);
    }
    pl_drain();

    cpu_preprocess(meas_img, cpu_in);
    cpu_cls = cpu_cnn_run(cpu_in, NULL);

    pl_mode = PL_DONE_IRQ;
    if (roi_dma_send_image(meas_img) != XST_SUCCESS) {
        return XST_FAILURE;
    }
    if (pl_wait(&c, 0)) {
        xil_printf("power: PL done via cnn IRQ\r\n");
    } else if (Xil_In32(CNN_BASEADDR + CNN_SR_OFFSET) & CNN_SR_DONE) {
        c = (u8)(Xil_In32(CNN_BASEADDR + CNN_RDR_OFFSET) & CNN_RESULT_MASK);
        pl_mode = PL_DONE_POLL;
        xil_printf("power: WARNING cnn IRQ did not fire, falling back to SR polling "
                   "(PL_CNN_WFI / PL_KICK_WFI are skipped)\r\n");
    } else {
        xil_printf("power: PL CNN did not finish (no IRQ, SR done = 0). "
                   "Check that the bitstream has the latched SR / intr.\r\n");
        return XST_FAILURE;
    }
    pl_cls = c;

    xil_printf("power: check CPU class = %d, PL class = %d -> %s\r\n",
               cpu_cls, (int)pl_cls,
               (cpu_cls == (int)pl_cls) ? "MATCH" : "MISMATCH");
    return XST_SUCCESS;
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
            cpu_preprocess(meas_img, cpu_in);
            if (cpu_cnn_run(cpu_in, NULL) != cpu_cls) {
                r->cls_err++;
            }
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
            if (roi_dma_send_image(meas_img) != XST_SUCCESS ||
                !pl_wait(&c, st == ST_PL_CNN_WFI)) {
                r->cls_err++;
                break;
            }
            if (c != pl_cls) {
                r->cls_err++;
            }
            n++;
        }
        while (!meas_done()) {          /* 실패로 빠져나온 경우에도 구간 길이는 유지 */
        }
        break;

    case ST_PL_KICK_WFI:
        /* 입력이 바뀌지 않으므로 flush 는 여기 한 번 (settle 구간 안) */
        Xil_DCacheFlushRange((INTPTR)meas_img, ROI_DMA_FRAME_BYTES);
        while (!meas_done()) {
            if (!started && meas_in_window()) {
                XTime_GetTime(&t0);
                n0 = n;
                started = 1;
            }
            if (roi_dma_start_image(meas_img) != XST_SUCCESS || !pl_wait(&c, 1)) {
                r->cls_err++;
                break;
            }
            if (c != pl_cls) {
                r->cls_err++;
            }
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
    if (meas_prepare_input() != XST_SUCCESS) {
        xil_printf("power: input / PL check failed, aborted\r\n");
        return;
    }
    if (!video_stopped) {
        meas_stop_video();
    }

    xil_printf("CFG,vin_mV=%u,sample_hz=%u,xadc_avg=256,settle_ms=%u,state_ms=%u,"
               "rounds=%u,cpu_hz=%u,pl_done=%s,cpu_cls=%d,pl_cls=%d\r\n",
               (unsigned)POWER_MEAS_VIN_MV, (unsigned)POWER_MEAS_SAMPLE_HZ,
               (unsigned)POWER_MEAS_SETTLE_MS, (unsigned)POWER_MEAS_STATE_MS,
               (unsigned)POWER_MEAS_ROUNDS,
               (unsigned)XPAR_CPU_CORTEXA9_0_CPU_CLK_FREQ_HZ,
               (pl_mode == PL_DONE_IRQ) ? "irq" : "poll", cpu_cls, (int)pl_cls);

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
