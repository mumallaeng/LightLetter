# FPGA BFSK Optical Communication Project — Progress

> **Single Source of Truth**
>
> 이 파일은 프로젝트 전체 진행상황과 통신 규격을 관리하는 기준 문서이다.
> 설계 변경, PASS/FAIL, 인터페이스 변경, 주파수 변경이 발생하면 이 파일을 우선 갱신한다.
>
> 기준일: **2026-09-16**
>
> 현재 상태: **BFSK 기반 광통신 구조 확정 / TX-1 CRC-8 PASS / TX-2 Frame Generator PASS / TX-3 BFSK Mapper Vivado/XSim PASS / TX-4 Carrier Generator 개발 대기**

---

# 0. 채팅 운영 규칙

| 채팅 | 용도 |
|---|---|
| **Master** | 전체 구조, 일정, 역할, 주요 설계 결정 |
| **01. 코드 작성** | RTL / Testbench 구현 및 수정 |
| **02. 진행사항 업데이트** | PASS/FAIL, Git, 완료 항목, 진행률 기록 |
| **03. 질문** | BFSK, FFT, XADC, Preamble, Packet 등 개념/설계 질문 |

---

# 1. 프로젝트 목표

Pcam으로 입력된 문자를 FPGA 기반 EMNIST/CNN으로 인식하고, 인식된 Character ID를 BFSK 광신호로 전송한다.

수신 FPGA에서는 Photodiode와 XADC를 통해 광신호를 Sample한 뒤 FFT를 수행하고, BFSK 주파수 성분을 판별하여 원래 Character ID를 복원한다.

최종적으로 복원된 Character를 UART 또는 HDMI 출력 경로로 전달한다.

---

# 2. 전체 시스템 구조

```text
[TX]

Pcam
↓
Image Preprocessing
↓
EMNIST / CNN
↓
Character ID
↓
Frame / Packet Generator
↓
CRC-8
↓
Preamble
↓
BFSK Mapper
↓
Carrier Generator
↓
2N7000
↓
LED / Laser

==============================
       Optical Link Only
==============================

[RX FRONT-END / FFT PART]

BPW34
↓
MCP6022 TIA / Gain
↓
Zybo Z7-20 XADC
↓
DC Offset Removal
↓
128 x 2 Ping-Pong Sample Buffer
↓
128-point FFT
↓
Re² + Im²
↓
128-bin Magnitude Stream

==============================

[RX TOP]

rx_bin_capture
↓
rx_symbol_detector
↓
rx_symbol_sync
↓
rx_frame_decoder
↓
rx_crc8_check
↓
rx_output_buffer
↓
UART / System Output
```

---

# 3. 역할 분담

## 3.1 TX 담당

```text
CNN Result Interface
↓
Character Latch
↓
Frame / Packet Generator
↓
CRC-8
↓
Preamble Generator
↓
BFSK Mapper
↓
Carrier Generator
↓
Optical TX Interface
```

주요 RTL 후보:

```text
crc8.v
tx_frame_generator.v
bfsk_mapper.v
bfsk_carrier_gen.v
tx_fsm.v
optical_tx_top.v
```

## 3.2 RX FFT 파트 담당

```text
XADC
↓
DC Removal
↓
128 x 2 Ping-Pong Buffer
↓
128-point FFT
↓
Magnitude / Power
↓
128-bin Stream
```

주요 RTL 후보:

```text
xadc_interface.v
rx_dc_remover.v
rx_sample_block_buffer.v
fft_wrapper_128.v
fft_magnitude.v
```

## 3.3 RX TOP 담당

RX TOP은 XADC와 FFT 자체를 포함하지 않는다.

```text
FFT magnitude stream
↓
rx_bin_capture
↓
rx_symbol_detector
↓
rx_symbol_sync
↓
rx_frame_decoder
↓
rx_crc8_check
↓
rx_output_buffer
↓
uart_tx
```

주요 RTL 후보:

```text
rx_top.v
rx_bin_capture.v
rx_symbol_detector.v
rx_symbol_sync.v
rx_frame_decoder.v
rx_crc8_check.v
rx_output_buffer.v
uart_tx.v
```

---

# 4. 현재 확정 통신 규격

```text
BFSK
Bit 0 → f0     = 10 kHz
Bit 1 → f1     = 20 kHz
SYNC  → f_sync = 25 kHz
```

현재 주파수는 초기 구현 기준값이며 실제 광원 / BPW34 / MCP6022 응답 측정 후 변경 가능하다.

---

# 5. ADC / FFT 규격

```text
XADC Sampling Rate Fs = 160 kSample/s
FFT Size N             = 128 point
```

FFT Bin Resolution:

```text
Δf = Fs / N
   = 160000 / 128
   = 1,250 Hz
```

주요 Bin:

```text
10 kHz → Bin 8
20 kHz → Bin 16
25 kHz → Bin 20
```

---

# 6. Sample / FFT / Symbol 시간 규격

```text
1 Sample = 6.25 us
128 Samples = 0.8 ms = FFT Block 1개
256 Samples = 1.6 ms = FFT Block 2개 = Symbol 1개
```

따라서:

```text
2 FFT Results
→ 1 BFSK Symbol Decision
```

