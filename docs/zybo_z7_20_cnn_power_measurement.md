# Zybo Z7-20 PS / PL CNN 전력 측정 정리

## 1. 실험 목적

Zybo Z7-20 보드에서 동일한 CNN 연산을 다음 두 방식으로 수행했을 때의 전력 및 에너지 효율을 비교한다.

1. **PS CPU 기반 CNN 연산**
   - Zynq-7000 PS 내부 ARM Cortex-A9에서 C 코드로 CNN 수행
2. **PL CNN accelerator 기반 CNN 연산**
   - PS는 입력 전달 및 accelerator 제어만 수행
   - 실제 CNN 연산은 PL에 구현한 CNN accelerator에서 수행

단순 소비전력뿐 아니라 **inference 1회당 에너지**까지 비교하는 것이 목표다.

---

## 2. 대상 보드

- Board: **Digilent Zybo Z7-20**
- Device: **Xilinx Zynq-7000 XC7Z020**
- PS CPU: **Dual-core ARM Cortex-A9**
- PL: Artix-7 계열 programmable logic
- 입력 전원: 일반적으로 5 V

---

## 3. 중요한 전제

PS에서 C 코드가 "아무 일도 하지 않는 상태"와 CNN을 계속 수행하는 상태의 전력은 동일하지 않다.

CMOS 동적 전력은 개념적으로 다음과 같이 표현된다.

\[
P_{dynamic} \approx \alpha C V^2 f
\]

- \(\alpha\): switching activity
- \(C\): effective switched capacitance
- \(V\): supply voltage
- \(f\): clock frequency

CNN 연산을 수행하면 다음 블록들의 switching activity가 증가한다.

- ALU
- multiplier / MAC 관련 datapath
- register
- cache
- AXI bus
- DDR memory interface
- instruction fetch / decode logic

따라서 일반적으로 CPU가 실제 CNN 연산을 수행할 때의 전력은 idle 상태보다 증가한다.

---

# 4. "아무 일도 하지 않는 상태"의 정의

실험에서 가장 주의해야 할 부분이다.

## 4.1 Busy loop

```c
while (1)
{
}
```

이 상태는 진정한 idle 상태가 아니다.

CPU가 지속적으로 다음 동작을 수행한다.

- instruction fetch
- branch execution
- pipeline operation

따라서 CPU 내부 회로가 계속 동작한다.

---

## 4.2 WFI 상태

ARM Cortex-A9의 `WFI`(Wait For Interrupt)를 사용할 수 있다.

예:

```c
while (1)
{
    __asm__("wfi");
}
```

또는 toolchain에서 제공하는 intrinsic을 사용한다.

WFI 상태에서는 CPU의 activity가 크게 감소한다.

따라서 일반적인 전력 관계는 다음과 같이 예상할 수 있다.

\[
P_{WFI} < P_{busy-loop} < P_{CNN}
\]

단, 실제 값은 다음 조건에 따라 달라질 수 있다.

- CPU clock
- voltage
- cache hit/miss
- DDR access 빈도
- compiler optimization
- NEON 사용 여부
- interrupt 발생 여부
- 주변 장치 동작 상태

---

# 5. 권장 실험 조건

전력 측정 시 다음 4개 상태를 비교한다.

## Case 0. PS true idle

CPU가 WFI 상태로 진입.

목적:

- 시스템의 최소 baseline power 측정

측정값:

\[
P_{idle}
\]

---

## Case 1. PS busy loop

```c
while (1)
{
}
```

목적:

- CPU는 active하지만 유효 연산을 수행하지 않는 상태 확인
- WFI와 단순 C busy loop의 차이를 확인

측정값:

\[
P_{busy}
\]

---

## Case 2. PS CPU CNN

CPU에서 동일한 CNN inference를 반복 수행한다.

예:

```c
while (1)
{
    cnn_inference(input, output);
}
```

측정값:

\[
P_{CPU}
\]

CPU CNN에 의해 증가한 시스템 전력:

\[
\Delta P_{CPU} = P_{CPU} - P_{idle}
\]

---

## Case 3. PL CNN accelerator

PS는 다음 역할만 수행한다.

- 입력 data 준비
- cache flush
- DMA transfer
- accelerator start
- done signal 확인
- 결과 확인

실제 CNN 연산은 PL에서 수행한다.

측정값:

\[
P_{PL}
\]

PL accelerator 사용으로 증가한 시스템 전력:

\[
\Delta P_{PL} = P_{PL} - P_{idle}
\]

---

