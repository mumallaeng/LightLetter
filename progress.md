# FPGA BFSK TX Project — Progress

> **Single Source of Truth**
>
> 이 문서는 `Critical-mankind/BFSK_Tx` 저장소의 **TX 구현 상태, 인터페이스, 공통 통신 규격, PASS/FAIL 이력**을 관리한다.
> 현재 저장소의 실제 파일과 검증 결과를 우선 기준으로 한다.
>
> **Last Updated:** 2026-09-17  
> **Document Version:** v1.5

---

# 0. 문서 범위

현재 `BFSK_Tx` 저장소는 **TX 전용 저장소**로 관리한다.

현재 Git 원격에서 확인된 구현 RTL:

```text
rtl/common/bfsk_params.vh
rtl/tx/crc8.v
rtl/tx/tx_frame_generator.v
rtl/tx/bfsk_mapper.v
```

현재 Git 원격에서 확인된 Testbench:

```text
sim/tx/tb_crc8.v
sim/tx/tb_tx_frame_generator.v
sim/tx/tb_bfsk_mapper.v
```

현재 Git 원격에서 확인된 TX-4 구현 / 검증 파일:

```text
rtl/tx/bfsk_carrier_gen.v
sim/tx/tb_bfsk_carrier_gen.v
tb_xpr/tb_bfsk_carrier_gen/tb_bfsk_carrier_gen.xpr
```

TX-4 구현 Commit:

```text
5886ce1
```

> `rx_symbol_sync`, `rx_frame_decoder` 등 RX 구현 모듈은 현재 `BFSK_Tx` 저장소의 구현 파일이 아니므로
> 본 문서에서 구현 완료 항목으로 관리하지 않는다.
> FFT/RX 관련 값은 TX와 맞춰야 하는 **공통 통신 규격 기준값**으로만 기록한다.

---

# 1. 현재 TX 구조

```text
Character ID
    ↓
tx_frame_generator
    ├─ crc8
    ↓
tx_bit / tx_bit_valid / tx_bit_ready
    ↓
bfsk_mapper
    ├─ DATA: tx_bit
    └─ PREAMBLE SYNC: sync_valid / sync_ready
    ↓
symbol_type / symbol_valid / symbol_start
    ↓
bfsk_carrier_gen
    ↓
optical_tx / tx_enable
    ↓
2N7000 / LED / Laser
```

추후 TX FSM이 추가되면:

```text
TX FSM
 ├─ PREAMBLE SYNC ×4 제어
 ├─ sync_valid / sync_ready
 └─ Frame Generator Start 제어
```

---

# 2. 현재 확정 공통 규격

| 항목 | 현재 값 |
|---|---:|
| TX Clock | 100 MHz |
| Sampling Rate 기준 `FS_HZ` | 160 kSample/s |
| FFT Size 기준 | 128 point |
| FFT Block | 128 Sample / 0.8 ms |
| Symbol | 256 Sample / 1.6 ms |
| Preamble | SYNC ×4 / 6.4 ms |
| BIT0 | 10 kHz |
| BIT1 | 20 kHz |
| SYNC | 25 kHz |
| BIT0 FFT Bin | 8 |
| BIT1 FFT Bin | 16 |
| SYNC FFT Bin | 20 |
| SFD | `8'hD5` |
| Bit Order | MSB First |
| CRC | CRC-8 / Poly `0x07` / Init `0x00` |

공통 Parameter 기준:

```verilog
FS_HZ             = 160000
FFT_N             = 128

F0_HZ             = 10000
F1_HZ             = 20000
FSYNC_HZ          = 25000

F0_BIN            = 8
F1_BIN            = 16
FSYNC_BIN         = 20

FFT_BLOCK_SAMPLES = 128
SYMBOL_SAMPLES    = 256
PREAMBLE_SYMBOLS  = 4
```

구형 값인 아래 항목은 **현재 규격으로 사용하지 않는다.**

```text
FFT 256 point
FFT Block 256 Sample / 1.6 ms
Symbol 512 Sample / 3.2 ms
Preamble 12.8 ms
```

---

# 3. Frame / CRC 규격

Frame Generator가 출력하는 32-bit Data Frame:

```text
SFD(8) + Frame ID(8) + DATA(8) + CRC-8(8)
```

SFD:

```text
8'hD5
```

CRC:

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

기준 Test Vector:

```text
Frame ID = 8'h00
DATA     = 8'h41
CRC      = 8'hC0
Frame    = 32'hD50041C0
```

---

# 4. TX Interface

## 4.1 Frame Generator → BFSK Mapper

| Signal | Dir 기준 | Width | 설명 |
|---|:---:|---:|---|
| `tx_bit` | Frame Gen → Mapper | 1 | 송신 Bit |
| `tx_bit_valid` | Frame Gen → Mapper | 1 | Bit 유효 |
| `tx_bit_ready` | Mapper → Frame Gen | 1 | Mapper DATA 수락 가능 |

Handshake:

```text
tx_bit_valid && tx_bit_ready
```

---

## 4.2 TX FSM → BFSK Mapper

TX-3에서 SYNC 전용 Handshake를 추가하였다.

| Signal | Dir 기준 | Width | 설명 |
|---|:---:|---:|---|
| `sync_valid` | TX FSM → Mapper | 1 | Preamble용 SYNC Symbol 요청 |
| `sync_ready` | Mapper → TX FSM | 1 | Mapper가 SYNC 요청을 받을 수 있음 |

Handshake:

```text
sync_valid && sync_ready
```

Priority:

```text
SYNC > DATA
```

SYNC와 DATA 요청이 동시에 들어오면 SYNC 요청을 먼저 수락하며 DATA ready를 차단한다.

---

## 4.3 BFSK Mapper → Carrier Generator

| Signal | Dir 기준 | Width | 설명 |
|---|:---:|---:|---|
| `symbol_type` | Mapper → Carrier | 2 | IDLE / BIT0 / BIT1 / SYNC |
| `symbol_valid` | Mapper → Carrier | 1 | Symbol 유효 |
| `symbol_start` | Mapper → Carrier | 1 | Symbol 시작 1-Clock Pulse |
| `symbol_done` | Carrier → Mapper | 1 | Symbol 출력 완료 1-Clock Pulse |

Symbol Encoding:

```text
2'b00 = IDLE
2'b01 = BIT0
2'b10 = BIT1
2'b11 = SYNC
```

---

## 4.4 Carrier Generator → Optical Driver

| Signal | Dir 기준 | Width | 설명 |
|---|:---:|---:|---|
| `optical_tx` | Carrier → Driver | 1 | BFSK Carrier |
| `tx_enable` | Carrier → Driver | 1 | Carrier 출력 Enable |

---

# 5. TX 개발 진행상황

## TX-1 CRC-8 — PASS

RTL:

```text
rtl/tx/crc8.v
```

TB:

```text
sim/tx/tb_crc8.v
```

검증:

```text
Frame ID = 00
DATA     = 41
CRC      = C0
Result   = PASS
```

Commit:

```text
266eeca
```

---

## TX-2 Frame Generator — PASS

RTL:

```text
rtl/tx/tx_frame_generator.v
```

TB:

```text
sim/tx/tb_tx_frame_generator.v
```

검증:

```text
SFD + Frame ID + DATA + CRC
MSB First
frame_start / frame_done
valid / ready Backpressure

D50041C0 PASS
D51234F1 PASS
```

Commit:

```text
abdd1cf
```

---

## TX-3 BFSK Mapper — PASS

RTL:

```text
rtl/tx/bfsk_mapper.v
```

TB:

```text
sim/tx/tb_bfsk_mapper.v
```

Vivado / XSim:

```text
TEST 1 RESET / IDLE   PASS
TEST 2 BIT0           PASS
TEST 3 BIT1           PASS
TEST 4 SYNC           PASS
TEST 5 SYNC PRIORITY  PASS

PASS = 24
FAIL = 0
```

검증 내용:

```text
IDLE = 2'b00
BIT0 = 2'b01
BIT1 = 2'b10
SYNC = 2'b11

symbol_start 1 Clock Pulse PASS
symbol_done 전 symbol_type / symbol_valid 유지 PASS
symbol_done 후 IDLE 복귀 PASS
SYNC와 DATA 동시 요청 시 SYNC 우선 PASS
```

Interface 변경:

```text
TX FSM → BFSK Mapper

sync_valid
sync_ready

Handshake:
sync_valid && sync_ready
```

변경 이유:

```text
기존 tx_bit 인터페이스는 BIT0 / BIT1만 표현할 수 있다.
Preamble용 SYNC Symbol(25 kHz)을 Mapper에 요청하기 위해
별도의 SYNC Handshake 경로가 필요하다.
```

Commit:

```text
eb25eed
```

Vivado XPR Commit:

```text
33bd050
```

---

## TX-4 BFSK Carrier Generator — PASS

RTL:

```text
rtl/tx/bfsk_carrier_gen.v
```

TB:

```text
sim/tx/tb_bfsk_carrier_gen.v
```

현재 Parameter:

```text
CLK_FREQ_HZ    = 100_000_000
FS_HZ          = 160_000
SYMBOL_SAMPLES = 256

F0_HZ          = 10_000
F1_HZ          = 20_000
FSYNC_HZ       = 25_000
```

Symbol Length:

```text
256 / 160,000
= 1.6 ms

100 MHz 기준
= 160,000 Clock
```

### RESET / IDLE

```text
optical_tx = 0 PASS
tx_enable  = 0 PASS
symbol_done = 0 PASS
```

### BIT0 / 10 kHz

```text
Symbol Length       = 160000 Clock PASS
Carrier Rising Edge = 16 PASS
symbol_done Pulse   = 1 PASS
종료 후 optical_tx  = 0 PASS
종료 후 tx_enable   = 0 PASS
```

