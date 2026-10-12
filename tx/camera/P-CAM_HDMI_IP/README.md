# P-CAM_HDMI_Test — Zybo Z7-20 + Pcam 5C 브링업 테스트 (C)

작성 2026-08-19 · 대상 보드 Digilent Zybo Z7-20 (XC7Z020-1CLG400C)
Vitis 2022.2 · standalone / `ps7_cortexa9_0` · **미검증 — 실물 확인 전**

Digilent `Zybo-Z7-20-pcam-5c` 예제의 **C++ PS 소프트웨어를 C로 변환**하고,
PZ7020 OV5640 테스트 코드의 파일 구성과 브링업 순서를 따라 재작성한 것이다.
Sobel을 비롯한 영상필터는 들어 있지 않다. **카메라가 살아 있는지 확인하는
최소 테스트 프로그램**이다.

---

## 1. 파일 구성

```
src/
  main.c                          브링업 순서 + 비블로킹 테스트 메뉴
  cam_gpio/       cam_gpio.c/.h       EMIO GPIO 1비트 = 센서 전원핀   [신규]
  iic_sccb_cfg/   iic_sccb_cfg.c/.h   PS I2C0 (XIicPs)               [PZ7020 그대로]
  ov5640/         OV5640.c/.h         센서 드라이버                   [신규 - MIPI용]
                  OV5640_REG.h        MIPI 레지스터 테이블 138개      [Digilent C++ 변환]
  mipi_rx/        mipi_rx.c/.h        D-PHY / CSI-2 리셋·인에이블     [신규]
  gamma/          gamma.c/.h          감마 계수 (= 10→8bpc 변환)      [신규]
  display_ctrl_hdmi/ display_ctrl.c/.h  VTC 설정                     [PZ7020 - dynclk 제거]
                     lcd_modes.h        해상도 테이블                 [PZ7020 그대로]
  vdma_api/       vdma_api.c/.h       VDMA 설정                       [PZ7020 그대로]
```

**PZ7020에서 그대로 가져온 것이 4개 모듈이다.** I2C, VDMA, 해상도 테이블은
보드와 무관한 코드이고, VTC 설정은 클럭 부분만 들어냈다.

`dynclk/`는 없다. 이 설계의 픽셀클럭은 Clocking Wizard 고정 74.25MHz이고
rgb2dvi가 5배 클럭을 내부 생성하므로, 소프트웨어가 클럭을 건드리는 코드가
한 줄도 없다.

---

## 2. C++ → C 변환에서 사라진 것

| C++ 원본 | C 변환 | 왜 |
|---|---|---|
| `OV5640` 클래스 + `I2C_Client`/`GPIO_Client` 추상 기반 | 모듈 + 직접 호출 | 버스 구현이 하나뿐인데 런타임 교체 기능을 유지할 이유가 없다 |
| `PS_IIC<T>`, `PS_GPIO<T>`, `AXI_VDMA<T>` 템플릿 | 각각 C 모듈 | 템플릿 인자 `T`가 인터럽트 컨트롤러였는데, 인터럽트를 안 쓴다 |
| `ScuGicInterruptController` | **없음** | 레지스터 트래픽은 부팅 때 수백 바이트, VDMA는 프리런. 폴링으로 충분 |
| `throw HardwareError` / `try-catch` | 반환값 검사 | 아래 설명 |
| `std::stringstream`, `snprintf` | `xil_printf` | C++ 스트림이 Zynq bare-metal에서 동작하지 않는 것은 원본 주석에도 적혀 있다 |

**예외를 반환값으로 바꾼 것은 단순 번역이 아니다.** 원본은 브링업 전체를
하나의 `try`로 감싸서 "브링업 중 뭔가 실패했다"만 알려 준다. C 버전은
단계마다 검사하므로 **버스가 죽었는지 칩 ID가 틀렸는지가 구분된다.**
`main.c`의 각 실패 메시지가 확인할 곳을 짚어 주는 것도 그래서 가능하다.

**인터럽트를 뺀 결과가 블록디자인에도 영향을 준다.** 소프트웨어가 VDMA
인터럽트를 쓰지 않으므로 `IRQ_F2P`와 `xlconcat`이 필요 없다. 구축 절차 문서
7.1 / 7.4절에서 그 둘을 빼도 된다.

---

## 3. 브링업 순서 — 이것이 이 프로그램의 핵심이다