# 6. Zybo Z7-20의 전력 측정 방법

Zybo Z7 보드에는 TPS25940 eFuse의 `IMON` 신호가 있으며, 이 신호를 통해 보드 입력 전류를 모니터링할 수 있다.

IMON은 Zynq-7000의 XADC dedicated analog input에 연결되어 있다.

따라서 별도의 외부 current sensor 없이도 XADC로 보드 전체 입력 전류를 측정할 수 있다.

주의:

이 값은 PS 또는 PL만의 전력이 아니라 **보드 전체 입력 전력**이다.

포함되는 요소 예:

- Zynq PS
- Zynq PL
- DDR3L
- ethernet PHY
- USB 관련 회로
- HDMI 관련 회로
- regulator loss
- LED
- 기타 주변회로

따라서 결과는 다음과 같이 표현하는 것이 정확하다.

> Board-level power consumption during CPU CNN inference

또는

> Board-level power consumption during PL-accelerated CNN inference

PS-only power라고 단정하면 안 된다.

---

# 7. XADC를 통한 전류 계산

Zybo Z7 reference manual에 제시된 변환식:

\[
I=
\frac{
\left(
\frac{X\times24.4}{3900}
\right)-0.8
}{52}
\]

- \(X\): XADC 12-bit ADC code
- \(I\): board input current [A]

XADC data register는 일반적으로 16-bit 형식으로 읽히므로 실제 ADC 값은 다음과 같이 얻는다.

```c
uint16_t raw16;
uint16_t x;

raw16 = XAdcPs_GetAdcData(...);
x = raw16 >> 4;
```

그 후 `x`를 위 전류 변환식에 사용한다.

---

# 8. 전력 계산

입력 전압을 \(V_{in}\), 입력 전류를 \(I\)라고 하면:

\[
P = V_{in} I
\]

5 V 공급을 가정하면:

\[
P \approx 5I
\]

하지만 실험 보고서나 논문에서는 실제 보드 입력단 전압을 DMM으로 측정하는 것이 더 좋다.

예:

\[
V_{in}=5.04\,V
\]

이면:

\[
P=5.04I
\]

---

# 9. 평균 전력 측정

CNN inference는 매우 짧기 때문에 1회만 실행해서 XADC로 측정하면 정확도가 떨어질 수 있다.

예를 들어 PL inference latency가 약 260 us라면 단일 inference 전력 transient를 XADC averaging 결과로 정확하게 잡기 어렵다.

따라서 inference를 반복 실행한다.

예:

```c
for (int i = 0; i < 10000; i++)
{
    cnn_inference();
}
```

CPU와 PL 모두 일정 시간 동안 연산이 계속 유지되도록 한다.

권장 측정 시간:

- 최소 수 초
- 가능하면 5~10초 이상

각 상태에서 XADC 값을 반복 sampling한 뒤 평균을 계산한다.

\[
P_{avg}
=
\frac{1}{N}
\sum_{k=0}^{N-1} P[k]
\]

---

# 10. XADC averaging

Zybo Z7 reference manual에서는 정확도를 위해 256-sample averaging 사용을 권장한다.

가능하면 다음 설정을 사용한다.

- XADC averaging: 256 samples
- 동일한 sampling 조건 유지
- 모든 실험에서 동일한 averaging 옵션 사용

---

# 11. 전력보다 중요한 지표: Energy per inference

FPGA accelerator는 CPU보다 순간 소비전력이 높을 수 있다.

하지만 accelerator가 훨씬 빠르면 inference 1회에 필요한 총 에너지는 더 작아질 수 있다.

에너지는:

\[
E=P\times t
\]

으로 계산한다.

CPU:

\[
E_{CPU/inference}
=
P_{CPU}\times t_{CPU}
\]

PL:

\[
E_{PL/inference}
=
P_{PL}\times t_{PL}
\]

단위 예:

- power: W
- time: ms
- energy: mJ

---

# 12. 현재 성능 측정값

기존 측정 결과:

## CPU

동일한 112 x 112 ROI에 대해:

- preprocess: 약 492.518 us
- CNN: 약 2097.188 us
- CPU total: 약 2589.707 us

## PL

- cache flush: 약 93.212 us
- DMA 112 x 112 input: 약 126.323 us
- preprocess + CNN: 약 134.204 us
- PL kick total: 약 260.528 us
- flush 포함 total: 약 353.741 us

CPU total 대비 PL kick 기준 speed-up:

\[
Speedup
=
\frac{2589.707}{260.528}
\approx
9.94
\]

