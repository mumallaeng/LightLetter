#ifndef SRC_POWER_MEAS_POWER_MEAS_H_
#define SRC_POWER_MEAS_POWER_MEAS_H_

/*
 * 보드 전체 입력 전력 측정 (docs/zybo_z7_20_cnn_power_measurement.md 절차).
 *
 *   TPS25940 eFuse IMON -> XADC VP/VN (전용 아날로그 입력) -> PS-XADC 인터페이스 (XAdcPs)
 *   PL 쪽에 XADC IP 를 넣지 않는다 (넣으면 PS-XADC 접근과 충돌).
 *
 * power_meas_run()
 *   1. 카메라 -> MIPI -> VDMA -> HDMI 영상 경로를 소프트웨어로 정지 (다시 켜지 않음)
 *   2. RTL 검증과 같은 70 장 (cpu_cnn_testset.h) 을 112x112 로 펼쳐 한 장씩
 *      CPU / PL 로 돌려 class 비교 + 구간별 평균 시간 (IMG 줄, TIME 표)
 *   3. 상태를 번갈아 돌리며 상태마다 평균 보드 전력과 inference 당 시간/에너지를 측정
 *        WFI -> BUSY -> CPU_CNN -> WFI -> PL_CNN -> PL_CNN_WFI -> PL_KICK_WFI -> WFI
 *        (x POWER_MEAS_ROUNDS)
 *        PL_CNN / PL_CNN_WFI : 매회 memcpy + cache flush + DMA (end-to-end)
 *        PL_KICK_WFI         : flush 한 번, 매회 DMA kick 만 (pure accelerator)
 *        CPU / PL 상태는 70 장을 순서대로 돌려 가며 처리
 *   4. UART 로 CSV 출력 (IMG / RUN / TRC / SUM 줄). tx/ui/power_meas_capture.py 가 파싱
 *
 * 측정 구간에는 UART 출력을 하지 않는다 (xil_printf 는 polling 이라 그 자체가 CPU 부하).
 * 카메라/HDMI 를 되살리려면 보드를 리셋할 것.
 */

void power_meas_run(void);

#endif /* SRC_POWER_MEAS_POWER_MEAS_H_ */