---

# 7. Idle / Preamble 규격

Idle:

```text
IDLE = No Carrier
```

Preamble:

```text
f_sync = 25 kHz
FFT Bin = 20
PREAMBLE_SYMBOLS = 4
```

시간:

```text
4 Symbol × 1.6 ms = 6.4 ms
4 Symbol = 1024 Samples
4 Symbol = 8 FFT Blocks
```

송신 순서:

```text
IDLE
↓
SYNC
↓
SYNC
↓
SYNC
↓
SYNC
↓
SFD
↓
Frame ID
↓
DATA
↓
CRC
↓
IDLE
```

---

# 8. Frame Format

```text
┌────────────┬────────┬──────────┬────────┬────────┐
│ PREAMBLE   │ SFD    │ FRAME ID │ DATA   │ CRC-8  │
├────────────┼────────┼──────────┼────────┼────────┤
│ 4 Symbol   │ 8 bit  │ 8 bit    │ 8 bit  │ 8 bit  │
└────────────┴────────┴──────────┴────────┴────────┘
```

SFD:

```text
SFD = 8'hD5
Binary = 1101_0101
```

DATA:

```text
char_id[7:0]
```

Frame ID:

```text
8 bit Packet Number
```

---

# 9. CRC 규격

```text
CRC Type : CRC-8
POLY     : 8'h07
INIT     : 8'h00
XOROUT   : 8'h00
REFIN    : false
REFOUT   : false
ORDER    : MSB First
```

CRC 계산 대상:

```text
Frame ID + DATA
```

CRC 계산 제외:

```text
SYNC
SFD
수신 CRC Byte
```

Test Vector:

```text
Frame ID = 8'h00
DATA     = 8'h41
CRC      = 8'hC0
```

---

# 10. Bit Order

모든 Byte는 MSB First.

```text
bit[7] → ... → bit[0]
```

---

# 11. TX Interface

## CNN → TX

| Signal | Dir | Width | 설명 |
|---|:---:|---:|---|
| `char_id` | IN | 8 | CNN 인식 Character ID |
| `char_valid` | IN | 1 | Character ID 유효 |
| `char_ready` | OUT | 1 | TX 통신부 입력 가능 |
| `tx_busy` | OUT | 1 | 현재 Frame 송신 중 |

Handshake:

```text
char_valid && char_ready
```

## Packet Generator → BFSK Mapper

| Signal | Dir | Width | 설명 |
|---|:---:|---:|---|
| `tx_bit` | OUT | 1 | 송신 Bit |
| `tx_bit_valid` | OUT | 1 | Bit 유효 |
| `tx_bit_ready` | IN | 1 | Mapper 수신 가능 |
| `frame_start` | OUT | 1 | TX Frame 시작 |
| `frame_done` | OUT | 1 | Frame 송신 완료 |

## TX FSM → BFSK Mapper

| Signal | Dir | Width | 설명 |
|---|:---:|---:|---|
| `sync_valid` | IN | 1 | TX FSM의 SYNC Symbol 요청 |
| `sync_ready` | OUT | 1 | Mapper가 SYNC 요청을 받을 수 있음 |

Handshake:

```text
sync_valid && sync_ready
```

SYNC와 DATA 요청이 동시에 들어오면 **SYNC가 우선**한다.

## BFSK Mapper → Carrier Generator

| Signal | Dir | Width | 설명 |
|---|:---:|---:|---|
| `symbol_type` | OUT | 2 | IDLE / BIT0 / BIT1 / SYNC |
| `symbol_valid` | OUT | 1 | Symbol 유효 |
| `symbol_start` | OUT | 1 | Symbol 시작 Pulse |
| `symbol_done` | IN | 1 | Carrier 출력 완료 |

Encoding:

```text
2'b00 → IDLE
2'b01 → BIT0 / 10 kHz
2'b10 → BIT1 / 20 kHz
2'b11 → SYNC / 25 kHz
```

## Carrier Generator → Driver

| Signal | Dir | Width | 설명 |
|---|:---:|---:|---|
| `optical_tx` | OUT | 1 | BFSK Carrier |
| `tx_enable` | OUT | 1 | 광원 송신 Enable |

---

# 12. TX Timing 기준

```text
SYMBOL_SAMPLES = 256
SYMBOL_TIME    = 1.6 ms
```

예: 100 MHz TX Clock이면:

```text
1.6 ms = 160,000 Clock Cycles
```

실제 TX Clock에 맞게 parameter화한다.

---

# 13. XADC → FFT 앞단

Sample 저장 조건:

```verilog
if (xadc_sample_valid) begin
    sample_buffer[write_index] <= centered_sample;
end
```

DC 제거:

```verilog
wire signed [12:0] centered_sample =
    $signed({1'b0, xadc_sample}) -
    $signed({1'b0, dc_estimate});
```

Ping-Pong Buffer:

```text
Bank A: XADC Write
Bank B: FFT Read
→ Swap
```

각 Bank = 128 Samples.

FFT 입력:

```verilog
output wire signed [SAMPLE_W-1:0] fft_s_data;
output wire                       fft_s_valid;
input  wire                       fft_s_ready;
output wire                       fft_s_last;
```

