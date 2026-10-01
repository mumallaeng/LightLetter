# NUCLEO-F411RE BFSK TX 검사기

사용자가 지정한 `f411re_c04`를 NUCLEO-F411RE 대상으로 해석한 펌웨어다.
FPGA의 **JC1 디지털 BFSK 출력**을 TIM2 Input Capture로 받아 원래 바이트와 CRC를 확인한다.
폴더 이름은 `Nucleo_tx_test`지만 뉴클레오의 역할은 **수신/검사**이다.
ADC 고속 샘플링 없이 상승 에지의 시간 차이를 측정한다.

## 먼저 실행하기

1. 뉴클레오의 ST-LINK USB와 Zybo의 USB/전원을 각각 연결한다.
2. 아래 표대로 **신호 1개 + GND**를 연결한다.
3. 이 폴더의 터미널에서 `make`로 빌드한다.
4. `make run`으로 ST-LINK/SWD 다운로드·검증·Reset을 수행한다.
   실습 폴더 `3.ARM_Lab/1703.SR04`와 같은 STM32CubeProgrammer CLI 방식이다.
   이 명령은 뉴클레오에 있던 기존 펌웨어를 교체한다.
5. 뉴클레오의 ST-LINK Virtual COM Port를 **115200 / 8N1 / 흐름 제어 없음**으로 연다.
   COM을 연 뒤 Reset하면 부팅 안내부터 볼 수 있다. Zybo의 COM 포트와 구분한다.
6. Vitis에서 `BFSK_TX_Test`를 실행해 FPGA가 `0x41`을 반복 송신하게 한다.
7. 뉴클레오 터미널에서 `PASS ... ASCII='A'`와 `capture_loss=0`을 확인한다.

다운로드 파일은 로컬에서 빌드해 생성했다. **실보드 다운로드/수신은 아직 실행하지 않았다.**
`build/`는 Git 제외이므로 다른 PC에 소스를 전달하면 아래 빌드 명령으로 생성한다.

## 배선

| FPGA Zybo | NUCLEO-F411RE | 의미 |
|---|---|---|
| PMOD JC 1번 `optical_tx` | Arduino **A0 = PA0**, CN8 1번 (또는 CN7 28번) | TIM2_CH1 입력 |
| 보드 GND / PMOD GND | 뉴클레오 GND | 공통 기준 전압 |
| PMOD JC 2번 `tx_enable` | 연결 안 함 | 이 디코더는 SYNC 신호로 동기화 |