따라서 PL의 instantaneous power가 CPU보다 높더라도 energy per inference에서는 큰 이득이 발생할 가능성이 있다.

---

# 13. 예시

가상의 측정 결과:

| Mode | Board power |
|---|---:|
| WFI idle | 1.55 W |
| Busy loop | 1.70 W |
| CPU CNN | 2.02 W |
| PL CNN | 2.20 W |

CPU CNN:

\[
E_{CPU}
=
2.02
\times
2.589707\,ms
=
5.23\,mJ
\]

PL CNN:

\[
E_{PL}
=
2.20
\times
0.260528\,ms
=
0.573\,mJ
\]

energy efficiency improvement:

\[
\frac{E_{CPU}}{E_{PL}}
\approx
9.1
\]

즉 PL의 순간 소비전력이 CPU보다 높더라도 inference당 에너지는 크게 감소할 수 있다.

주의:

위 수치는 예시이며 실제 전력 측정값이 아니다.

---

# 14. 권장 실험 시퀀스

다음 순서로 측정하는 것을 권장한다.

```text
System boot
    ↓
thermal stabilization
    ↓
WFI idle
    ↓
busy loop
    ↓
CPU CNN repeat
    ↓
idle
    ↓
PL CNN accelerator repeat
    ↓
idle
```

각 상태를 충분히 긴 시간 유지한다.

예:

- WFI: 10 s
- busy loop: 10 s
- CPU CNN: 10 s
- idle: 10 s
- PL CNN: 10 s
- idle: 10 s

각 구간에서 다음 데이터를 UART 또는 memory buffer에 저장한다.

- timestamp
- XADC raw value
- converted current
- input voltage
- calculated power
- operating state

---

# 15. 측정 결과 정리 형식

최종 결과 표 예:

| Metric | WFI | Busy loop | CPU CNN | PL CNN |
|---|---:|---:|---:|---:|
| Average current [A] | | | | |
| Average board power [W] | | | | |
| Incremental power vs idle [W] | 0 | | | |
| Inference latency [ms] | - | - | | |
| Energy / inference [mJ] | - | - | | |
| Throughput [inference/s] | - | - | | |

추가적으로 계산:

\[
\Delta P_{CPU}=P_{CPU}-P_{idle}
\]

\[
\Delta P_{PL}=P_{PL}-P_{idle}
\]

\[
Energy\ improvement
=
\frac{E_{CPU}}{E_{PL}}
\]

\[
Energy\ reduction
=
\left(
1-
\frac{E_{PL}}{E_{CPU}}
\right)
\times100
\]

---

# 16. 실험 공정성을 위한 통제 조건

CPU와 PL을 비교할 때 아래 조건은 반드시 동일하게 유지한다.

## 입력 데이터

동일한 112 x 112 input image 또는 ROI를 사용한다.

## 결과

CPU와 PL inference 결과 class가 동일해야 한다.

현재 확인된 예:

- CPU class = 18
- PL class = 18

## clock

CPU clock 및 PL accelerator clock을 명확히 기록한다.

## peripherals

가능하면 불필요한 peripheral 상태를 동일하게 유지한다.

예:

- HDMI
- USB
- ethernet
- LEDs

## cache

CPU inference와 PL inference에서 cache flush 등 부가 작업을 어디까지 포함할 것인지 명확하게 정의한다.

---

# 17. 권장 비교 방법 두 가지

PL 경로의 성능은 두 가지 방식으로 평가하는 것이 좋다.

## Pure accelerator comparison

CPU:

\[
CPU\ CNN
\]

PL:

\[
DMA + accelerator
\]

또는 accelerator core 자체의 execution time만 사용.

목적:

- 계산 accelerator 자체 성능 비교

---

## End-to-end comparison

CPU:

\[
capture
+
ROI
+
preprocess
+
CNN
\]

PL:

\[
capture
+
ROI
+
DMA
+
accelerator
\]

목적:

- 실제 application-level 성능 비교

전력과 에너지 분석에서도 두 결과를 구분하면 좋다.

---

# 18. local AI가 구현해야 할 코드

향후 local AI가 작업할 경우 다음 코드를 작성하면 된다.

## 목표

Vitis / bare-metal 환경에서 다음 기능 구현:

1. XADC initialization
2. IMON channel reading
3. raw XADC data -> current conversion
4. current -> board power conversion
5. 256 averaging 설정
6. state별 반복 측정
7. UART 출력
8. CPU CNN 반복 실행
9. PL CNN 반복 실행
10. inference 횟수 및 실행시간 측정