sample 127에서 `fft_s_last=1`.

128 Sample 처리 완료 후 메모리 값 자체는 지우지 않고 index/full 상태만 갱신한다.

Buffer Overflow 발생 시:

```text
현재 Packet 폐기
Symbol Pairing 폐기
SEARCH_SYNC 복귀
```

---

# 14. FFT → RX TOP Interface

현재 채택안: 128개 Magnitude 순차 Stream.

```verilog
input  wire                 fft_mag_valid;
output wire                 fft_mag_ready;
input  wire                 fft_mag_last;
input  wire [6:0]           fft_bin_index;
input  wire [MAG_W-1:0]     fft_mag;
input  wire                 fft_frame_error;
```

Handshake:

```verilog
wire fft_mag_fire = fft_mag_valid && fft_mag_ready;
```

`valid=1 && ready=0`이면 `fft_mag`, `fft_bin_index`, `fft_mag_last` 유지.

---

# 15. rx_bin_capture

관심 Bin만 저장:

```text
Bin 8
Bin 16
Bin 20
```

Output:

| Signal | Width | 의미 |
|---|---:|---|
| `block_mag8` | `MAG_W` | Bin 8 Magnitude |
| `block_mag16` | `MAG_W` | Bin 16 Magnitude |
| `block_mag20` | `MAG_W` | Bin 20 Magnitude |
| `block_valid` | 1 | FFT Block 완료 Pulse |
| `block_invalid` | 1 | Bin 누락 / Last 오류 / FFT 오류 |

---

# 16. 256-Sample Symbol Detector

두 FFT Block 결과를 합산:

```text
sum_bin8  = bin8_0  + bin8_1
sum_bin16 = bin16_0 + bin16_1
sum_bin20 = bin20_0 + bin20_1
```

합산 Width:

```text
MAG_W + 1
```

Symbol Encoding:

```verilog
localparam [1:0] SYMBOL_BIT0    = 2'b00;
localparam [1:0] SYMBOL_BIT1    = 2'b01;
localparam [1:0] SYMBOL_SYNC    = 2'b10;
localparam [1:0] SYMBOL_INVALID = 2'b11;
```

판정:

```text
sum_bin8 단독 최대  → BIT0
sum_bin16 단독 최대 → BIT1
sum_bin20 단독 최대 → SYNC
동률 / 오류          → INVALID
```

동률 방지를 위해 `>` 사용, `>=` 사용 금지.

`symbol_valid=1, symbol_code=INVALID`은 판정 실패 결과를 의미한다.

첫 FFT Block이 Invalid여도 두 번째 FFT Block까지 소비 후 Symbol 하나를 INVALID 처리한다.

---

# 17. Threshold 정책

초기 기능 검증은 Threshold 없이 단독 최대 비교 가능.

단 실제 Hardware에서는 신호 없음 상태에서도 승자가 발생하므로 최소 SYNC Threshold 검토 필요.

예:

```verilog
wire sync_candidate =
    (sum_bin20 > sum_bin8) &&
    (sum_bin20 > sum_bin16) &&
    (sum_bin20 > SYNC_THRESHOLD);
```

추후 개선:

```text
Minimum Threshold
Winner / Runner-up Margin
중심 Bin + 인접 Bin Energy 합산
```

---

# 18. Symbol Sync / Pairing

1 Symbol = FFT Block 2개.

1차 구현은 TX/RX 시작 경계가 정렬되었다고 가정하고 고정 Pairing 사용.

```text
(0,1) → Symbol 0
(2,3) → Symbol 1
(4,5) → Symbol 2
```

추후 실제 독립 Clock 환경에서:

```text
Pairing A/B 비교
Overlap FFT
Timing Sync
```

검토.

---

# 19. SYNC Detection

```text
SYNC #1
SYNC #2
SYNC #3
SYNC #4
↓
frame_start 1-Clock Pulse
```

중간에 BIT0 / BIT1 / INVALID 발생 시:

```text
sync_count = 0
```

네 번째 SYNC 다음 Symbol부터 `SFD[7]` 수신.

SYNC Tone만으로 내부 경계를 완벽히 알 수 없으므로 SFD `0xD5` 검사로 최종 검증.

---

# 20. INVALID 처리 정책

SEARCH_SYNC:

```text
SYNC    → sync_count 증가
BIT0    → sync_count = 0
BIT1    → sync_count = 0
INVALID → sync_count = 0
```

Frame 수신 중:

```text
BIT0/BIT1 → 정상
SYNC       → Packet Abort
INVALID    → Packet Abort
```

Abort 후:

```text
Bit Counter 초기화
Frame Register 초기화
SEARCH_SYNC 복귀
```

---

# 21. RX Frame Decoder FSM

```text
SEARCH_SYNC
    |
    +-- SYNC x4
    v
READ_SFD
    |
    v
CHECK_SFD
    |-- FAIL → PACKET_ERROR → SEARCH_SYNC
    |
    +-- PASS
         v
READ_FRAME_ID
         v
READ_DATA
         v
READ_CRC
         v
CHECK_CRC
    |-- FAIL → PACKET_ERROR → SEARCH_SYNC
    |
    +-- PASS
         v
CHECK_FRAME_ID (Optional)
         v
OUTPUT
         v
SEARCH_SYNC
```