각 보드는 자체 USB/전원으로 공급한다. 전원 핀끼리는 연결할 필요가 없다.
입력은 현재 XDC의 **3.3 V LVCMOS 출력**이다. LED 드라이버 출력이나 BPW34/TIA 아날로그 출력을
여기에 바로 연결하는 검사가 아니다. 실제 광수신 검사는 별도 아날로그 전처리가 필요하다.
PA0/TIM2_CH1과 Arduino 핀 매핑은 [STM32F411 데이터시트](https://www.st.com/resource/en/datasheet/stm32f411re.pdf) 및
[NUCLEO 보드 설명서 UM1724](https://www.st.com/resource/en/user_manual/um1724-stm32-nucleo64-boards-mb1136-stmicroelectronics.pdf)를 기준으로 한다.

## UART 출력 읽기

PL 리셋 직후 첫 프레임을 받은 경우의 **예상 예시**:

```text
NUCLEO-F411RE BFSK TX monitor
Input: A0/PA0 TIM2_CH1; UART: 115200 8N1
Expect: SYNC x4, D5 ID DATA CRC; connect GND
PASS raw=D5 00 41 C0 ASCII='A' CRC=C0/C0 Hz[0,1,S]=10000,20000,25000
STATUS edges=... pass=1 fail=0 sync_errors=0 capture_loss=0
```

- `raw`: SFD, Frame ID, DATA, 수신 CRC. 이후 Frame ID와 CRC는 바뀐다.
- `CRC=수신값/계산값`: SFD가 D5이고 CRC가 일치하면 PASS.
- `Hz[0,1,S]`: 0/1/SYNC에서 측정한 평균 주파수. HSI 기준이므로 정확히 위 숫자일 필요는 없다.
- `ASCII`: 0x20~0x7E는 문자, 나머지는 `.`으로 표시한다. 실제 값은 raw로 확인한다.
- `pass`/`fail`: 완성된 프레임 중 SFD/CRC 검사 통과/실패 누적 수.
- `sync_errors`: 불명확한 심볼이나 프레임 중 긴 에지 공백. 다음 프레임에서 다시 동기화한다.
- `capture_loss`: 타이머 overcapture 또는 소프트웨어 큐 초과. 증가하면 해당 수집 상태를 버린다.
- 녹색 LD2(PA5)는 PASS 프레임마다 토글된다.

PASS는 수신한 디지털 비트/CRC 확인이다. 아날로그 파형 품질, LED 광출력, 전체 심볼 길이를
정밀 계측하는 판정은 아니다. 마지막 비트 중앙 구간까지 측정하면 프레임을 보고한다.

## 파일 구조

```text
Nucleo_tx_test/
├── src/main.c                 UART 결과 출력, 검사 통계
├── src/drv/bfsk_rx.c/.h        MCU와 독립적인 BFSK/CRC 디코더
├── src/hal/nucleo_board.c/.h   F411RE 클록, TIM2, 큐, USART2, 인터럽트
├── config/                    HAL 설정, F411RE 링커 스크립트
├── tests/                     RTL 타이밍을 모델링한 상승 에지 테스트
├── Makefile                   make / make run / make test / make clean
└── build/                     ELF / HEX / BIN 및 중간 산출물 (Git 제외)
```

## 빌드

실습과 같은 **Windows GNU Make + Arm GCC + STM32CubeProgrammer CLI**를 사용한다.
PowerShell/CMD에서 이 폴더로 이동한 뒤 실행한다. 별도 `.ps1` 빌드 스크립트는 제거했다.

```powershell
make            # ELF / BIN / HEX 생성
make run        # 필요하면 ELF 빌드 후 ST-LINK 다운로드, 검증, Reset
make test       # PC에서 디코더 테스트 (보드 불필요)
make clean      # build 폴더의 생성 파일 정리
```

`make flash`도 `make run`과 같고, `make -j4` 병렬 빌드도 가능하다.
기본 명령 `make`는 빌드만 한다. `make run`/`make flash`만 보드에 다운로드한다.
`make test`에는 PC용 GCC가 필요하다. STM32CubeProgrammer GUI를 직접 사용할 필요는 없다.

펌웨어 의존성은 **Arm GNU Toolchain**과 **STM32CubeF4**다. ST HAL/CMSIS 코드는 복제하지 않고
로컬 Cube 패키지의 원본/라이선스를 사용한다. 기본 Cube 경로:
`%USERPROFILE%/STM32Cube/Repository/STM32Cube_FW_F4_V1.28.3`.
빌드 방식은 실습과 같지만 현재 보드 초기화 코드는 ST HAL을 사용하므로 CubeF4 패키지가 필요하다.

기본 도구 경로는 이 PC의 실제 설치를 기준으로 했다.

- `TOOL_DIR`: `C:/arm-gnu-toolchain-15.2.rel1-mingw-w64-i686-arm-none-eabi`
- `PROGRAMMER`: `C:/Program Files/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin/STM32_Programmer_CLI.exe`

경로가 다르면:

```powershell
make TOOL_DIR=C:/path/arm-gnu-toolchain CUBE_ROOT=C:/path/STM32Cube_FW_F4_V1.28.3
make run "PROGRAMMER=C:/path/STM32_Programmer_CLI.exe"
```

`CUBE_ROOT`는 공백 없는 경로를 사용한다. 도구 경로는 공백을 지원한다.
ST-LINK가 여러 개면 `make run STLINK_SN=해당보드의시리얼번호`로 대상을 지정한다.
GUI 다운로드도 가능하다. `build/Nucleo_tx_test.hex`/`.elf`는 주소가 포함되며,
`.bin`의 시작 주소는 **0x08000000**이다.

검증 환경: Arm GNU GCC 15.2.1, STM32CubeF4 1.28.3, 호스트 GCC.
현재 빌드에서 ST HAL의 unused-parameter 및 bare-metal newlib syscall stub 관련 경고가 발생할 수 있다.
UART 출력은 `_write`/stdout 대신 `HAL_UART_Transmit`을 직접 사용한다.
`make clean` 후 재빌드와 `make test`를 확인했다. 다운로드 명령은 `make -n run`으로만 확인했다.

## 구현/설정값

| 항목 | 설정 |
|---|---|
| SYSCLK | HSI 16 MHz → PLL → 100 MHz, 외부 HSE/ST-LINK 클록 불필요 |
| APB1 / TIM2 clock | 50 MHz / 100 MHz |
| TIM2 | 32-bit upcounter, PSC=99, ARR=0xFFFFFFFF, 1 us/tick |
| TIM2_CH1 | PA0 AF1, rising edge, capture prescaler DIV1, filter 0, IRQ |
| UART | USART2 PA2/PA3 AF7, 115200 8N1, 기본 ST-LINK VCP 경로 |
| Queue | 2048 timestamp slots; ISR에서 저장, main에서 디코드/로그 |
| 송신 규격 | 25 kHz SYNC 4개 + 32비트, MSB first, 약 1.6 ms/symbol |
| CRC | ID+DATA에 CRC-8 poly 0x07, init/xorout 0, 반사 없음 |

약 145개의 연속 25 kHz 주기로 SYNC를 찾고 평균 주기를 이용해 심볼 시간을 추정한다.
심볼 중앙 25~75%의 주기들을 다수결로 판별하므로 전환 경계의 혼합 주기를 피한다.
100/50/40 us가 각각 BIT0/BIT1/SYNC에 해당한다. 동작 원리는 ST의
[Timer Input Capture 안내 AN4776](https://www.st.com/resource/en/application_note/an4776-how-to-use-generalpurpose-timer-peripheral-on-stm32-mcus-stmicroelectronics.pdf)를 참고했다.

## CubeIDE에 넣으려면

이 폴더는 Makefile/GCC로 독립 빌드하는 소스이며 `.ioc`/CubeIDE 프로젝트 파일은 포함하지 않는다.
기본 사용에는 CubeIDE가 필요하지 않다.
CubeIDE에서는 STM32F411RE 프로젝트를 만든 뒤 `src/`와 `config/`를 추가할 수 있다.
이 경우 기존 `main.c`는 제외하고, **SysTick_Handler/TIM2_IRQHandler 중복 정의**를 제거한다.
현재 `nucleo_board.c`가 초기화와 두 IRQ를 모두 제공하므로 Cube 생성 TIM/UART 초기화를 중복 호출하지 않는다.
`config`를 HAL 설정 include 경로로 지정하고 프로젝트의 다른 HAL config와 중복되지 않게 한다.
startup/system/HAL은 F411xE용으로 포함하고 `STM32F411xE`, `USE_HAL_DRIVER`를 정의한다.

## 결과가 안 나올 때

| 증상 | 확인할 항목 |
|---|---|
| 부팅/STATUS 로그 없음 | 뉴클레오 COM 선택, baud, Reset, ST-LINK USART2 연결/솔더브리지 |
| edges가 계속 0 | FPGA 송신 실행, JC1→A0 배선, 공통 GND |
| edges 증가, pass=0 | 주파수/심볼 RTL 설정, 중간 연결 후 다음 프레임까지 대기 |
| sync_errors 증가 | 신호 누락/잡음, TIM2 tick 설정, 다른 송신 포맷 |
| capture_loss 증가 | CPU 중단/긴 인터럽트 금지 구간, 디버거 breakpoint |
| FAIL 프레임 | raw/SFD/CRC 값과 송신 데이터를 대조 |

## 검증 범위

호스트 테스트: 알려진 `D5 00 41 C0`, 256 payload, CRC/SFD 오류 검출, ±1 us 에지 jitter,
±4% 타이머 속도 편차 모델, uint32 wrap, 잘못된 심볼, 중간 연결, 잘린 프레임, 연속 프레임 재동기화.
F411RE용 ELF/HEX/BIN 컴파일·링크도 확인했다.
실제 보드의 HSI/신호 품질, ISR 최대 처리량, 광수신 경로는 검증하지 않았다.