---

## 권장 출력 형식

예:

```text
STATE,WFI
TIME_US,0
RAW,1234
CURRENT_A,0.315
POWER_W,1.588

STATE,CPU_CNN
TIME_US,100000
RAW,1510
CURRENT_A,0.401
POWER_W,2.021

STATE,PL_CNN
TIME_US,200000
RAW,1665
CURRENT_A,0.437
POWER_W,2.202
```

또는 CSV 형식:

```text
timestamp_us,state,raw,current_A,power_W
0,WFI,1234,0.315,1.588
1000,WFI,1235,0.316,1.593
...
```

이 형식이면 PC에서 Python으로 바로 분석 가능하다.

---

# 19. local AI에게 줄 구현 지시문

다음 요구사항을 유지할 것.

1. Target board는 Zybo Z7-20이다.
2. Zynq device는 XC7Z020이다.
3. Vitis bare-metal C 환경을 기준으로 한다.
4. Zybo Z7의 IMON 신호를 XADC로 읽는다.
5. CPU idle은 busy loop가 아니라 WFI를 baseline으로 사용한다.
6. busy loop도 별도 상태로 측정한다.
7. CPU CNN과 PL CNN을 동일 조건으로 반복 실행한다.
8. inference 1회가 너무 짧으므로 수천~수만 회 반복하여 steady-state power를 측정한다.
9. UART로 CSV 형태의 값을 출력한다.
10. 단순 power뿐 아니라 energy per inference까지 계산 가능하도록 time과 inference count를 기록한다.
11. 기존 CNN inference 함수를 임의로 수정하지 않는다.
12. 기존 accelerator control 함수도 가능한 그대로 사용한다.
13. 전력 측정 코드만 기존 코드 주변에 삽입하는 형태를 우선한다.
14. PS/PL 비교 시 동일 입력 이미지를 사용한다.
15. CPU와 PL의 output class가 동일한지 검증한다.
16. XADC 결과가 16-bit register format이면 실제 12-bit ADC code를 얻기 위해 필요한 bit shift를 확인한다.
17. current 변환식은 Zybo Z7 reference manual을 기준으로 구현한다.

---

# 20. 해석 시 주의사항

IMON 기반 측정값은 보드 전체 전력이다.

따라서 다음 표현은 피한다.

> CPU consumes exactly 2.0 W.

대신:

> The board consumed 2.0 W while CNN inference was executed on the Cortex-A9 CPU.

또는:

> Board-level power increased by 0.45 W relative to the idle state during CPU inference.

이 표현이 더 정확하다.

마찬가지로 PL 측정도:

> Board-level power consumption during PL-accelerated inference

라고 표현해야 한다.

---

# 21. 최종 실험 목적

궁극적으로 아래 세 가지를 비교한다.

### Performance

\[
Speedup =
\frac{t_{CPU}}{t_{PL}}
\]

### Power

\[
P_{CPU}
\quad vs \quad
P_{PL}
\]

### Energy efficiency

\[
E_{CPU}
=
P_{CPU}t_{CPU}
\]

\[
E_{PL}
=
P_{PL}t_{PL}
\]

특히 FPGA accelerator에서는 단순 전력보다 **energy per inference**가 중요한 평가 지표다.

PL의 power가 CPU보다 다소 높더라도 accelerator가 5~10배 이상 빠르면 전체 inference energy는 훨씬 낮아질 수 있다.

---

# 22. 참고 문헌 및 자료

## Digilent Zybo Z7 Reference Manual

확인할 내용:

- TPS25940 eFuse
- IMON current monitor
- XADC dedicated analog input
- current conversion equation
- XADC averaging recommendation

Official document:

Digilent, **Zybo Z7 Reference Manual**.

---

## Zynq-7000 CNN accelerator 관련 참고

Zynq-7000 기반 CNN accelerator 연구에서는 보통 다음 지표를 함께 사용한다.

- inference latency
- throughput
- power consumption
- GOP/s
- GOP/s/W
- energy efficiency

따라서 본 실험에서도 power와 latency를 결합한 energy per inference를 주요 지표로 사용하는 것이 적절하다.

---

# 23. 한 줄 요약

본 실험에서는 Zybo Z7-20의 XADC와 TPS25940 IMON을 이용하여 `WFI idle`, `busy loop`, `CPU CNN`, `PL CNN accelerator` 상태의 board-level power를 측정하고, 동일 CNN inference에 대해 latency와 energy per inference를 비교하여 PL accelerator의 실제 에너지 효율을 평가한다.