### BIT1 / 20 kHz

```text
Symbol Length       = 160000 Clock PASS
Carrier Rising Edge = 32 PASS
symbol_done Pulse   = 1 PASS
종료 후 optical_tx  = 0 PASS
종료 후 tx_enable   = 0 PASS
```

### SYNC / 25 kHz

```text
Symbol Length       = 160000 Clock PASS
Carrier Rising Edge = 40 PASS
symbol_done Pulse   = 1 PASS
종료 후 optical_tx  = 0 PASS
종료 후 tx_enable   = 0 PASS
```

### IDLE / No Carrier

```text
Symbol Length       = 160000 Clock PASS
Carrier Rising Edge = 0 PASS
symbol_done Pulse   = 1 PASS
optical_tx           = 0 PASS
tx_enable            = 0 PASS
```

Final:

```text
BFSK CARRIER GENERATOR TEST RESULT : PASS
PASS = 27
FAIL = 0
```

Git 반영:

```text
Commit = 5886ce1
tb_xpr/tb_bfsk_carrier_gen/tb_bfsk_carrier_gen.xpr 포함
```

---

# 6. TX-4 디버깅 이력

문제:

```text
symbol_count가 증가하지 않아 symbol_done이 발생하지 않음.
```

원인:

```verilog
symbol_count <= symbol_count + 1'b0;
```

수정:

```verilog
symbol_count <= symbol_count + 1'b1;
```

결과:

```text
수정 후 전체 Test PASS
PASS = 27
FAIL = 0
```

---

# 7. 설계 / 인터페이스 변경 이력

| 날짜 | 영역 | 변경 전 | 변경 후 | 이유 / 결과 |
|---|---|---|---|---|
| 2026-09-14 | FFT | 256 point | 128 point | 현재 공통 규격 확정 |
| 2026-09-14 | FFT Block | 256 Sample / 1.6 ms | 128 Sample / 0.8 ms | 128-point FFT 기준 |
| 2026-09-14 | Symbol | 512 Sample / 3.2 ms | 256 Sample / 1.6 ms | 2 × 128-sample FFT Block |
| 2026-09-14 | Preamble | 12.8 ms | 6.4 ms | SYNC ×4 × 1.6 ms |
| 2026-09-16 | TX FSM → Mapper | SYNC 별도 경로 없음 | `sync_valid / sync_ready` | Preamble SYNC 요청 전용 Handshake |
| 2026-09-16 | Mapper Priority | DATA 경로 중심 | SYNC > DATA | SYNC / DATA 동시 요청 충돌 방지 |
| 2026-09-17 | Carrier Generator | 미검증 | 10/20/25 kHz + IDLE PASS | PASS=27 / FAIL=0 |
| 2026-09-17 | Carrier Counter | `+ 1'b0` | `+ 1'b1` | `symbol_done` 미발생 버그 수정 |

---

# 8. 다음 작업

## TX-5 TX FSM

목표:

```text
IDLE
↓
PREAMBLE
  SYNC ×4
↓
FRAME START
↓
SFD + Frame ID + DATA + CRC
↓
DONE
↓
IDLE
```

구현 예정:

```text
rtl/tx/tx_fsm.v
sim/tx/tb_tx_fsm.v
```

검증 항목:

```text
SYNC Symbol 정확히 4개
sync_valid / sync_ready Handshake
Frame Generator Start 제어
Mapper Busy 상태에서 중복 요청 금지
Frame 완료 후 IDLE 복귀
```

TX-5 PASS 후:

```text
optical_tx_top.v
XDC
PMOD
Oscilloscope 10 / 20 / 25 kHz Hardware Test
```

---

# 9. Git / 문서 동기화 상태

TX-4 구현 파일은 Git 원격 반영 완료:

```text
rtl/tx/bfsk_carrier_gen.v
sim/tx/tb_bfsk_carrier_gen.v
tb_xpr/tb_bfsk_carrier_gen/tb_bfsk_carrier_gen.xpr
```

구현 Commit:

```text
5886ce1
```

현재 문서 동기화 대상:

```text
progress.md
docs/BFSK_TX_Interface_Spec_Verilog.xlsx
```

문서 Commit은 구현 Commit과 분리하여 관리한다.

---

# 10. 업데이트 규칙

- RTL은 Verilog `.v` 기준으로 관리한다.
- 설명 주석은 한글을 기본으로 한다.
- PASS되지 않은 항목은 완료 처리하지 않는다.
- 기존 PASS RTL/TB는 Regression 용도로 유지한다.
- 실제 저장소에 없는 모듈을 구현 완료 항목으로 기록하지 않는다.
- 인터페이스 변경 시 `progress.md`와 Interface Excel을 함께 갱신한다.
- `FS_HZ`, `FFT_N`, `SYMBOL_SAMPLES`, `F0/F1/FSYNC` 변경 시 공통 규격을 함께 갱신한다.
- Hardware 검증 전 Simulation PASS를 먼저 확보한다.