Frame FSM은 `symbol_valid=1`이고 BIT0/BIT1일 때만 Bit Counter 진행.

---

# 22. frame_start 초기화 범위

초기화:

```text
SFD Shift Register
Frame ID Register
DATA Register
Received CRC Register
Frame Bit Counter
CRC Calculation Register
```

초기화하지 않음:

```text
XADC
Sample Ping-Pong Buffer
FFT
FFT Block Order
Symbol Pairing
```

---

# 23. Output Buffer / UART

Output Buffer Interface:

```verilog
output wire [7:0] rx_frame_id;
output wire [7:0] rx_char_id;
output wire       rx_char_valid;
input  wire       rx_char_ready;
```

CRC PASS 후에만 데이터 저장 및 Valid Assert.

UART 초기 설정:

```text
115200 baud
8 data bit
No parity
1 stop bit
```

UART 송신 중에도 XADC / Buffer / FFT는 계속 동작.

---

# 24. RX 주요 내부 인터페이스

## FFT → RX TOP

| Signal | Width | 의미 |
|---|---:|---|
| `fft_mag` | `MAG_W` | 현재 Bin의 `Re²+Im²` |
| `fft_bin_index` | 7 | Bin 0~127 |
| `fft_mag_valid` | 1 | 현재 Bin 유효 |
| `fft_mag_ready` | 1 | RX TOP 수신 가능 |
| `fft_mag_last` | 1 | Bin 127 / FFT Block 종료 |
| `fft_frame_error` | 1 | FFT Block 오류 |

## Bin Capture → Symbol Detector

| Signal | Width | 의미 |
|---|---:|---|
| `block_mag8` | `MAG_W` | Bin 8 |
| `block_mag16` | `MAG_W` | Bin 16 |
| `block_mag20` | `MAG_W` | Bin 20 |
| `block_valid` | 1 | FFT Block 완료 Pulse |
| `block_invalid` | 1 | FFT Block 오류 |

## Symbol Detector → Symbol Sync / Decoder

| Signal | Width | 의미 |
|---|---:|---|
| `symbol_code` | 2 | BIT0 / BIT1 / SYNC / INVALID |
| `symbol_valid` | 1 | 256-Sample Symbol 판정 완료 |

## Symbol Sync → Frame Decoder

| Signal | Width | 의미 |
|---|---:|---|
| `frame_start` | 1 | SYNC 4회 후 Pulse |
| `symbol_locked` | 1 | Symbol Pairing 상태 |

## Frame Decoder → CRC

| Signal | Width | 의미 |
|---|---:|---|
| `rx_frame_id` | 8 | Frame ID |
| `rx_char_id` | 8 | DATA |
| `received_crc` | 8 | 수신 CRC |
| `decode_valid` | 1 | Frame 수신 완료 |

## CRC → Output Buffer

| Signal | Width | 의미 |
|---|---:|---|
| `rx_frame_id` | 8 | Frame ID |
| `rx_char_id` | 8 | DATA |
| `frame_valid` | 1 | 정상 Frame |
| `crc_ok` | 1 | CRC Match |
| `packet_error` | 1 | SFD / Symbol / CRC Error |

---

# 25. TX / RX 공통 Parameter

```text
FS_HZ             = 160_000
FFT_N             = 128

F0_HZ             = 10_000
F1_HZ             = 20_000
FSYNC_HZ          = 25_000

F0_BIN            = 8
F1_BIN            = 16
FSYNC_BIN         = 20

FFT_BLOCK_SAMPLES = 128
SYMBOL_SAMPLES    = 256

PREAMBLE_SYMBOLS  = 4

SFD               = 8'hD5

CRC_POLY          = 8'h07
CRC_INIT          = 8'h00
CRC_XOROUT        = 8'h00

BIT_ORDER         = MSB_FIRST
```

공통 Parameter File 권장:

```text
rtl/common/bfsk_params.vh
```

---

# 26. Packet 전송시간

Preamble:

```text
4 × 1.6 ms = 6.4 ms
```

Digital Frame:

```text
SFD      = 8 bit
Frame ID = 8 bit
DATA     = 8 bit
CRC      = 8 bit
Total    = 32 bit
```

Data:

```text
32 × 1.6 ms = 51.2 ms
```

전체:

```text
57.6 ms / Character
```

이론상 최대 약 17 Character/s.

---

# 27. Hardware 구성

TX:

```text
FPGA
↓
2N7000
↓
LED / Laser
```

RX:

```text
BPW34
↓
MCP6022 A : TIA
↓
MCP6022 B : Gain / Filter
↓
Zybo XADC
```

| 부품 | 역할 |
|---|---|
| BPW34 / BPW34S | Photodiode |
| MCP6022 | TIA + Gain / Filter |
| 2N7000 | Optical Source Switching |
| Zybo Z7-20 XADC | 12-bit ADC |
| LM393N | Debug 전용, Main FFT 경로 미사용 |

---

# 28. 구현 진행상황

## Phase 0 — Architecture / Specification

