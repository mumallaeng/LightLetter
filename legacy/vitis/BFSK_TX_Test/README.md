# BFSK TX PS C API

Zybo Z7-20 / Zynq Cortex-A9 standalone / Vitis 2020.2용 드라이버.
다른 PS C 코드에서 `src/drv/bfsk_tx.h`를 include하고 `src/drv/bfsk_tx.c`, `src/hal/bfsk_tx_hal.c`를 함께 빌드한다.
`src/main.c`는 송신 완료 후 1초 대기하며 `0x41`을 반복 송신하는 예제다.
자체 `main()`이 있는 앱에 통합할 때는 예제 파일을 빌드에서 제외한다.

## 계층과 CNN 프로젝트 통합

```text
src/
├── main.c
├── drv/
│   ├── bfsk_tx.c
│   └── bfsk_tx.h
└── hal/
    ├── bfsk_tx_hal.c
    └── bfsk_tx_hal.h
```

```text
CNN 앱 / main.c        결과의 Character ID 결정, 호출 시점, 오류 처리
        ↓ bfsk_tx_send_byte()
bfsk_tx.c / .h         송신 순서, 상태 판정, timeout, 버퍼 처리
        ↓ bfsk_tx_hal_*()
bfsk_tx_hal.c / .h     BSP 주소, 32-bit MMIO, Global Timer 접근/시간 단위 변환
        ↓ Xil_In32 / Xil_Out32 / XTime_GetTime
Zynq PS → AXI4-Lite → BFSK PL
```

CNN 담당자는 다음 **4개 파일만** 가져가면 된다.

- `src/drv/bfsk_tx.h`, `src/drv/bfsk_tx.c`
- `src/hal/bfsk_tx_hal.h`, `src/hal/bfsk_tx_hal.c`

`src/drv`를 include 경로에 추가하고 두 `.c` 파일을 빌드에 등록한다. Driver의 HAL include는 `../hal/bfsk_tx_hal.h`를 사용하므로 `drv`와 `hal`은 같은 상위 폴더에 둔다.
`main.c`, `lscript.ld`, `Xilinx.spec`, 테스트 스텁은 복사하지 않고 CNN 프로젝트의
기존 main/링커/BSP 설정을 사용한다. 두 헤더는 표준 C 타입만 사용하며 C++ 호출을 지원한다.
Driver에는 Xilinx 헤더, UART 출력, sleep, 동적 메모리 할당이 없다.

아래 코드는 CNN 앱에 넣을 통합 예제다. 시작 시 초기화하고 결과가 확정될 때 송신한다.

```c
#include "bfsk_tx.h"

static bfsk_tx cnn_tx;

bfsk_tx_result cnn_tx_init(void)
{
    return bfsk_tx_init_default(&cnn_tx);
}

bfsk_tx_result cnn_tx_send_character(unsigned int character_id)
{
    if (character_id > 255U)
        return BFSK_TX_ERR_ARGUMENT;
    return bfsk_tx_send_byte(&cnn_tx, (uint8_t)character_id);
}
```

`cnn_tx_init()` 성공을 확인한 다음 `cnn_tx_send_character(0x41U)`를 호출하면 'A'를
송신한다. **CNN class index와 ASCII는 별개**다. 예를 들어 class 0이 'A'를 뜻하는 모델이면
CNN 앱이 label 표를 사용해 0을 `0x41`로 매핑해야 한다. Driver는 입력 바이트를 그대로 보낸다.
문자 매핑은 CNN 모델/수신부의 규약에 맞춰 앱에서 관리한다.

`bfsk_tx_send_byte()`는 정상 상태에서 약 57.6 ms 동안 송신 완료를 기다린다.
추론 실행을 계속해야 하는 앱은 결과를 큐에 넣고 단일 송신 태스크에서 호출하도록 통합한다.
현재 Driver 자체에는 큐나 비동기 API가 없다. 전송 성공은 PL 송신 완료를 뜻하며
광 수신부의 수신 성공/ACK를 뜻하지 않는다.

## 주소 설정과 버퍼 송신

`bfsk_tx_init_default()`는 HAL에서 BSP의 실제 IP 주소와 기본 timeout을 사용한다.
통합 디자인의 IP 이름이 바뀌면 HAL의 `BFSK_TX_BASEADDR`를 새 `xparameters.h` 심볼에
맞추거나 컴파일 옵션으로 정의한다. 이 매크로는 이제 공개 API 헤더에서 제공하지 않는다.
주소를 명시할 때는 기존 `bfsk_tx_init(&tx, base_address, timeout_us)`를 사용한다.
주소 타입은 Xilinx `UINTPTR` 대신 표준 `uintptr_t`이다.

초기화한 `bfsk_tx` 객체를 보관하면서 여러 번 송신해도 된다. `init`은 PL을 리셋하지 않는다.
문자열/바이너리 배열은 다음과 같이 송신한다. **각 바이트가 하나의 별도 프레임**이다.

```c
const uint8_t message[] = {'A', 'B', 'C'};
size_t sent;
bfsk_tx_result result = bfsk_tx_send_buffer(&cnn_tx, message, sizeof(message), &sent);
/* result != BFSK_TX_OK이면 sent는 완료를 확인한 바이트 수이다. */
```

`bfsk_tx_get_status(&tx, &status)`로 상태를 조회하고
`bfsk_tx_result_string(result)`로 오류를 출력할 수 있다.