```
1.  cam_gpio_init()          EMIO GPIO. 센서 전원핀을 소프트웨어가 쥔다
2.  mipi_rx_reset()          D-PHY / CSI-2 를 리셋 상태로 붙잡는다
3.  OV5640_Init()            PS I2C0 기동
4.  OV5640_PowerCycle()      센서 하드웨어 전원 재인가 (1초 off, 1초 on)
5.  OV5640_InitSensor()      칩 ID 확인 + 공통 테이블. 파워다운 상태로 끝난다
6.  run_vdma_frame_buffer()  VDMA 양방향 기동. DDR이 프레임을 받을 준비
7.  mipi_rx_enable()         D-PHY -> CSI-2 순서로 해제
8.  OV5640_SetMode720p()     ★ 여기서 비로소 센서가 스트리밍을 시작한다
9.  OV5640_SetAWB()
10. DisplayInitialize / SetMode / Start
```

**리셋은 소비자에서 생산자 쪽으로, 인에이블은 생산자에서 소비자 쪽으로.**
PZ7020에서는 순서가 크게 중요하지 않았다. DVP는 클럭과 데이터선이라 캡처가
늦게 시작하면 한 프레임 놓치고 다음 프레임을 잡으면 그만이다.

**MIPI CSI-2는 패킷 프로토콜이다.** 수신기가 패킷 헤더를 봐야 경계를 안다.
스트림 중간에 합류하면 스스로 복구하지 못한다. 그래서 센서를 가장 마지막에
깨운다. 8번을 앞으로 옮기면 **리셋 버튼으로는 안 고쳐지고 전원을 껐다 켜야
돌아오는 검은 화면**이 나온다. 처음부터 원인을 찾기 꽤 성가신 증상이다.

같은 원리가 `mipi_rx.c` 안에도 한 단계 더 적용되어 있다. 리셋은 CSI-2 먼저,
인에이블은 D-PHY 먼저다. 두 줄이 나란히 있어서 바꿔도 되어 보이지만 아니다.

---

## 4. 화면이 안 나오면 `t` 부터 누른다

`t`는 센서 내부의 컬러바 생성기를 켠다. **한 번의 키 입력으로 용의자가
반으로 준다.**

| 결과 | 의미 |
|---|---|
| **컬러바가 보인다** | SCCB · PLL · D-PHY · CSI-2 · 데모자이크 · VDMA · VTC · HDMI 전부 정상. 문제는 센서 앞쪽 — 렌즈캡, 초점, 조명 |
| **컬러바가 안 보인다** | 경로 안에 문제가 있다. 렌즈는 볼 필요 없다. `i`와 `c`로 내려간다 |

컬러바는 Bayer 포맷터 앞에서 생성되므로 RAW 데이터로 나와 PL에서 데모자이크를
거친다. **바 경계가 약간 무른 것은 정상이다.**

### 나머지 키

| 키 | 동작 |
|---|---|
| `w` | AWB 순환 (advanced / simple / off) |
| `v` | 상하 반전 — **베이어 위상이 틀어져 색이 깨진다. 의도한 것이다** (아래) |
| `m` | 좌우 반전 |
| `g` | 감마 계수 순환 (1.0 / 1.2 / 1.5 / 1.8 / 2.2) |
| `i` | 센서 주요 레지스터 덤프 |
| `c` | MIPI 수신 IP 버전 레지스터 |
| `e` | SCCB 에러 카운트 확인 후 초기화 |
| `?` | 도움말 |

`c`가 의외로 유용하다. 이 레지스터는 **MIPI 링크가 죽어 있어도 응답한다.**
버전이 제대로 읽히면 AXI-Lite 배선 · 주소맵 · `xparameters.h`가 서로 맞는다는
뜻이므로, 카메라를 보기 전에 용의자 세 개가 먼저 지워진다.

---

## 5. PZ7020과 달라진 센서 제어

PZ7020 드라이버에는 밝기 · 대비 · 채도 · 감마 · 컬러매트릭스 · 샤픈 · 노이즈
제거가 다 있었다. **여기엔 거의 없다.** 빠뜨린 게 아니다.

그 블록들은 전부 센서 **내부 ISP의 데모자이크 이후 단계**에 있다. 이 설계는
데모자이크 **이전**의 RAW Bayer를 가져오므로, 채도를 올려도 화면은 변하지
않는다. 데모자이크는 PL의 `AXI_BayerToRGB`가 한다.