- [x] BFSK 방식 확정
- [x] OFDM 미사용
- [x] IFFT 미사용
- [x] FFT 기반 Frequency Detection 확정
- [x] XADC 사용 확정
- [x] BPW34 사용 확정
- [x] MCP6022 사용 확정
- [x] 2N7000 사용 확정
- [x] `f0 = 10 kHz`
- [x] `f1 = 20 kHz`
- [x] `f_sync = 25 kHz`
- [x] `Fs = 160 kS/s`
- [x] FFT = **128-point**
- [x] FFT Block = **128 Sample / 0.8 ms**
- [x] Symbol = **256 Sample / 1.6 ms**
- [x] Symbol당 FFT Result = **2개**
- [x] `F0_BIN = 8`
- [x] `F1_BIN = 16`
- [x] `FSYNC_BIN = 20`
- [x] Preamble = **SYNC Symbol 4개 / 6.4 ms**
- [x] SFD = `8'hD5`
- [x] CRC-8 규격 확정
- [x] Bit Order = MSB First
- [x] RX TOP 범위 재정의
- [x] FFT → RX TOP = 128-bin Magnitude Stream
- [x] 2 FFT Block 합산 후 Symbol 판정
- [x] INVALID 정책 확정
- [x] Output Buffer / UART 구조 반영

---

# 29. v1.0 → v1.1 주요 변경

```text
FFT Size
256 → 128 point

FFT Block
256 Sample / 1.6 ms
→ 128 Sample / 0.8 ms

Symbol
512 Sample / 3.2 ms
→ 256 Sample / 1.6 ms

BFSK Bins
10 kHz: 16 → 8
20 kHz: 32 → 16
25 kHz: 40 → 20

Preamble Time
12.8 ms → 6.4 ms

Packet Time
115.2 ms → 57.6 ms
```

---

# 30. TX 개발 체크리스트

## TX-1 CRC

- [x] `crc8.v`
- [x] `tb_crc8.v`
- [x] `00 / 41 → C0` PASS
- [x] Vivado/XSim 검증
- [x] Vivado Project (`tb_xpr/tb_crc8/tb_crc8.xpr`) 반영
- Commit: `266eeca` (`refactor: organize CRC-8 TX files and add Vivado project`)

## TX-2 Frame Generator

- [x] `tx_frame_generator.v`
- [x] `tb_tx_frame_generator.v`
- [x] SFD = `8'hD5`
- [x] Frame ID 8 bit
- [x] DATA 8 bit
- [x] CRC-8 8 bit
- [x] MSB First
- [x] 32-bit Frame 전송
- [x] `frame_start` / `frame_done` Pulse 검증
- [x] `valid/ready` Backpressure 검증
- [x] Vivado 2020.2 / XSim 검증
- [x] Vivado Project (`tb_xpr/tb_tx_frame_generator/tb_tx_frame_generator.xpr`) 반영
- Test Vector #1: `Frame ID=00`, `DATA=41` → `D50041C0` PASS
- Test Vector #2: `Frame ID=12`, `DATA=34` → `D51234F1` PASS (Backpressure)
- Commit: `abdd1cf` (`feat: add verified TX frame generator and update CRC comments`)

## TX-3 BFSK Mapper

- [x] `bfsk_mapper.v`
- [x] `tb_bfsk_mapper.v`
- [x] IDLE
- [x] BIT0
- [x] BIT1
- [x] SYNC
- [x] `symbol_start` 1-Clock Pulse
- [x] `symbol_done` 전까지 Symbol 유지
- [x] `symbol_done` 후 IDLE 복귀
- [x] `sync_valid` / `sync_ready` Handshake
- [x] SYNC / DATA 동시 요청 시 SYNC 우선
- [x] Vivado 2020.2 / XSim 검증
- [x] Vivado Project (`tb_xpr/tb_bfsk_mapper/tb_bfsk_mapper.xpr`) 반영
- Commit: `eb25eed` (`feat: verify BFSK mapper and add SYNC handshake interface`)
- XPR Commit: `33bd050` (`add new xpr folder`)

## TX-4 Carrier Generator

- [ ] `bfsk_carrier_gen.v`
- [ ] 10 kHz
- [ ] 20 kHz
- [ ] 25 kHz
- [ ] Symbol = 1.6 ms
- [ ] IDLE = No Carrier

## TX-5 TX FSM

- [ ] PREAMBLE 4 Symbol
- [ ] SFD 8 bit
- [ ] Frame ID 8 bit
- [ ] DATA 8 bit
- [ ] CRC 8 bit
- [ ] `frame_done`

## TX-6 Integration

- [ ] `optical_tx_top.v`
- [ ] CNN Interface
- [ ] Frame Generator
- [ ] Mapper
- [ ] Carrier Generator
- [ ] 2N7000 / LED
- [ ] Scope 주파수 확인

---

# 31. RX FFT 파트 체크리스트

- [ ] XADC 160 kS/s
- [ ] `xadc_sample_valid` 기반 Count
- [ ] DC Removal
- [ ] 128 x 2 Ping-Pong Buffer
- [ ] Buffer Overflow
- [ ] FFT `valid/ready/last`
- [ ] 128-point FFT
- [ ] Bin 0~127
- [ ] Re² + Im²
- [ ] 128-bin Magnitude Stream
- [ ] `fft_mag_last`
- [ ] `fft_frame_error`