## 확인한 하드웨어 계약

근거: 저장소 `rtl/tx/axi_lite_tx_wrapper.v`, `optical_tx_axi_top.v`, `tx_fsm.v`,
Vivado `design_1.bd`, 현재 BSP의 `xparameters.h` 및 `system.mss`.

| 항목 | 현재 설정 |
|---|---|
| AXI 주소 | BSP `XPAR_OPTICAL_TX_AXI_TOP_0_BASEADDR` = `0x43C00000` |
| `+0x00` DATA | R/W, 하위 8비트 Character ID |
| `+0x04` CTRL | bit 0에 1 쓰기 → START 펄스; 0 쓰기 불필요 |
| `+0x08` STATUS | bit 0 READY, bit 1 BUSY |
| AXI/TX clock | PS FCLK0 100 MHz |
| UART 로그 | PS UART1, 115200 baud, 8N1 |
| 출력 | optical_tx: JC1/V15, tx_enable: JC2/W15 (현재 XDC) |

READY=1, BUSY=0 확인 → DATA 쓰기/읽기 검증 → START → BUSY=1 확인 →
BUSY=0, READY=1 복귀 확인 순서다. 준비되지 않은 상태의 START는 RTL에서 무시된다.
Frame ID 증가, SFD 및 CRC 생성은 PL에서 수행한다.
PL 리셋 직후 첫 `0x41` 송신 프레임은 `D5 00 41 C0`이다.

주파수(10/20/25 kHz), symbol(1.6 ms), preamble(4 symbols)은 현재 RTL 파라미터다.
현재 레지스터에는 주파수 변경, 송신 취소, 소프트 리셋, 완료 인터럽트, Frame ID 조회 기능이 없다.
해당 기능이 필요하면 RTL/레지스터 맵 확장이 필요하다.

## 실행 조건과 오류 처리

1. 현재 XSA와 일치하는 bitstream을 PL에 로드한다.
2. Vitis 실행 설정의 PS 초기화(`ps7_init`/post-config)로 DDR/FCLK/reset을 준비한다.
3. standalone BSP와 함께 ELF를 실행한다. BSP 시작 코드가 Global Timer를 시작한다.
4. UART 완료 로그와 JC1/JC2 파형을 확인한다. 프레임 시간은 약 57.6 ms다.

기본 제한 시간은 READY/START/DONE **각 단계당 250 ms**다.
반환값은 준비 타임아웃, 데이터 읽기 불일치, 시작 타임아웃, 완료 타임아웃을 구분한다.
오류 시 `tx.last_status`는 마지막으로 읽은 STATUS이다.
송신 함수는 blocking이며 같은 IP를 여러 태스크/코어/ISR에서 동시에 호출하면 안 된다.
버퍼 전체의 최악 대기 시간은 바이트 수에 비례한다.

타임아웃은 이미 진행 중인 송신을 중단하지 않는다. 오류 바이트는 실제로 송신되었을 수
있으므로 무조건 재시도하지 말고 상태/하드웨어를 확인한다. BUSY 전체 구간 동안 PS가
중단되면 완료된 송신도 START 타임아웃으로 보고할 수 있다(현재 RTL에 sticky DONE 없음).
타임아웃은 MMIO 읽기가 반환되고 Global Timer가 실행 중일 때 동작한다.
PL 미구성/AXI 응답 정지로 MMIO 자체가 멈추는 문제는 이 드라이버로 복구할 수 없다.

## 빌드와 검증

Vitis에서 프로젝트 Refresh 후 Build Project. 하위 폴더의 Driver/HAL 소스도 자동으로 빌드한다.
호스트 전용 `tests` 폴더는 Debug/Release 타깃 소스에서 제외했다.

현재 환경에서 재현할 수 있는 PowerShell 빌드:

```powershell
$env:Path = 'C:\Xilinx\Vitis\2020.2\gnu\aarch32\nt\gcc-arm-none-eabi\bin;C:\Xilinx\Vitis\2020.2\gnuwin\bin;' + $env:Path
& 'C:\Xilinx\Vitis\2020.2\gnuwin\bin\make.exe' -C Debug main-build
```

호스트 회귀 검사(프로젝트 폴더에서 GCC 사용):

```powershell
gcc -std=c11 -Wall -Wextra -Werror -Itests/stubs -Isrc/drv -Isrc/hal tests/test_bfsk_tx.c src/drv/bfsk_tx.c src/hal/bfsk_tx_hal.c -o Debug/test_bfsk_tx.exe
if ($LASTEXITCODE -ne 0) { throw 'Test build failed' }
& ./Debug/test_bfsk_tx.exe
```

2026-09-21 검증: Vitis ARM 컴파일/링크 성공(`Debug/BFSK_TX_Test.elf`).
모의 MMIO/타이머 검사 통과: 정상/지연 handshake, 각 타임아웃, readback 실패,
바이너리 버퍼, 부분 완료, 잘못된 인수, 상태 조회, timer wrap, 짧은 timeout.
HAL 분리 후 같은 회귀 검사와 ARM 빌드를 실행하고 기본 초기화 및 HAL 시간 변환도 확인했다.
이 결과는 실제 보드/AXI/광출력 검증을 대신하지 않으며 TX-9B 하드웨어 PASS는 미확인이다.