남은 것은 RAW 경로에 실제로 작용하는 것들뿐이다.

- **AWB** — R/Gr/Gb/B 채널 게인을 조정하므로 우리가 뽑는 지점보다 앞이다. 작동한다
- **컬러바** — 포맷터 앞에서 생성된다. 작동한다
- **반전/미러** — 작동하지만 아래 주의

### 반전이 색을 깨뜨리는 이유

센서의 컬러필터 배열은 고정된 BG/GR 체커보드다. 리드아웃을 뒤집으면 첫 픽셀에
오는 색이 바뀌는데, PL의 `AXI_BayerToRGB`는 **한 가지 위상으로 고정 설정**되어
있다. 그래서 상하 반전은 화면을 뒤집는 동시에 빨강과 파랑을 바꾸거나 고운 색
격자를 만든다.

PZ7020에서는 이런 일이 없었다. 거기선 센서 내부 ISP가 데모자이크를 하면서
반전을 알아서 보정했기 때문이다. **데모자이크가 센서 밖으로 나오면서 생긴
비용**이고, 일부러 남겨 뒀다. "색이 격자로 보인다"가 실제로 어떤 그림인지
한 번 보여 주기 좋다.

---

## 6. Vitis 프로젝트 만들기

1. Create Platform Project — 구축 절차 문서 9절의 `.xsa` 선택,
   standalone on `ps7_cortexa9_0`
2. 플랫폼 Build
3. Create Application Project — **Empty Application (C)**
   - **C++가 아니라 C다.** 이 소스는 전부 C99다
4. 이 폴더의 `src/` 내용을 애플리케이션의 `src/`에 복사
5. Build

`lscript.ld`는 새 프로젝트가 만든 것을 쓴다. 복사해 넣지 않는다.

### 필요한 `xparameters.h` 심볼

플랫폼 빌드 후 아래가 없으면 블록디자인이 문서와 다르다.

```
XPAR_XIICPS_0_DEVICE_ID
XPAR_XGPIOPS_0_DEVICE_ID
XPAR_AXIVDMA_0_DEVICE_ID
XPAR_VTC_0_DEVICE_ID
XPAR_MIPI_D_PHY_RX_0_S_AXI_LITE_BASEADDR
XPAR_MIPI_CSI_2_RX_0_S_AXI_LITE_BASEADDR
XPAR_AXI_GAMMACORRECTION_0_S_AXI_BASEADDR
```

**BD를 고칠 때마다** 비트스트림 → XSA export → Vitis 플랫폼 rebuild 순서를
지킨다. 이 헤더는 `.xsa`에서 자동 생성된다.

---

## 7. 실행

1. JP6 = WALL, 5V 어댑터. JP5 = JTAG
2. HDMI 케이블은 **TX 포트**에 (RX가 바로 옆이다)
3. 시리얼 터미널 **115200 8N1** — Tera Term이나 PuTTY.
   Vitis 내장 터미널은 문자 단위 수신이 안 되는 경우가 있어 메뉴가 먹지 않는다
4. Program Device → Run As → Launch on Hardware
5. 기대 출력

```
=================================================
 Zybo Z7-20 + Pcam 5C (MIPI CSI-2) + HDMI  720p60
=================================================
  MIPI_D_PHY_RX  v1.x  @ 0x43C10000
  MIPI_CSI_2_RX  v1.x  @ 0x43C20000
OV5640 detected successful!
camera  : 1280x720 RAW10, 2 lane MIPI
display : 1280x720 @ 74.25 MHz fixed
frame buffer at 0x0A100000
```

---

## 8. 알아 둘 것

- **`OV5640_PowerCycle()`의 1초 x 2는 줄이지 않는다.** Digilent 원본 값이다.
  Pcam 레귤레이터가 방전되는 데 시간이 걸리고, 짧게 하면 **SCCB는 응답하는데
  MIPI는 안 깨어나는** 어중간한 상태가 된다
- **칩 ID가 0x5640으로 읽히는데 화면이 검다면** `cam_gpio` 쪽을 본다. SCCB
  블록과 MIPI 송신기는 전원 도메인이 달라서, 파워다운 상태에서도 ID는 읽힌다
- **화면 전체가 노랗고 테두리에 회색 선이 한 줄 보이는 것은 정상이다.**
  Digilent가 공식 인정한 알려진 현상이다. AWB 한계와 데모자이크 경계 처리다