---

# 32. RX TOP 체크리스트

## Bin Capture

- [ ] `rx_bin_capture.v`
- [ ] Bin 8
- [ ] Bin 16
- [ ] Bin 20
- [ ] Block Completeness Check

## Symbol Detector

- [ ] `rx_symbol_detector.v`
- [ ] FFT Block 2개 Pair
- [ ] Magnitude 합산
- [ ] BIT0 / BIT1 / SYNC / INVALID

## Symbol Sync

- [ ] `rx_symbol_sync.v`
- [ ] SYNC 4회
- [ ] `frame_start`
- [ ] `symbol_locked`

## Frame Decoder

- [ ] `rx_frame_decoder.v`
- [ ] SFD
- [ ] Frame ID
- [ ] DATA
- [ ] Received CRC
- [ ] Frame 중 SYNC/INVALID Abort

## CRC

- [ ] `rx_crc8_check.v`
- [ ] POLY 0x07
- [ ] INIT 0
- [ ] MSB First
- [ ] Frame ID + DATA

## Output

- [ ] `rx_output_buffer.v`
- [ ] `rx_char_valid`
- [ ] `rx_char_ready`
- [ ] UART 115200 8N1
- [ ] 필요 시 FIFO

---

# 33. 초기 Test Vector

CRC:

```text
Frame ID = 8'h00
DATA     = 8'h41
CRC      = 8'hC0
```

Full Frame:

```text
SYNC ×4
D5
00
41
C0
```

Expected:

```text
rx_frame_id = 00
rx_char_id  = 41
crc_ok      = 1
```

Error Injection:

```text
CRC Error → packet_error=1, rx_char_valid=0
INVALID Symbol → Packet Abort → SEARCH_SYNC
```

---

# 34. Integration PASS 기준

TX:

- [ ] 25 kHz SYNC
- [ ] 10 kHz BIT0
- [ ] 20 kHz BIT1
- [ ] Symbol 1.6 ms
- [ ] Preamble 4 Symbol
- [ ] Frame 순서 정상

RX FFT:

- [ ] 128 Sample = 0.8 ms
- [ ] FFT 정상
- [ ] Bin 8 / 16 / 20 Magnitude 확인
- [ ] 128-bin Stream 정상
- [ ] 2 FFT Block Pairing 정상

RX TOP:

- [ ] Bin8 우세 → BIT0
- [ ] Bin16 우세 → BIT1
- [ ] Bin20 우세 → SYNC
- [ ] SYNC×4 → frame_start
- [ ] SFD D5 PASS
- [ ] Frame ID 복원
- [ ] DATA 복원
- [ ] CRC PASS
- [ ] UART 정상 출력

최종:

```text
TX char_id == RX char_id
crc_ok = 1
```

---

# 35. Hardware Test 순서

```text
1. TX 10 / 20 / 25 kHz Scope 확인
2. Symbol Time 1.6 ms 확인
3. BPW34 수광 확인
4. MCP6022 TIA 출력 확인
5. XADC Sample 확인
6. DC Offset 확인
7. 128 Sample Buffer 확인
8. 128-point FFT 확인
9. Bin 8 / 16 / 20 확인
10. FFT Result 2개 합산
11. BFSK Symbol Decode
12. SYNC 4 Symbol Detect
13. SFD D5 확인
14. Frame ID / DATA / CRC 복원
15. UART Output
16. CNN / HDMI Integration
```

---

# 36. TBD / 실험 후 변경 가능 항목

- [ ] 최종 `f0`
- [ ] 최종 `f1`
- [ ] 최종 `f_sync`
- [ ] XADC Sampling Rate
- [ ] `SYNC_THRESHOLD`
- [ ] Data Threshold
- [ ] Winner / Runner-up Margin
- [ ] Preamble Length
- [ ] Symbol Pairing 보정 방식
- [ ] Overlap FFT 적용 여부
- [ ] 최종 LED / Laser
- [ ] MCP6022 TIA `Rf`
- [ ] MCP6022 TIA `Cf`
- [ ] MCP6022 Gain
- [ ] FFT Fixed-point Width
- [ ] Frame ID Sequence 정책
- [ ] UART FIFO Depth

---

# 37. 현재 상태 요약

