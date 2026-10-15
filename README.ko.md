[English](README.md) | 한국어

> 이 브랜치는 6.1절 비교를 위해 FC 공유 엔진(DSP 20)을 남겨 둔 브랜치입니다. 최종 설계는 `main`에 있습니다.

# Light Letter — 손글씨를 인식해 빛으로 보내는 FPGA 광통신 시스템

> [컨소시엄] 온디바이스AI 시스템반도체 설계 2기 교육과정 최종 프로젝트입니다.

1팀 L-LINK 7인 · 2026.09.09~2026.10.15

| 팀원 | 파트 | 담당 업무 |
|:--|:--|:--|
| 조성재 (팀장) ([@seongjaejo](https://github.com/seongjaejo)) | CNN | Conv 연산, 이미지 전처리 설계 및 검증 |
| 김동현 ([@kimdh315](https://github.com/kimdh315)) | CNN | Conv 컨트롤러, Max Pooling, Zero Gating, CNN 통합과 IP 패키징 |
| 김연우 ([@mumallaeng](https://github.com/mumallaeng)) | CNN | Output Buffer, ReLU & Quantization, Fully Connected, ArUco 크롭, Web UI |
| 박종웅 ([@Critical-mankind](https://github.com/Critical-mankind)) | 광통신 | BFSK 송신부 설계 및 검증, 광 송수신 회로 |
| 변호연 ([@k948158](https://github.com/k948158)) | 광통신 | BFSK 수신부 설계 및 검증, UART 펌웨어 |
| 권재원 ([@kw0njw2222](https://github.com/kw0njw2222)) | FFT | FFT Buffer, Core 설계 및 검증 |
| 박경민 ([@gyeongminbag27-arch](https://github.com/gyeongminbag27-arch)) | FFT | FFT Core Butterfly, Power 설계 및 UVM 검증 |

<p align="center">
  <img src="assets/readme/fig32a.png" height="220" />
  <img src="assets/readme/fig32b.png" height="220" />
  <img src="assets/readme/fig32c.png" height="220" />
</p>
<p align="center">최종 시연: 수신부 UI(좌), 송수신부 사이 광 경로(중), 송신부 UI(우)</p>

# 1. 개요

## 1.1 목적 및 목표

무선 통신은 이동성, 설치 유연성, 확장성을 제공하지만 전파를 쓰는 한 전자파 간섭과 전파 확산에 따른 보안 문제가 따른다. Light Letter는 '무선이 꼭 전파여야 하는가'라는 질문에서 출발해, 손글씨를 인식하고 빛으로 전송하고 다시 복원하는 FPGA 기반 광통신 시스템을 목표로 했다.

화이트보드에 쓴 손글씨를 카메라로 촬영해 FPGA CNN 가속기로 인식하고, 인식한 문자를 레이저 빛에 실어 BFSK 방식으로 전송한 뒤, 수신 보드에서 FFT로 주파수를 판별해 문자를 복원한다. 손글씨 인식 AI(CNN), 광통신(BFSK), 신호처리(FFT) 세 기술을 Zybo Z7-20 보드 두 대 위에서 하나의 송수신 흐름으로 통합하는 것이 최종 목표다.

- 손글씨 전처리와 CNN 추론을 PL 가속기로 수행해 CPU 대비 연산 시간과 전력을 줄인다.
- 인식한 문자를 패킷으로 구성하고 BFSK 레이저 신호로 송신해, 비동기 수신 환경에서도 시작 시점과 오류를 판별한다.
- 128-point FFT로 수신 신호의 주파수 성분을 계산하고, CPU FFT 대비 실시간 처리 여유를 확보한다.
- 송신부와 수신부 UI에서 인식 결과, FFT 스펙트럼, 복원 문자를 함께 확인한다.

## 1.2 설계 범위

설계 범위에는 송신 보드의 카메라 입력과 ArUco 마커 기반 글자 영역 추출(PS), 이미지 전처리와 CNN 가속기(PL), 문자 데이터의 패킷 구성과 BFSK 광 송신(PL), 수신 보드의 광센서 입력과 XADC 샘플링, 128-point FFT와 Power 계산, BFSK 복조와 CRC-8 검증(PL), UART를 통한 PC UI 출력(PS)이 포함된다. 레이저 송신 회로와 광센서 수신 회로도 직접 구성했다.

여러 문자의 동시 전송, 오류 정정(FEC)과 재전송, 장거리·실외 환경 검증은 이번 범위에서 제외하고 개선 항목으로 남겼다.

## 1.3 프로젝트 요약

![프로젝트 구상도](assets/readme/fig01.png)

Light Letter는 '손글씨를 인식하고, 빛으로 전송하고, 다시 복원한다'는 개념으로 CNN, BFSK, FFT를 결합한 시스템이다. 송신부는 카메라 입력 → 문자 인식 → BFSK 변조 → 레이저 광 송신을, 수신부는 광 수신 → FFT 주파수 판별 → 문자 복원 → UI 출력을 맡는다.

활용 장면으로는 통신이 제한된 의료·산업 현장의 메모 전달, 외부 네트워크 없이 문자 정보를 공유해야 하는 보안 공간, 작성한 문자를 수신 장치로 보내는 수중 슬레이트를 상정했다.

## 1.4 설계 사양

| 항목 | 내용 |
|:--|:--|
| 보드 | Zybo Z7-20(Zynq-7020) 2대: 송신부, 수신부 |
| 동작 주파수 | PL 100 MHz, PS(ARM Cortex-A9) 667 MHz |
| 카메라 입력 | Digilent Pcam 5C, VDMA로 DDR 저장, ArUco 기반 112×112 ROI |
| CNN 입력 | 28×28 grayscale, 글자 비율 유지 최대 22×22, 중앙 배치 |
| CNN 구조 | Conv(3×3) 6채널 → Max Pool(2×2) → Conv(3×3) 16채널 → Max Pool(2×2) → FC 400→120→84→26 |
| 학습 | EMNIST 대문자 26 클래스, QAT(Quantization-Aware Training), 정확도 94.13% |
| 광 송신 | 레이저 모듈(2N7000 스위칭), BFSK |
| 광 수신 | 광센서 BPW34, 분압 회로, XADC(VAUX14, 12-bit) |
| BFSK 주파수 | bit 0 60.096 kHz, bit 1 97.656 kHz, SYNC 120.192 kHz |
| 샘플링 | 961.538 kS/s(100 MHz / 104), 1 심볼 = 256 샘플(266.24 µs) |
| 패킷 | Preamble(SYNC 4심볼) + SFD 0xD5 + Frame ID + Data + CRC-8(다항식 0x07) |
| FFT | 128-point Radix-2 DIT, 단일 Butterfly 재사용, 판별 Bin 8·13·16 |
| PC 출력 | PS7 UART1 115200 baud, CRLF로 끝나는 CSV |

## 1.5 AS-IS / TO-BE

| 구분 | AS-IS | TO-BE |
|:--|:--|:--|
| 통신 매체 | 전파 기반 무선은 간섭과 전파 확산에 따른 보안 문제가 있음 | 빛(레이저)을 매체로 쓰는 전파 비의존형 문자 전송 |
| 입력 방식 | 문자를 키보드로 입력하거나 사람이 읽어 전달 | 손글씨를 카메라로 촬영하면 FPGA CNN이 문자로 인식 |
| 인식 연산 | CPU에서 전처리와 CNN을 수행하면 지연과 전력 소모가 큼 | PL 가속기로 CPU 대비 6.22배 빠르고 전력 57.7% 감소 |
| 수신 판별 | 신호 세기만 보면 잡음과 비트 경계에서 판별이 흔들림 | FFT Power로 주파수를 판별하고 패킷·CRC로 오류 검출 |
| 신호처리 | CPU FFT는 실시간 처리 여유가 작음 | Pipeline FFT로 CPU 대비 7.08배 빠른 처리 |

# 2. 프로젝트 관리

## 2.1 일정

| 단계 | 기간 | 주요 작업 |
|:--|:--|:--|
| 주제 선정 | 08.28~09.10 | 팀 구성, 주제 의논과 가능성 검토, 프로젝트 주제 구체화 |
| 역할 분배 | 09.11 | CNN·광통신·FFT 파트와 개인별 담당 확정 |
| 설계 | 09.14~09.16 | CNN 설계 구체화, 패킷·BFSK 규격과 FFT 구조 정의 |
| 구현 | 09.17~09.27 | CNN 골든 모델과 RTL, Vitis C 코드, BFSK TX/RX RTL, FFT RTL 구현 |
| 통합 | 09.28~09.30 | 송신부·수신부 통합과 디버깅 |
| 개선 및 발표 준비 | 10.01~10.14 | 전처리·복조 알고리즘 개선, 고속 송수신 설정 적용(10.09), 발표 준비 |
| 최종 발표 | 10.15 | 최종 발표와 시연 |

## 2.2 역할 분담

| 파트 | 팀원 | 역할 | 담당 업무 |
|:--|:--|:--|:--|
| CNN | 조성재(팀장) | Conv 연산, 이미지 전처리 설계 및 검증 | 3채널 Line Buffer와 3×3 Window, 병렬 MAC Array와 덧셈 트리, 흑백 변환·극성 반전·배경 제거 전처리, Bounding Box 크롭과 28×28 정규화, DDR–DMA–전처리–CNN 연동 및 검증 |
| CNN | 김동현 | Conv 컨트롤러 설계 및 검증 | Convolution 연산 컨트롤러, Max Pooling layer, Zero Gating 기법, CNN 전체 통합, AXI4-Lite 기반 CNN IP 패키징 |
| CNN | 김연우 | Conv 출력 및 FC 설계 및 검증 | Convolution 출력 재정렬 Output Buffer, ReLU & Quantization, Fully Connected 연산 설계 및 최적화, ArUco 마커 검출과 크롭 C 구현, Web User Interface |
| 광통신 | 박종웅 | BFSK 송신부 설계 및 검증 | Frame 생성·CRC-8·BFSK 변조 RTL, TX FSM과 모듈 간 Handshake, AXI4-Lite 연동과 Vitis 송신 제어, 광 송수신 물리 회로 구성과 보드 간 통신 검증, RX-UI 연동과 FFT 스펙트럼 전송 |
| 광통신 | 변호연 | BFSK 수신부 설계 및 검증 | BFSK 수신 패킷 처리 RTL, CRC 검증 로직, FFT-RX RTL Top 통합, UART 송신 펌웨어와 PC 통신 규격, 발표자료 제작 |
| FFT | 권재원 | FFT Buffer, Core 설계 및 검증 | Ping-Pong Buffer 기반 입력 데이터 처리, FFT Core 데이터 흐름과 제어 로직, Memory Read/Write 구조, 128-point FFT Top 통합 및 동작 검증, 실물 외관과 발표자료 제작 |
| FFT | 박경민 | FFT Core, Power 설계 및 검증 | FFT Core Butterfly 연산 구조와 동작 검증, Twiddle Factor mem 파일 생성, FFT Power 연산 로직, Ping-Pong Buffer UVM, Butterfly UVM 검증 |

## 2.3 개발 및 설계 환경

| 분류 | 내용 |
|:--|:--|
| 언어 | Verilog(RTL), SystemVerilog·UVM(검증), C(Vitis 애플리케이션, 골든 모델), Python(QAT 학습, 기준 모델, PC UI) |
| FPGA | Zybo Z7-20(Zynq-7020) 2대 |
| 주변 하드웨어 | Pcam 5C 카메라, 레이저 모듈, 광센서 BPW34, 오실로스코프 |
| EDA | Vivado 2020.2, Vitis 2020.2 |
| 검증 | Vivado Simulator, UVM, Vivado ILA, Python·C 골든 모델 |
| 형상관리·협업 | Git, GitHub, Notion, Google Drive |
| 문서·편집 | PowerPoint, Word, draw.io, Visual Studio Code, MobaXterm |

# 3. 아키텍처

## 3.1 시스템 구조

전체 시스템은 송신 보드와 수신 보드로 나뉜다.

- **송신:** 화이트보드 → 카메라 → 이미지 전처리 → CNN(Conv, Pool, FC, Argmax) → 패킷·CRC → BFSK 광 송신. 버튼을 한 번 누르면 한 번 촬영과 인식이 시작되고, 인식 결과는 ASCII 8-bit 데이터로 전달된다.
- **전송:** ASCII 데이터를 36-bit 패킷으로 만들어 1-bit씩 BFSK 레이저 신호로 송신한다.
- **수신:** 광센서(BPW34) → XADC → FFT Buffer → FFT → FFT Power → BFSK 복조 → 프레임·CRC 검증 → UI 출력. 수신 UI에서 FFT 주파수 분석 결과와 복원된 문자를 함께 확인한다.

![System Flow](assets/readme/fig02.png)

버튼을 누르면 화이트보드의 손글씨를 촬영해 인식하고, 송신 UI에 촬영 화면, 전처리 결과, 인식 문자가 표시된다. 인식 결과는 레이저로 송신되고, 수신 보드의 광센서가 받은 신호를 복원해 수신 UI에 문자를 출력한다.

<p align="center">
  <img src="assets/readme/fig03.png" width="49%" />
  <img src="assets/readme/fig04.png" width="49%" />
</p>
<p align="center">송신부 동작: 화이트보드 손글씨 촬영과 인식(좌), 송신 UI: 촬영 화면, 전처리 결과, 인식 문자(우)</p>

<p align="center">
  <img src="assets/readme/fig05a.png" width="30%" />
  <img src="assets/readme/fig05b.png" width="31%" />
  <img src="assets/readme/fig06.png" width="37%" />
</p>
<p align="center">레이저 광 송신(좌), 광센서 수신 보드(중), 수신 UI: 누적 문자열과 최근 수신 문자(우)</p>

송신부 PS는 버튼 입력(AXI-GPIO), 카메라 프레임 선택, ArUco 원근 보정과 ROI 생성, DMA 제어, CNN 결과 수신(인터럽트 + AXI-Lite), BFSK 송신 드라이버를 맡는다. PL은 VDMA, DMA, 이미지 전처리, CNN 가속기, BFSK TX를 맡는다. 수신부 PL은 XADC부터 CRC 검증까지를 맡고, PS는 AXI GPIO로 패킷을 읽어 UART CSV로 PC UI에 전달한다.

![CNN PS-PL Architecture](assets/readme/fig07.png)

## 3.2 설계 이론 및 배경

### CNN과 QAT

28×28 입력에 3×3 Convolution(stride 1)과 2×2 Max Pooling을 두 번 적용해 6@26×26 → 6@13×13 → 16@11×11 → 16@5×5 feature map을 만들고, Flatten한 400개 값을 FC 120 → 84 → 26으로 줄여 A~Z 중 하나를 고른다. 학습 단계에서 하드웨어와 같은 비트 폭의 양자화를 적용하는 QAT로 정수 연산 오차까지 반영해 학습했고, 정확도는 94.13%였다. QAT 모델의 Python 출력은 하드웨어 출력과 같은 값이 되므로 C 골든 모델과 RTL 검증의 기준으로 사용했다.

### BFSK와 패킷 구조

인식 결과를 패킷으로 바꾸고, 패킷을 빛 신호로 바꿔 쏘고, 수신한 빛을 패킷으로 해석한 뒤, 패킷을 다시 ID와 문자로 해석한다. bit 0, bit 1, SYNC를 서로 다른 주파수의 빛 깜박임으로 표현하는 BFSK를 사용했다.

![빛에 정보를 담아 전달하는 흐름](assets/readme/fig08.png)

송수신 시점이 다른 비동기 통신이라 수신부는 데이터 시작 시점을 알아야 한다. 그래서 시작 표시와 순서·오류 검사 정보를 담은 패킷 단위로 전송했다.

| 필드 | 역할 |
|:--|:--|
| Preamble | SYNC 4심볼, 어디서 동기화할지 알림 |
| SFD | 0xD5, 정해진 위치에 맞는지 확인 |
| Frame ID | 몇 번째 순서로 보냈는지 표시 |
| Data | 문자의 ASCII 값 |
| CRC-8 | 다항식 0x07, 중간에 오류가 없는지 확인하고 오류면 패킷 폐기 |

![비동기 수신을 위한 패킷 설계](assets/readme/fig09.png)

### 주파수 설정

초기 설계는 bit 0·1·SYNC를 10·20·25 kHz, 샘플링을 160 kS/s(Bin 8·16·20)로 정했다. 10월 9일 송수신 모두 bit 0 60.096 kHz, bit 1 97.656 kHz, SYNC 120.192 kHz, 샘플링 961.538 kS/s(Bin 8·13·16)로 바꾸고 문자 사이 대기도 10 ms에서 0.4 ms로 줄여, 패킷 전송 속도가 기존 대비 6배(1 packet/9.6 ms) 빨라졌다. 같은 128-point FFT를 쓰면서 1 심볼(256 샘플)의 길이가 1.6 ms에서 266.24 µs로 짧아졌다. 두 설정 모두 목표 주파수가 FFT Bin에 정수 주기로 맞도록 골랐다.

### FFT 기반 주파수 판별

빛의 변화를 숫자로 기록하고(XADC), 128개씩 모으고(FFT Buffer), 주파수별로 분석하고(FFT Core), 각 성분의 크기를 구해(FFT Power, Re² + Im²) 후단 판별부가 0과 1을 가른다. Radix-2 DIT 구조는 최초 입력을 Bit Reversed 주소에 저장하면 결과가 Natural Order(Bin 0 → 127)로 출력되어 후단 처리 순서가 단순해진다.

DC 성분은 Bin 0에 나타나므로 판별에서 제외했고, 목표 주파수를 정수 주기 Bin에 맞춰 단일 주파수 조건에서 추가 창(Windowing) 곱셈을 생략했다.

![FFT로 빛에 담긴 정보를 읽는 흐름](assets/readme/fig10.png)

# 4. 상세 설계

## 4.1 CNN 가속기

### 인식 흐름과 이미지 전처리

카메라 속 글자는 촬영 및 저장 → 글자 영역 선택 → AI 인식용 전처리 → CNN 인식 순서로 결과가 된다. 버튼을 누르면 VDMA가 DDR에 저장한 화면을 고르고, PS가 ArUco 마커 위치를 기준으로 글자를 잘라 112×112 ROI를 만든다. ROI는 DMA로 PL에 전달되어 전처리와 CNN을 차례로 거친다.

PL의 `img_preprocess`는 흑백 변환과 극성 반전 → 임계값 처리(T = 160) → Bounding Box로 글자만 추출 → 비율을 유지해 최대 22×22로 크기 정규화 → 검은 여백으로 28×28 중앙 배치를 거쳐 CNN 입력(28×28, 16-bit)을 만든다. 글자만 크게, 비율은 유지하고, 주변에는 여백을 두어 학습 데이터(EMNIST)와 같은 형태로 맞추는 것이 목적이다.

![CNN Image Preprocessing Architecture](assets/readme/fig11.png)

### Convolution: Line Buffer와 MAC Array

Line Buffer가 입력 채널을 재사용해 3×3 Window를 만들고, Weight MEM이 공급하는 가중치로 3채널을 병렬 곱셈한 뒤 Adder Tree가 채널별 결과를 더해 부분합을 낸다. Conv2(입력 6채널, 출력 16채널)는 ch0~2를 Phase 1, ch3~5를 Phase 2로 나눠 같은 Line Buffer·MAC Array를 재사용하고, 두 Phase의 부분합은 Output Buffer에서 누적한다. Conv 컨트롤러는 Layer·Phase·출력 채널 순서를 관리한다.

![CNN Block Diagram - Line Buffer & MAC Array](assets/readme/fig12.png)

### Output Buffer, ReLU & Quantization

- MAC Array가 3채널씩 곱하고 더한 부분합(Partial Sum)을 받아, Phase로 나눠 계산한 부분합을 bias와 이전 저장값에 더해 출력 채널별로 누적한다.
- 누적이 끝난 40-bit 값은 ReLU로 음수를 제거한 뒤 Quantizer에서 16-bit로 줄인다.
- Lane Packer는 픽셀 순서로 도착한 채널 값을 3채널 단위 그룹으로 묶어 저장하고, Out Reorder는 저장된 값을 채널 순서로 읽어 Max Pooling이 한 채널의 픽셀을 연속으로 받도록 재정렬한다.
- 40-bit 누적과 ReLU·Quantization은 레지스터 단계로 나누고, 데이터와 valid를 같은 클록에 등록해 MAC Array 출력과 Max Pooling 입력을 정렬했다.

<p align="center">
  <img src="assets/readme/fig13a.png" width="55%" />
  <img src="assets/readme/fig13b.png" width="43%" />
</p>
<p align="center">CNN Block Diagram - Output Buffer</p>

### Fully Connected

FC는 Flatten된 400개 입력을 120 → 84 → 26으로 줄이고, 마지막 층 출력에서 ArgMax로 가장 큰 값의 클래스를 고른다. 두 가지 구조를 설계해 비교했다(결과는 6.1절).

- **층별 전용 엔진(DSP 40):** FC1·FC2·FC3에 각각 25·10·5개의 MAC을 둔다.
- **공유 엔진(DSP 20):** Feature Buffer(LUT 기반 distributed RAM)에 층별 입력을 두고, FC Controller가 Layer MUX와 Weight·Bias 주소를 바꿔 세 층이 하나의 Shared MAC Array를 순차 사용한다.

<p align="center">
  <img src="assets/readme/fig14a.png" width="49%" />
  <img src="assets/readme/fig14b.png" width="49%" />
</p>

![CNN Block Diagram - Fully Connected](assets/readme/fig14c.png)

### Zero Gating과 Zero Skipping

![EMNIST 입력 이미지와 픽셀 값 분포](assets/readme/fig15.png)

EMNIST 입력 이미지에서 픽셀 값이 0인 부분은 66.43%였고, 0은 곱해도 0이므로 곱셈의 3분의 2가 불필요한 연산이었다. Conv·FC의 MAC Array 앞에 Zero Gating Array를 두어 입력이 0이면 MAC 입력을 고정해 곱셈기 동작을 막았고(전력 감소), FC에서는 입력이 0이면 결과를 바로 0으로 처리하고 다음 입력으로 넘어가(Zero Skipping) 연산 사이클을 줄였다(속도 향상). 예를 들어 FC 입력 6개 중 0이 3개면 연산이 6번에서 3번으로 줄어든다.

<p align="center">
  <img src="assets/readme/fig16a.png" width="49%" />
  <img src="assets/readme/fig16b.png" width="49%" />
</p>
<p align="center">CNN Block Diagram - Zero Gating(좌: Convolution, 우: Fully Connected)</p>

## 4.2 BFSK 송신부

AXI4-Lite TX Wrapper가 CNN 결과 문자를 받으면, Frame Generator가 SFD·ID·DATA로 프레임을 만들고 CRC-8을 붙인다. TX FSM은 IDLE → PREAMBLE → SFD → ID → DATA → CRC 순서로 진행하며 심볼 완료 신호를 받아 다음 비트로 넘어가고, BFSK Mapper가 비트를 심볼 종류로, Carrier Generator가 심볼을 주파수별 깜박임 신호로 바꿔 2N7000으로 레이저를 구동한다.

![광통신 TX Block Diagram](assets/readme/fig17.png)

Vitis에서는 Driver(`bfsk_tx.c`)와 HAL(`bfsk_tx_hal.c`)을 나눴다. CNN 통합 API `bfsk_tx_send_byte()`가 한 문자를 보내고, DATA(+0x00)·CTRL(+0x04)·STATUS(+0x08) 레지스터로 READY/BUSY와 완료·타임아웃을 확인한다.

![BFSK TX SW Stack](assets/readme/fig18.png)

## 4.3 BFSK 수신부

XADC가 광센서 전압을 샘플링하면 128-point FFT가 Bin별 Power를 계산하고, Bin Detector가 bit 0·bit 1·SYNC Bin을 비교해 심볼을 판정한다. Symbol Detect & Sync는 SEARCH → WAIT_NON_SYNC → COLLECT → SCORE → SELECT → CHECK_SFD 순서로 Preamble 뒤 비트 구간을 모으고, Frame Decoder와 CRC-8 Check가 패킷을 복원·검증한다.

![BFSK RX Block Diagram](assets/readme/fig19.png)

복원 결과는 RX Latch를 거쳐 AXI GPIO 32-bit Packet Word로 PS에 전달되고, PS 애플리케이션이 Packet Reader·Error Check·CSV Formatter로 UART 출력을 만든다.

![BFSK RX SW Stack](assets/readme/fig20.png)

## 4.4 FFT

FFT Top은 XADC(VAUX14, 12-bit) → FFT Buffer → FFT Core(128-point Radix-2 DIT) → FFT Power로 구성되고, 40-bit `power_result`와 `fft_mag_valid`를 출력한다.

![FFT Architecture](assets/readme/fig21.png)

### FFT Buffer: Ping-Pong 구조

128샘플×12-bit(64×24-bit) 뱅크 두 개를 번갈아 써서 샘플 수집과 Core 전달을 분리했다. 128샘플 수집이 끝나면 해당 뱅크의 Full을 1로 만들고, `frame_valid && frame_ready`로 프레임 전달을 시작해 `buf_data_valid && buf_data_ready`로 두 샘플씩 64쌍을 넘긴다. 마지막 쌍 전달이 끝나면 Full을 0으로 돌려 다음 수집에 재사용하므로, 미처리 프레임이 덮어쓰이지 않는다.

![FFT Buffer의 Ping-Pong 구조](assets/readme/fig22.png)

### FFT Core: 연산기 재사용과 주소 생성

FFT Core는 Controller FSM, Address Generator, Dual-Port Memory(MEM0·MEM1, 128×40-bit), Twiddle ROM(64×32-bit), Butterfly 하나로 구성된다. 한 Stage에서 64번의 Butterfly 연산을 마치면 읽기·쓰기 메모리 역할을 교대하며 총 7 Stage를 반복한다. 하나의 회로로 반복 계산해 연산 자원을 아끼고, 입력과 결과를 다른 메모리에 저장해 덮어쓰기를 막고 제어를 단순화했다.

- **입력 저장:** 최초 입력을 Bit Reversed 주소에 저장해(예: x[1] → MEM0[64]) 결과를 Natural Order로 출력한다.
- **주소 생성:** Stage 위치에 0/1을 삽입해 A/B 주소를 만들고, count의 하위 Stage 비트로 Twiddle 주소를 구한다. Stage별 case문 대신 gap/mask/shift 공통 규칙으로 바꿔, 같은 조건(XADC IP + FFT Top, 100 MHz)에서 Setup WNS를 0.766 ns에서 0.887 ns로 0.121 ns 늘렸다.
- **Valid 기반 제어:** ST_READ(주소 전달, 동기식 메모리 읽기 대기) → ST_CALC(Butterfly) → ST_WRITE(결과 기록, Count 갱신)를 반복한다. 읽은 데이터와 valid를 함께 등록하고(`o_rdata_valid` → `i_read_data_valid`), 쓰기 완료(`i_wdata_valid`)까지 주소를 유지한다.

![하나의 연산기로 FFT를 반복 계산하는 구조](assets/readme/fig23.png)

### Multi Cycle에서 Pipeline으로

1차 RTL은 한 데이터 쌍의 읽기·연산·저장이 끝나야 다음 쌍을 처리하는 Multi Cycle 구조였다. 더 높은 처리량이 필요한 환경에서는 이 대기 시간이 실시간 처리 제약이 될 수 있어, 서로 다른 데이터 쌍의 읽기·연산·저장을 동시에 수행하는 Pipeline 구조로 바꿨다. 성능 비교 결과는 6.1절에 정리했다.

<p align="center"><img src="assets/readme/fig24.png" width="60%" /></p>
<p align="center">Pipeline 동작: 데이터 쌍별 읽기·연산·저장 중첩</p>

## 4.5 광 송수신 회로

- **광원:** 적외선 LED는 10 cm에서 전압차가 68 mV로 송수신 가능 거리가 매우 짧았다. 레이저 모듈은 1.6 m에서 전압차 2 V, 수신단 전류 약 404 µA를 확보해, 장거리 통신이라는 1차 목표에 맞춰 레이저를 최종 송신 모듈로 선택했다.

![광원 선택(적외선 LED vs 레이저 모듈)](assets/readme/fig25.png)

- **수신 회로:** 수신단 전류가 이미 충분한 상태에서 OPAMP로 증폭하면 과도한 출력이 요구되어 파형이 왜곡됐다(6.2절). OPAMP를 쓰지 않고 분압 회로로 2.4 V를 0.4 V로 낮춰 XADC 허용 입력 범위 안에서 파형을 정상 유지했다.

![수신부 회로 구조 선택(OPAMP 유무)](assets/readme/fig26.png)

# 5. 시뮬레이션 및 검증

## 5.1 CNN 골든 모델 비교 검증

같은 입력을 Python QAT 모델, C 골든 모델, RTL 하드웨어에 넣고 세 출력이 같은지 확인했다. QAT 덕분에 Python 출력이 하드웨어 출력값과 같으므로, Python 결과를 기준으로 C 골든 모델과 RTL을 차례로 맞췄고 세 모델의 출력이 일치했다.

![CNN 동작 검증](assets/readme/fig27.png)

## 5.2 BFSK TX UVM 검증

BFSK 송신부(AXI-Lite TX Wrapper, Optical TX Top, BFSK Mapper, Frame Generator, CRC-8, TX FSM, Carrier Generator)를 UVM 환경에서 검증했다. 총 34개 검증 항목을 충족하고 DATA 256종을 전수 송신했으며, 기능 Coverage 100%를 달성했다.

| 분류 | 항목 수 | 확인 내용 |
|:--|:--:|:--|
| 정상·패턴 패킷 | 8 | DATA 256종 전수 송신, 교대 패턴(0x55·0xAA), ID 순환, 총 832개 패킷 완료 |
| Reset 복구 | 9 | 프리앰블·데이터·CRC 처리 중 Reset, 출력 초기화, 정상 패킷 재송신 |
| 송신 제어 | 5 | Busy 중 START 무시, 송신 데이터 유지, 무효 START 무시, 완료 후 Ready 복귀 |
| AXI 인터페이스 | 12 | 읽기·쓰기, 주소·데이터 도착 순서, 응답 지연, 후속 요청 대기 |

![BFSK TX UVM Test](assets/readme/fig28.png)

## 5.3 BFSK RX UVM 검증

BFSK 수신부를 UVM 환경에서 총 328개 시나리오로 검증하고 Coverage 100%를 달성했다.

| 분류 | 횟수 | 확인 내용 |
|:--|:--:|:--|
| 정상·랜덤 프레임 | 32 | Phase, valid 간격, ID·DATA 조합 |
| 오류·Reset 복구 | 38 | CRC·SFD 오류, 무신호, 중간 Reset, 상태별 복구 |
| 경계·전력 조건 | 96 | 판정 임계값, 혼합·비대칭 전력, SFD 전력 Sweep |
| 패킷/분류기 | 162 | 패킷 모호성·거부·복구, 40-bit Power 변화 |

![BFSK RX UVM Test](assets/readme/fig29.png)

## 5.4 FFT Buffer UVM 검증

directed 기능 bin 7/7(100%)을 달성했다. 동시 입출력 64회, 두 버퍼 사용(버퍼 0 3회, 버퍼 1 2회), 두 버퍼 재사용 각 1회, Full 상태 입력 폐기 4회, 공간 확보 후 수집 재개 1회를 확인했고, randomized 데이터 구간은 1018/1024 bins(99.41%)를 달성했다.

![FFT Buffer UVM 검증](assets/readme/fig30.png)

## 5.5 FFT Butterfly UVM 검증

![FFT Butterfly UVM 검증](assets/readme/fig31.png)

랜덤 입력 1,024개를 대상으로 Butterfly 파이프라인의 단계별 데이터 저장, 단계 간 이동, Valid 정렬 및 연속 처리 동작을 검증했다. 각 단계의 데이터를 기대값과 비교하여 입력 간 데이터가 섞이지 않는지 확인하고, Valid의 지연 관계와 입력 중간의 Valid 공백, 초과 입력, 예상 밖 출력 및 결과 불일치 여부를 점검했다. 입력 ID에 대응하는 1,024개 bin이 모두 hit되어 입력 ID 커버리지 100%를 달성했다.

## 5.6 검증 결과 요약

| 항목 | 검증 방법 | 결과 | 달성 여부 |
|:--|:--|:--|:--:|
| CNN 추론 | Python QAT·C 골든 모델·RTL 비교 | 세 모델 출력 일치 | 달성 |
| BFSK TX | UVM | 34개 항목, DATA 256종 전수, 832 패킷, Coverage 100% | 달성 |
| BFSK RX | UVM | 328개 시나리오, Coverage 100% | 달성 |
| FFT Buffer | UVM | directed 7/7(100%), randomized 1018/1024(99.41%) | 달성 |
| FFT Butterfly | UVM | randomized 1024/1024(100%) | 달성 |
| RX 복조 | Vivado ILA | frame_id 연속 증가, packet_error 0 | 달성 |

# 6. 결과 분석 및 트러블슈팅

## 6.1 FPGA 결과

### 최종 시연

송신부에서 'HELLO' 다섯 글자를 촬영해 인식하고 레이저로 송신했으며, 수신부 UI에서 FFT 스펙트럼과 함께 같은 문자열이 복원되는 것을 최종 시연으로 확인했다. 손글씨 CNN 인식, BFSK 광통신, FFT 신호처리를 하나의 시스템으로 통합했다.

### CNN 가속기 성능

CPU(667 MHz)에서 전처리와 CNN을 수행한 경우와 비교해, 100 MHz PL 가속기는 6.7배 낮은 클록에서 약 6배 빠르게 동작했다.

| 항목 | CPU | CNN 가속기 | 개선 |
|:--|:--|:--|:--|
| 연산 시간 | 2582.13 µs | 415.15 µs | 6.22배 |
| 전력 | 157.02 mW | 66.43 mW | 57.7% 감소 |
| 에너지 | 405.45 µJ | 27.58 µJ | 93.2% 감소(14.7배 효율) |

![CNN 성능 확인 결과](assets/readme/fig33.png)

#### Zero Gating·Zero Skipping 적용 결과

Zero Gating·Zero Skipping 적용 전후는 연산 시간 141.67 µs → 124.28 µs(12.3% 단축), 전력 93.38 mW → 88.75 mW(5% 감소), 에너지 13.23 µJ → 11.03 µJ(16.6% 감소)였다.

![CNN 가속기의 연산, 전력 최적화 결과](assets/readme/fig34.png)

#### FC 공유 엔진 적용 결과(DSP 40 → DSP 20)

FC를 층별 전용 엔진(FC1 25 + FC2 10 + FC3 5 = DSP 40)에서 세 층이 MAC 20개를 함께 쓰는 공유 엔진(DSP 20)으로 바꿨다. 두 엔진은 같은 가중치에서 FC 출력(logit 26개)이 bit 단위로 같고, 보드 testset 70장의 인식 결과도 10회 측정 모두 같았다(PL 정답 65/70). 두 엔진은 브랜치 [`fc-dsp-40`](https://github.com/mumallaeng/LightLetter/tree/fc-dsp-40)과 [`fc-dsp-20`](https://github.com/mumallaeng/LightLetter/tree/fc-dsp-20)에 남겨 두었다.

측정은 FC 단독 추정과 보드 실측 두 가지로 나눠, 두 엔진을 같은 조건에서 진행했다.

- **비교 조건:** 두 버전은 FC RTL과 FC 가중치 파일만 다르다. 전처리, Conv, BFSK, block design, PS 소프트웨어(Release -O2)와 빌드 스크립트(Vivado/Vitis 2020.2), 보드, 전원 어댑터, 실습 PC를 같게 두고 두 버전을 처음부터 같은 순서로 빌드했다. 측정마다 보드에 올린 bitstream의 SHA-256을 확인해 버전이 섞이지 않게 했다.
- **FC 단독(추정):** `fc_top`만 xc7z020에 100 MHz 제약으로 합성·배선해 자원과 타이밍을 구했다. 전력은 배선 후 netlist를 같은 테스트벤치로 시뮬레이션해 신호별 toggle(SAIF)을 기록하고, 이를 Vivado `report_power`에 넣어 실제 동작의 switching activity로 계산했다. netlist net의 99~100%가 SAIF와 맞아 Confidence Level은 High였다. 배선 지연(SDF)을 넣은 timing simulation SAIF로 glitch를 포함한 값도 확인했다.
- **보드(실측):** Zybo Z7-20에서 카메라와 HDMI를 멈추고 testset 70장을 PL로 처리하며 구간 시간을 기록했다. 보드 전력은 전원 보호 IC(TPS25940)의 전류 모니터 출력을 XADC로 1 kHz 샘플링해 구했고, 상태마다 10초씩 3라운드 측정했다. 증가분은 바로 앞뒤 WFI(대기) 구간 기준으로 계산해 온도와 레귤레이터 드리프트를 상쇄했다.
- **반복 측정:** 시간에 따른 드리프트가 한 버전에만 실리지 않도록 DSP 20과 DSP 40을 번갈아 5회씩, 총 10회 측정했다. 원본 로그, 리포트, SAIF, bitstream은 해시와 함께 보관했다.

| 항목 | 전용 엔진 (DSP 40) | 공유 엔진 (DSP 20) | 변화 |
|:--|:--:|:--:|:--:|
| DSP48E1 | 40 | 20 | −20 (−50%) |
| LUT | 2,632 | 1,832 | −800 (−30.4%) |
| BRAM | RAMB36 31 + RAMB18 6 | RAMB36 35 | 36Kb 환산 +1 |
| Setup WNS (100 MHz) | +0.919 ns | +1.051 ns | +0.132 ns |
| FC 사이클, 입력 매 클럭 | 3,229 | 3,216 | −13 |
| FC 사이클, 입력 30%·출력 15% | 3,477 | 4,195 | +718 |
| FC 동적 전력, 동작 (추정) | 0.111 W | 0.103 W | −8 mW |
| FC 동적 전력, 대기 (추정) | 0.074 W | 0.005 W | −69 mW |
| 보드 PL 전처리+CNN 시간 | 257.37 µs | 271.44 µs | +14.07 µs (+5.5%) |
| 보드 대기 전력 (WFI) | 2,778.7 mW | 2,680.6 mW | −98.1 mW (−3.5%) |
| inference당 보드 에너지 | 1,093.9 µJ | 1,096.0 µJ | +2.1 µJ (+0.2%) |

공유 엔진은 DSP를 절반, LUT를 30% 줄였고 타이밍 여유도 늘었다. 대신 보드의 전처리+CNN 시간이 14.07 µs(5.5%) 늘었다. 입력이 매 클럭 들어오면 두 엔진의 사이클은 거의 같지만, 입력에 빈 클럭이 섞일수록 공유 엔진이 느려진다. 전용 엔진은 층마다 MAC이 있어 pool2 출력이 들어오는 동안 FC1을 함께 계산하고, 공유 엔진은 FC1 출력의 첫 그룹만 입력과 함께 계산한 뒤 나머지는 입력 400개가 모두 모인 다음 계산하기 때문이다. 보드에서는 pool2 출력이 띄엄띄엄 들어와 이 차이가 시간으로 드러났다. DMA 전송과 CPU 처리 시간은 두 버전이 같아, 시간 차이는 FC에서만 나왔다. 10회 측정의 시간 편차는 0.03 µs 이내였다.

대기 전력은 공유 엔진이 98.1 mW 낮았고, 10회 측정 범위가 한 번도 겹치지 않았다(공유 엔진 최대 2,692 mW, 전용 엔진 최소 2,766 mW). 원인은 가중치 ROM 읽기 조건이다. 전용 엔진의 ROM은 연산이 없을 때도 매 클럭 읽고, 공유 엔진의 ROM은 MAC을 발행하는 클럭에만 읽는다. FC 단독 추정의 대기 차이 69 mW(이 중 BRAM 64 mW)는 1.0 V 코어 기준이며, 레귤레이터 효율 70~75%를 가정하면 5 V 입력에서 92~99 mW로 보드 실측 차이와 맞는다.

inference 한 번의 보드 에너지는 0.2% 차이로 사실상 같다. 공유 엔진은 처리 시간이 늘어난 만큼 전력이 낮아 서로 상쇄됐다. 송신 보드는 촬영과 버튼 입력을 기다리는 대기 시간이 대부분이므로, DSP 절반과 대기 전력 98 mW 감소를 얻는 공유 엔진을 적용했다.

보드 전력은 입력 전압을 5.000 V로 가정해 계산했으므로(DMM 미측정) 절대값보다 두 버전 사이의 차이로 해석한다. FC 단독 전력은 Vivado 추정치이고, 레귤레이터 효율로 두 값을 잇는 계산은 효율이 가정이라 크기 비교에만 쓴다.

### FFT 성능

Multi Cycle 구조의 1차 RTL은 CPU 대비 FFT Core 단독 1.34배(51.407 µs → 38.43 µs), Core + Power 1.38배(53.021 µs → 38.44 µs)였다. Pipeline 적용 후에는 CPU 대비 7.08배(53.021 µs → 7.49 µs, 85.87% 단축), 기존 RTL 대비 5.13배(80.52% 단축) 빨라졌다.

![FFT 1차 성능 확인 결과](assets/readme/fig35.png)

![FFT 2차 성능 개선(Pipeline 적용)](assets/readme/fig36.png)

### FFT 자원 사용량과 타이밍

| 항목 | 결과 |
|:--|:--|
| LUT | 688 / 53,200 (1.29%) |
| FF | 770 / 106,400 (0.72%) |
| BRAM | 4.5 / 140 (3.21%) |
| DSP | 6 / 220 (2.73%) |
| Setup WNS / Hold WHS | +0.699 ns / +0.036 ns, 위반 Endpoint 0개 |
| On-Chip 전력(Vivado 추정) | 0.142 W (Dynamic 0.037 W + Static 0.105 W) |

![FFT 자원 사용량과 Timing](assets/readme/fig37.png)

## 6.2 문제 원인 분석

### CNN 입력 영상 불일치

학습한 CNN의 정확도는 94.13%였지만, 실제 촬영 환경에서는 테스트 문자 5개(A~E)가 모두 'WMAMR'로 오인식됐다. 학습 데이터와 촬영 입력의 기울기, 배경, 글자 크기 차이가 원인이었다. PS에서 ArUco 마커 좌표로 원근을 보정해 글자 영역을 정면 형태로 바꾸고, PL 전처리를 Gray filter·고정 샘플링에서 임계값 160, Bounding Box 추출, 비율 유지 최대 22×22, 검은 여백 28×28 중앙 배치로 바꿔 입력 형태를 일관되게 맞춘 뒤 인식률이 올라갔다.

![Troubleshooting - CNN 입력 영상 개선](assets/readme/fig38.png)

### OPAMP 포화에 따른 수신 파형 왜곡

레이저 수신단 전류가 약 404 µA로 이미 충분한 상태에서 OPAMP 증폭을 적용하자, 약 400 µA × 47 kΩ = 18.8 V에 해당하는 출력이 요구되어 OPAMP가 포화되고 파형이 왜곡됐다. OPAMP를 제거하고 분압 회로로 2.4 V를 0.4 V로 낮춰 XADC 허용 입력 범위 안에서 정상 파형을 확보했다(4.5절).

### BFSK 복조 경계 오류

임의의 시점에서 예상 ID 179·182 대신 178·181이 출력되는 복조 실패가 나타났다. 1비트는 FFT 분석 구간 두 개에 해당하는데, 기존에는 데이터 유효 시점부터 FFT 결과를 하나씩 건너뛰며 사용했다. FFT 구간이 비트 전환 경계에 걸리면 그 결과를 유효로 인식해 판별 오류가 났다.

연속된 두 FFT 결과를 합산해 비트를 판별하고, (0+1)(2+3)...과 (1+2)(3+4)... 두 후보 중 SFD 8-bit 패턴과 맞는 쪽을 시작점으로 골라 잘못된 복조를 막았다. 개선 후 ILA에서 frame_id가 71, 72, 73 ... 78로 연속 증가하고 data가 'A'로 유지되며 packet_error가 0인 정상 복조를 확인했다.

![복조 알고리즘 수정](assets/readme/fig39.png)

## 6.3 개선 방안

- 소문자를 포함한 문자 집합 확장과 촬영 환경 데이터 추가 학습으로 실환경 인식률을 높인다.
- 오류 정정 부호(FEC)와 재전송 절차를 추가해 장거리·외부광 환경에서 통신 신뢰도를 높인다.
- FC 공유 엔진과 Zero Skipping을 Conv 단계까지 넓히고, 보드 실측 전력으로 Vivado 추정값을 검증한다.
- 여러 문자를 연속 촬영·인식·송신하는 파이프라인과 수신부 자동 이득 조정을 추가한다.

# 7. 결론 및 고찰

- 손글씨 CNN 인식, BFSK 광통신, FFT 신호처리를 Zybo Z7-20 두 대 위에서 하나의 송수신 시스템으로 통합하고, 'HELLO' 촬영·인식·광 송신·복원을 시연으로 확인했다.
- CNN 가속기는 CPU 대비 6.22배 빠르고 전력 57.7%, 에너지 93.2%를 줄였으며, Zero Gating·Skipping으로 연산 시간 12.3%, 에너지 16.6%를 추가로 줄였다.
- FFT는 Pipeline 구조로 7.49 µs(CPU 대비 7.08배)에 처리하면서 100 MHz 타이밍을 만족하고 자원은 1~3%만 사용했다.
- BFSK TX·RX와 FFT Buffer를 UVM으로 검증해 Coverage 100%(randomized 구간 99.41%)를 달성했다.

모델 정확도가 높아도 입력이 학습 데이터와 다르면 인식이 실패했고, 시뮬레이션에서 맞던 판별도 실제 신호의 비트 경계에서는 흔들렸다. 입력 준비, 회로 특성, 신호 경계까지 함께 보는 시스템 관점이 결과를 좌우했다. 각 파트가 합의한 패킷 규격과 Python·C 골든 모델을 구현과 검증의 공통 기준으로 삼은 덕분에, 일곱 명이 나눠 만든 모듈을 짧은 통합 기간 안에 하나의 송수신 시스템으로 맞출 수 있었다.

# 저장소 구조와 실행 방법

두 Zybo Z7-20 보드는 각자 디렉터리를 가진다. 보드 디렉터리 안에서 기능 블록마다 RTL, 테스트벤치, 모델을 함께 두고, `vivado/`와 `vitis/`에 보드 단위 Vivado 프로젝트 스크립트와 Vitis 워크스페이스를 둔다.

```text
tx/                 송신 보드: camera -> ArUco -> preprocess -> CNN -> BFSK TX
  cnn/              rtl/, ip/ (packaged cnn_ip), golden/ (C), model/ (Python), tb/
  preprocess/       img_preprocess RTL and testbenches
  aruco/            notebook, calibration, marker sheets, host test harness
  bfsk_tx/          BFSK TX RTL, testbenches, Nucleo checker firmware
  camera/           Digilent P-CAM / HDMI IP repository
  ui/               capture-board viewer (START_UI.cmd)
  vivado/           create_project.tcl, block design Tcl, constraints/, export/ (XSA)
  vitis/            Vitis workspace: tx_fpga, tx_fpga_system, tx_top_wrapper
rx/                 수신 보드: XADC -> FFT -> BFSK RX -> UART -> PC UI
  fft/  bfsk_rx/  snapshot/  ui/
  vivado/  vitis/  prebuilt/  tools/  docs/
legacy/             사용하지 않거나 대체된 소스, 예전 경로 그대로 보관
assets/readme/      README 그림
```

Windows에서 Vivado·Vitis 2020.2가 필요하다. 각 보드 디렉터리의 `CREATE_VIVADO.cmd`, `CREATE_VITIS.cmd`는 스크립트 기준 상대 경로로 프로젝트를 다시 만들므로, 새로 clone한 PC에서도 동작한다.

### 수신 보드

1. `rx\CREATE_VIVADO.cmd`가 `rx\vivado\project\rx_final.xpr`를 만든다.
2. `rx\CREATE_VITIS.cmd`가 `rx\vivado\export\FFT_RX_FINAL.xsa`로 `rx\vitis\workspace`를 만들고 `BFSK_RX_UART` 앱을 빌드한다.
3. Vitis에서 `rx\vitis\workspace`를 열어 `BFSK_RX_UART`를 보드에서 실행한다. 미리 빌드한 파일은 `rx\prebuilt\`에 있다.
4. `rx\START_UI.cmd`가 `http://127.0.0.1:8765`에 PC UI를 띄운다. 보드 COM 포트를 잡고 있는 시리얼 터미널은 먼저 닫는다. UART 형식은 `rx/docs/rx_uart_protocol.md`에 있다.

### 송신 보드

1. `tx\CREATE_VIVADO.cmd`가 `tx\vivado\project\tx_top.xpr`를 만든다. bitstream을 생성하고, 하드웨어가 바뀌면 XSA를 `tx\vivado\export\`로 내보낸다.
2. 하드웨어(bitstream 포함)를 `tx\vivado\export\tx_top_wrapper.xsa`로 내보낸 뒤 `tx\CREATE_VITIS.cmd`를 실행한다. `tx\vitis\workspace`를 만들고 platform/BSP를 생성한 뒤 `tx_fpga`를 빌드한다. 다시 실행하면 같은 XSA로 platform을 갱신하고 다시 빌드한다. 실행 전에 이 워크스페이스를 연 Vitis는 닫는다.
3. Vitis에서 `tx\vitis\workspace`를 열어 `tx_fpga`를 보드에서 실행한다. 앱은 `tx\vitis\tx_fpga\src` 원본을 링크하므로 이후 소스 수정이 다시 빌드할 때 반영된다. 실행기는 `C:\Xilinx\Vitis\2020.2\bin\xsct.bat`을 쓰며, Vitis가 다른 경로에 있으면 `BFSK_XSCT`를 바꾼다.
4. `tx\START_UI.cmd`가 캡처 보드 뷰어를 띄운다. 패키지는 처음 한 번 `pip install -r tx\ui\requirements.txt`로 설치한다.

BFSK TX 이력은 Critical-mankind/BFSK_Tx에서 가져왔다. BFSK TX RTL과 테스트벤치는 `tx/bfsk_tx/`, Nucleo 수신 테스트는 `tx/bfsk_tx/nucleo_checker/`에 있다.

# 참고 문헌

- [1] Digilent, Zybo Z7 Reference Manual. https://digilent.com/reference/programmable-logic/zybo-z7/reference-manual
- [2] Digilent, Pcam 5C Reference Manual. https://digilent.com/reference/add-ons/pcam-5c/reference-manual
- [3] AMD, 7 Series FPGAs and Zynq-7000 SoC XADC Dual 12-Bit 1 MSPS Analog-to-Digital Converter User Guide (UG480). https://docs.amd.com/r/en-US/ug480_7Series_XADC
- [4] AMD, Vivado Design Suite User Guide: Getting Started (UG910). https://docs.amd.com/r/en-US/ug910-vivado-getting-started
- [5] AMD, Vitis Unified Software Platform Documentation: Embedded Software Development (UG1400). https://docs.amd.com/r/en-US/ug1400-vitis-embedded
- [6] G. Cohen, S. Afshar, J. Tapson, A. van Schaik, "EMNIST: an extension of MNIST to handwritten letters," 2017. https://arxiv.org/abs/1702.05373
- [7] OpenCV, Detection of ArUco Markers. https://docs.opencv.org/4.x/d5/dae/tutorial_aruco_detection.html
- [8] Vishay, BPW34 Silicon PIN Photodiode Datasheet. https://www.vishay.com/docs/81521/bpw34.pdf