- **`AXI_GammaCorrection`은 이름과 달리 뺄 수 없다.** `AXI_BayerToRGB`가
  컴포넌트당 10비트(32bit 스트림)를 내고, 이것을 8비트(24bit)로 줄이는 물건이
  설계에 이것 하나뿐이다. 감마 곡선은 룩업의 부수 효과다. 레지스터 리셋값 0이
  선형(1.0)이라 아무것도 안 써도 그림은 정상으로 나오는데, 그래서 "없어도 되는
  IP"로 오해하기 쉽다. 자세한 근거는 `gamma/gamma.h` 주석에 있다
- 720p 고정이다. `DisplaySetMode()`에 다른 모드를 넘기면 VTC는 바뀌지만
  픽셀클럭은 74.25MHz 그대로라 모니터가 동기를 잃는다. **`axi_dynclk`이
  무엇을 사 주고 있었는지 보여 주기 좋은 실험이다**

---

## 9. 필터를 붙이기 전에 — 채널 순서가 다르다

IP 소스를 끝까지 따라가 결론이 났다. **초판에서 "확인해 보라"고 적은 것을
"이렇게 다르다"로 확정한다.**

```vhdl
-- AXI_BayerToRGB.vhd 419행 : 출력 조립
m_axis_video_tdata <= "00" & Red(10) & Blue(10) & Green(10);

-- AXI_GammaCorrection.vhd 303행 : 10bpc -> 8bpc, 순서 유지
m_axis_video_tdata <= sGammaComponent(2) & (1) & (0);   -- = R & B & G

-- rgb2dvi.vhd 215~217행 : TMDS 채널 배정
pDataOut(2) <= vid_pData(23 downto 16); -- red is channel 2
pDataOut(1) <= vid_pData(7  downto 0);  -- green is channel 1   <-- [7:0]
pDataOut(0) <= vid_pData(15 downto 8);  -- blue is channel 0    <-- [15:8]
```

**`rgb2dvi`가 Xilinx 표준이 아닌 순서로 받는다.** 세 IP가 일관되게 `R B G`
규약을 쓰고 있어서 **화면 색은 정상으로 나온다.** Digilent 데모가 멀쩡한 이유가
이것이다.

| | `[23:16]` | `[15:8]` | `[7:0]` |
|---|---|---|---|
| Xilinx 표준 · **PZ7020 필터가 가정하는 것** | R | **G** | **B** |
| **Zybo Pcam 파이프라인** | R | **B** | **G** |

**G와 B가 서로 반대다.** PZ7020 필터를 그대로 끼우면 그레이스케일 계수가
뒤바뀌어 적용된다. 초록이 0.114를, 파랑이 0.587을 받는다.

증상은 고약하다. **화면이 깨지지 않는다.** 휘도만 미묘하게 틀어져서 눈으로는
알 수 없다. `PZ7020 영상필터 삽입 절차` 2절이 경고한 바로 그 상황이다.

**고치는 방법 두 가지**

1. 필터 RTL 안에서 G와 B 인덱스를 바꾼다 (권장)
2. 필터 앞뒤에 채널을 스왑하는 얇은 래퍼를 둔다

교육용으로는 1번이 낫다. "왜 보드가 바뀌면 같은 RTL이 안 맞는가"를 설명할
자리가 생긴다.

**확인 방법은 그대로 유효하다.** 순수 초록과 순수 파랑을 비추고 그레이 출력의
밝기를 비교한다. 이제는 **결과를 알고 하는 확인**이다.

| 입력 | 필터가 맞으면 | 안 고쳤으면 |
|---|---|---|
| 순수 초록 (0,255,0) | 압도적으로 밝다 (149) | 거의 검다 (29) |
| 순수 파랑 (0,0,255) | 거의 검다 (29) | 압도적으로 밝다 (149) |

**결과가 정반대로 나온다.** 10초면 판정된다.

---

## 10. 참고

- 구축 절차 : 프로젝트 문서 `Zybo-Z7-20 Pcam5C CAM-to-HDMI 구축 절차.md`
- 원본 C++ : `Zybo-Z7-20-pcam-5c/sdk/appsrc/pcam_vdma_hdmi/`
- 원본 C (DVP) : `20260811_PZ-7020_OV5640_Sobel/vitis_sobel/ov5640_sobel/src/`