```text
Project Architecture    : 확정
Modulation              : BFSK
OFDM                    : 사용 안 함
IFFT                    : 사용 안 함

ADC                     : Zybo XADC
Sampling Rate           : 160 kS/s

FFT                     : 128-point
FFT Block               : 128 Samples / 0.8 ms

Symbol                  : 256 Samples / 1.6 ms
FFT Results per Symbol  : 2

Bit 0                   : 10 kHz / Bin 8
Bit 1                   : 20 kHz / Bin 16
Preamble                : 25 kHz / Bin 20
Preamble Length         : 4 Symbol / 6.4 ms

SFD                     : 0xD5
Frame ID                : 8 bit
DATA                    : 8 bit
CRC                     : CRC-8 / 0x07
Bit Order               : MSB First
Idle                    : No Carrier

RX FFT Interface        : 128-bin Magnitude Stream
RX Symbol Decision      : 2 FFT Block Magnitude Sum
RX Invalid Policy       : Packet Abort
RX Output               : Buffer + UART

TX-1 CRC-8              : PASS
CRC RTL                  : rtl/tx/crc8.v
CRC Testbench            : sim/tx/tb_crc8.v
CRC Test Vector          : 00 / 41 -> C0 PASS
CRC Verification         : Vivado/XSim
CRC Commit               : 266eeca

TX-2 Frame Generator      : PASS
Frame Generator RTL       : rtl/tx/tx_frame_generator.v
Frame Generator TB        : sim/tx/tb_tx_frame_generator.v
Frame Format              : SFD + Frame ID + DATA + CRC-8 (32 bit)
Test Vector #1            : D50041C0 PASS
Test Vector #2            : D51234F1 PASS (Backpressure)
Handshake                 : valid/ready Backpressure PASS
Frame Pulse               : frame_start / frame_done PASS
Verification              : Vivado 2020.2 / XSim
Frame Generator Commit    : abdd1cf

TX-3 BFSK Mapper           : PASS
BFSK Mapper RTL            : rtl/tx/bfsk_mapper.v
BFSK Mapper TB             : sim/tx/tb_bfsk_mapper.v
Mapping                    : IDLE / BIT0 / BIT1 / SYNC PASS
SYNC Handshake             : sync_valid / sync_ready PASS
SYNC Priority              : SYNC > DATA PASS
Symbol Control             : symbol_start / symbol_done PASS
Verification               : Vivado 2020.2 / XSim
BFSK Mapper Commit         : eb25eed
BFSK Mapper XPR            : tb_xpr/tb_bfsk_mapper/tb_bfsk_mapper.xpr
```

---

# 38. 지금 바로 진행할 작업

TX:

```text
1. bfsk_carrier_gen.v
2. tb_bfsk_carrier_gen.v
3. 10 / 20 / 25 kHz Carrier 검증
4. Symbol 1.6 ms 적용
5. IDLE = No Carrier 검증
6. tx_fsm.v
7. PREAMBLE SYNC x4 연동
8. optical_tx_top.v
```

RX FFT:

```text
1. XADC sample_valid 확인
2. DC Removal
3. 128 x 2 Ping-Pong Buffer
4. 128-point FFT
5. Re² + Im²
6. 128-bin Stream Interface
```

RX TOP:

```text
1. rx_bin_capture.v
2. rx_symbol_detector.v
3. rx_symbol_sync.v
4. rx_frame_decoder.v
5. rx_crc8_check.v
6. rx_output_buffer.v
7. uart_tx.v
```

---

# 39. 진행상황 업데이트 규칙

```markdown
## YYYY-MM-DD Progress

### 완료
- [x] module_name
  - File:
  - Testbench:
  - Tool:
  - Result: PASS
  - Commit:

### 변경된 설계 결정
- 변경 전:
- 변경 후:
- 이유:

### 실패 / 이슈
- 문제:
- 원인:
- 해결:

### 다음 작업
1.
2.
3.
```

PASS되지 않은 항목은 완료 처리하지 않는다.

---

# 40. 공통 개발 규칙

- RTL은 **Verilog** 기준
- 기존 PASS 코드 임의 삭제 금지
- 수정 시 변경 이유 기록
- PASS Testbench는 Regression 유지
- TX/RX Parameter 변경 시 양쪽 동시 갱신
- `f0`, `f1`, `f_sync`, `Fs`, `FFT_N` 불일치 금지
- Symbol Time 불일치 금지
- Bit Order 불일치 금지
- CRC 규칙 불일치 금지
- Frame FSM이 XADC / Buffer / FFT를 Reset하지 않도록 유지
- Sample 연속성이 깨지는 Overflow 발생 시 Packet / Pairing 모두 폐기
- Hardware 값 변경 시 `progress.md` 먼저 수정

---

# 41. 2026-09-14 Progress

## 완료

- [x] TX-1 CRC-8 RTL 구현 및 검증 완료
  - File: `rtl/tx/crc8.v`
  - Testbench: `sim/tx/tb_crc8.v`
  - Vivado Project: `tb_xpr/tb_crc8/tb_crc8.xpr`
  - Tool: Vivado 2020.2 / XSim
  - Test Vector: `Frame ID=8'h00`, `DATA=8'h41`
  - Expected CRC: `8'hC0`
  - Result: **PASS**
  - Commit: `266eeca` (`refactor: organize CRC-8 TX files and add Vivado project`)

- [x] GitHub / Notion 진행상황 관리 연결 완료
  - Repository: `Critical-mankind/BFSK_Tx`
  - Branch: `main`
  - Notion Testbench 결과 페이지에 CRC 검증 이미지 기록

## 변경된 설계 결정

- 변경 없음. CRC 규격은 기존 확정값 유지.
  - POLY: `8'h07`
  - INIT: `8'h00`
  - XOROUT: `8'h00`
  - REFIN/REFOUT: `false`
  - Bit Order: MSB First

## 실패 / 이슈

- 현재 기록된 TX-1 CRC 관련 미해결 이슈 없음.

## 다음 작업

1. `tx_frame_generator.v` 구현
2. `tb_tx_frame_generator.v` 작성 및 SFD/Frame ID/DATA/CRC/MSB First 검증
3. Frame Generator PASS 후 `bfsk_mapper.v` 진행

---

# 42. 2026-09-15 Progress

## 완료

- [x] TX-2 Frame Generator RTL 구현 및 검증 완료
  - File: `rtl/tx/tx_frame_generator.v`
  - Testbench: `sim/tx/tb_tx_frame_generator.v`
  - Vivado Project: `tb_xpr/tb_tx_frame_generator/tb_tx_frame_generator.xpr`
  - Tool: Vivado 2020.2 / XSim
  - Frame Format: `SFD(8) + Frame ID(8) + DATA(8) + CRC-8(8)`
  - Bit Order: MSB First
  - Test #1: `Frame ID=8'h00`, `DATA=8'h41` → `32'hD50041C0` PASS
  - Test #2: `Frame ID=8'h12`, `DATA=8'h34` → `32'hD51234F1` PASS
  - Backpressure: `tx_bit_valid && tx_bit_ready` Stall/Resume PASS
  - Pulse: `frame_start=1회`, `frame_done=1회` PASS
  - Result: **PASS**
  - Commit: `abdd1cf` (`feat: add verified TX frame generator and update CRC comments`)

- [x] CRC RTL 파일 위치 정리
  - 기존 문서 경로: `rtl/common/crc8.v`
  - 현재 저장소 경로: `rtl/tx/crc8.v`

## 변경된 설계 결정

- Frame Generator는 CRC를 먼저 계산한 뒤 완성된 32-bit Frame을 MSB First로 직렬 출력한다.
- `tx_bit_valid && tx_bit_ready`가 성립할 때만 비트가 진행되며, `ready=0`에서는 현재 비트를 유지한다.
- 기존 Frame Format / CRC / SFD 규격 변경 없음.

## 실패 / 이슈

- 현재 기록된 TX-2 Frame Generator 관련 미해결 이슈 없음.

## 다음 작업

1. `bfsk_mapper.v` 구현
2. `tb_bfsk_mapper.v` 작성 및 IDLE / BIT0 / BIT1 / SYNC Mapping 검증
3. Mapper PASS 후 `bfsk_carrier_gen.v` 진행

---

# 43. 2026-09-16 Progress

## 완료

- [x] TX-3 BFSK Mapper RTL 구현 및 검증 완료
  - File: `rtl/tx/bfsk_mapper.v`
  - Testbench: `sim/tx/tb_bfsk_mapper.v`
  - Vivado Project: `tb_xpr/tb_bfsk_mapper/tb_bfsk_mapper.xpr`
  - Tool: Vivado 2020.2 / XSim
  - Mapping: `IDLE / BIT0 / BIT1 / SYNC`
  - `tx_bit=0 -> BIT0`: PASS
  - `tx_bit=1 -> BIT1`: PASS
  - `sync_valid -> SYNC`: PASS
  - `symbol_start` 1-Clock Pulse: PASS
  - `symbol_done` 전까지 `symbol_type/symbol_valid` 유지: PASS
  - `symbol_done` 후 IDLE 복귀: PASS
  - SYNC / DATA 동시 요청 시 SYNC 우선: PASS
  - Result: **PASS**
  - Commit: `eb25eed` (`feat: verify BFSK mapper and add SYNC handshake interface`)
  - XPR Commit: `33bd050` (`add new xpr folder`)

- [x] TX 인터페이스 명세 파일 저장소 반영 확인
  - File: `docs/BFSK_TX_Interface_Spec_Verilog.xlsx`
  - Commit: `9780ac5` (`chore: clean unused RX directories and add TX interface spec`)

## 변경된 설계 결정

- TX FSM -> BFSK Mapper에 SYNC 요청 Handshake 추가
  - `sync_valid`: TX FSM -> Mapper SYNC 요청
  - `sync_ready`: Mapper -> TX FSM 요청 수락 가능
- Mapper가 IDLE일 때만 `sync_ready=1`.
- `tx_bit_ready = (~active) && (~sync_valid)`로 정의하여 SYNC 요청이 있으면 DATA 수락을 차단한다.
- SYNC와 DATA가 동시에 요청되면 SYNC를 우선 처리한다.
- Mapper가 Symbol을 수락하면 `symbol_start`를 1-Clock Pulse로 발생시키고, `symbol_done`까지 `symbol_type/symbol_valid`를 유지한다.

## 실패 / 이슈

- 현재 기록된 TX-3 BFSK Mapper 관련 미해결 이슈 없음.

## 다음 작업

1. `bfsk_carrier_gen.v` 구현
2. `tb_bfsk_carrier_gen.v` 작성
3. 10 / 20 / 25 kHz Carrier + IDLE No Carrier 검증
4. Symbol 1.6 ms 검증 후 `tx_fsm.v` 진행

---

**Last Updated:** 2026-09-16  
**Document Version:** v1.4  
**Communication:** BFSK + 128-point FFT Frequency Detection  
**Physical Link:** Wireless Optical Only
