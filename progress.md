# FPGA BFSK TX Project — Progress

> **Single Source of Truth**
>
> 이 문서는 `Critical-mankind/BFSK_Tx` 저장소의 **TX 구현 상태, 인터페이스, 공통 통신 규격, PASS/FAIL 이력**을 관리한다.
> 현재 저장소의 실제 파일과 검증 결과를 우선 기준으로 한다.
>
> **Last Updated:** 2026-09-17  
> **Document Version:** v1.7

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

현재 Git 원격에서 확인된 TX-5 구현 / 검증 파일:

```text
rtl/tx/tx_fsm.v
sim/tx/tb_tx_fsm.v
tb_xpr/tb_TX_FSM/tb_TX_FSM.xpr
```

TX-5 구현 Commit:

```text
92ece97
```

현재 Git 원격에서 확인된 TX-6 구현 / 검증 파일:

```text
rtl/tx/optical_tx_top.v
sim/tx/tb_optical_tx_top.v
tb_xpr/optical_Tx_top/optical_Tx_top.xpr
```

TX-6 구현 Commit:

```text
cd0f5b5
```

> `rx_symbol_sync`, `rx_frame_decoder` 등 RX 구현 모듈은 현재 `BFSK_Tx` 저장소의 구현 파일이 아니므로
> 본 문서에서 구현 완료 항목으로 관리하지 않는다.
> FFT/RX 관련 값은 TX와 맞춰야 하는 **공통 통신 규격 기준값**으로만 기록한다.

---

# 1. 현재 TX 구조

```text
char_id / char_valid
        ↓
optical_tx_top
        │
        ├─ tx_fsm
        │    ├─ PREAMBLE SYNC ×4 제어
        │    ├─ frame_gen_start
        │    └─ 최종 frame_done 대기
        │
        ├─ tx_frame_generator
        │    ├─ crc8
        │    └─ frame_gen_done
        │         = 마지막 Frame Bit의 Mapper 전달 완료
        │
        ├─ bfsk_mapper
        │
        └─ bfsk_carrier_gen
              └─ symbol_done
                   = 현재 Optical Symbol 출력 완료

optical_tx_top:
frame_gen_done
    ↓
last_symbol_pending = 1
    ↓
마지막 symbol_done
    ↓
final frame_done
    ↓
tx_fsm 송신 완료
        ↓
optical_tx / tx_enable
```

외부 TX 인터페이스:

```text
Input:
char_id[7:0]
char_valid

Output:
char_ready
tx_busy
optical_tx
tx_enable
```

완료 시점 정의:

```text
frame_gen_done
= 마지막 비트가 Mapper에 전달된 시점

frame_done
= 마지막 Optical Symbol의 Carrier 출력까지 끝난 시점
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

## 4.1 CNN / Upstream → TX FSM

| Signal | Dir 기준 | Width | 설명 |
|---|:---:|---:|---|
| `char_id` | Upstream → TX FSM | 8 | 전송할 Character ID |
| `char_valid` | Upstream → TX FSM | 1 | Character ID 유효 |
| `char_ready` | TX FSM → Upstream | 1 | 새 Character 수락 가능 |
| `tx_busy` | TX FSM → Upstream | 1 | TX 전체 송신 진행 중 |

Handshake:

```text
char_valid && char_ready
```

TX FSM은 Handshake 시 `char_id`를 `latched_char_id`에 저장한다.

---

## 4.2 TX FSM → Frame Generator

| Signal | Dir 기준 | Width | 설명 |
|---|:---:|---:|---|
| `latched_char_id` | TX FSM → Frame Gen | 8 | 현재 전송할 Character DATA |
| `frame_id` | TX FSM → Frame Gen | 8 | 현재 Frame ID |
| `frame_gen_start` | TX FSM → Frame Gen | 1 | Frame Generator Start 1-Clock Pulse |
| `frame_gen_done` | Frame Gen → Top | 1 | 마지막 Frame Bit의 Mapper 전달 완료 Pulse |
| `frame_done` | Top → TX FSM | 1 | 마지막 Optical Symbol 출력 완료 Pulse |

Frame ID 정책:

```text
Frame 완료 시 +1
8'hFF 다음 8'h00 Roll-over
```

TX-6 완료 시점 보정:

```text
tx_frame_generator.frame_done
→ Top 내부 wire: frame_gen_done

frame_gen_done 발생
→ last_symbol_pending = 1

최종 frame_done
= last_symbol_pending && symbol_done
```

`frame_gen_done`은 마지막 Bit의 Mapper 전달 완료이고,
TX FSM으로 전달되는 `frame_done`은 마지막 Optical Symbol 출력 완료를 의미한다.

---

## 4.3 Frame Generator → BFSK Mapper

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

## 4.4 TX FSM → BFSK Mapper

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

## 4.5 BFSK Mapper → Carrier Generator

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

## 4.6 Carrier Generator → TX FSM

| Signal | Dir 기준 | Width | 설명 |
|---|:---:|---:|---|
| `symbol_done` | Carrier → TX FSM | 1 | 현재 SYNC Symbol 출력 완료 Pulse |

TX FSM은 Preamble 구간에서 `sync_valid && sync_ready`로 SYNC 요청이 수락된 뒤
`symbol_done`을 기다린 다음 다음 SYNC를 요청한다.

SYNC Count는 실제 Symbol 완료 기준으로 진행한다.

---

## 4.7 Carrier Generator → Optical Driver

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

## TX-5 TX FSM — PASS

RTL:

```text
rtl/tx/tx_fsm.v
```

TB:

```text
sim/tx/tb_tx_fsm.v
```

Vivado Project:

```text
tb_xpr/tb_TX_FSM/tb_TX_FSM.xpr
```

구현 Commit:

```text
92ece97
feat: add and verify TX FSM
```

Vivado / XSim:

```text
TX FSM TEST RESULT : PASS
PASS = 15
FAIL = 0
```

검증 항목:

```text
Reset 후 IDLE / char_ready / tx_busy PASS
char_valid && char_ready에서 Character ID Latch PASS
tx_busy 동안 새로운 Character 입력 차단 PASS
sync_valid / sync_ready Handshake PASS
SYNC Symbol 정확히 4개 PASS
Mapper Ready Stall 중 sync_valid 유지 PASS
4번째 SYNC 완료 후 frame_gen_start 1-Clock Pulse PASS
Frame 완료 후 IDLE 복귀 PASS
Frame ID 증가 PASS
Frame ID 8'hFF → 8'h00 Roll-over PASS
```

TX FSM 송신 순서:

```text
ST_IDLE
→ ST_LOAD_DATA
→ ST_PREAMBLE
→ ST_SEND_FRAME
→ ST_FRAME_DONE
→ ST_IDLE
```

Preamble 제어:

```text
sync_valid
→ sync_valid && sync_ready
→ SYNC 요청 수락
→ symbol_done 대기
→ 완료 Count
→ 총 4회 완료 후 frame_gen_start
```

---

## TX-6 Optical TX Top Integration — PASS

RTL:

```text
rtl/tx/optical_tx_top.v
```

TB:

```text
sim/tx/tb_optical_tx_top.v
```

Vivado Project:

```text
tb_xpr/optical_Tx_top/optical_Tx_top.xpr
```

구현 Commit:

```text
cd0f5b5
feat: integrate optical TX top and verify full BFSK frame
```

통합 대상:

```text
tx_fsm
tx_frame_generator
crc8
bfsk_mapper
bfsk_carrier_gen
```

Vivado / XSim:

```text
OPTICAL TX TOP TEST RESULT : PASS
PASS = 18
FAIL = 0
```

주요 검증:

```text
Preamble SYNC x4 PASS
Data Symbol 32개 PASS
D50041C0 복원 PASS

frame_gen_start 1회 PASS
frame_done 1회 PASS

optical carrier rising edge = 816 PASS

frame_id 00 → 01 PASS

최종:
char_ready  = 1 PASS
tx_busy     = 0 PASS
optical_tx  = 0 PASS
tx_enable   = 0 PASS
```

Optical Rising Edge 기준:

```text
Preamble:
4 × 25 kHz × 1.6 ms = 4 × 40 = 160

D50041C0:
BIT1 = 9개  → 9 × 32 = 288
BIT0 = 23개 → 23 × 16 = 368

Total = 160 + 288 + 368
      = 816
```

### TX-6 완료 시점 보정

문제:

```text
tx_frame_generator.frame_done은
마지막 Bit가 Mapper에 전달된 시점에 발생한다.

이 시점에는 마지막 BFSK Optical Symbol의
1.6 ms Carrier 출력이 아직 완료되지 않았을 수 있다.
```

변경:

```text
frame_gen_done
= tx_frame_generator.frame_done

frame_gen_done 발생 시
last_symbol_pending <= 1

최종 frame_done
= last_symbol_pending && symbol_done
```

의미:

```text
frame_gen_done = Digital Frame Bit 전달 완료
frame_done     = 실제 마지막 Optical Symbol 출력 완료
```

결과:

```text
TX FSM은 마지막 Optical Symbol이 끝난 이후에만
Frame 완료 처리 및 IDLE 복귀를 수행한다.

통합 TB PASS = 18 / FAIL = 0
```

참고 수정:

```text
tx_frame_generator ST_LOAD:
frame_shift = ...
→
frame_shift <= ...
```

Clocked Always Block의 `frame_shift` 갱신을 Nonblocking Assignment로 통일하였다.

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
| 2026-09-17 | TX FSM | 미구현 | `IDLE → LOAD_DATA → PREAMBLE → SEND_FRAME → FRAME_DONE` | PASS=15 / FAIL=0 |
| 2026-09-17 | Preamble Control | Mapper 직접 요청 계획 | TX FSM이 `sync_valid/sync_ready` + `symbol_done`으로 SYNC ×4 제어 | 실제 Symbol 완료 기준으로 다음 SYNC 진행 |
| 2026-09-17 | Frame ID | 정책만 정의 | Frame 완료 시 +1 / `FF → 00` Roll-over | TX-5 TB PASS |
| 2026-09-17 | TX-6 Integration | 개별 TX Block PASS | `optical_tx_top`으로 TX-1~TX-5 통합 | PASS=18 / FAIL=0 / Commit `cd0f5b5` |
| 2026-09-17 | Frame 완료 의미 | Generator `frame_done`을 전체 Frame 완료로 사용 | `frame_gen_done`과 최종 `frame_done` 분리 | 마지막 Optical Symbol 완료까지 TX FSM이 대기 |
| 2026-09-17 | Optical Frame Done | 마지막 Bit Mapper 전달 시점 | `last_symbol_pending && symbol_done` | 실제 마지막 Carrier 출력 완료를 Frame 완료 기준으로 사용 |
| 2026-09-17 | Frame Shift Assignment | `frame_shift = {...}` | `frame_shift <= {...}` | Clocked Always Block의 Nonblocking Assignment 통일 |

---

# 8. 다음 작업

## TX-7 Hardware Verification

TX RTL 통합 Simulation은 TX-6까지 완료하였다.

다음 단계:

```text
1. Hardware Test용 입력 방법 결정
   - VIO 또는 고정 Test Character Generator

2. XDC 작성
   - clk
   - rst
   - optical_tx
   - 필요 시 tx_enable / debug signal

3. Zybo PMOD에 optical_tx 연결

4. Oscilloscope Hardware 검증
   - SYNC 25 kHz
   - BIT0 10 kHz
   - BIT1 20 kHz
   - Symbol Time 1.6 ms
   - Preamble SYNC ×4
   - Frame 종료 후 IDLE

5. 2N7000 Driver 연결

6. LED / Laser Optical Source 검증
```

Hardware 검증 이후:

```text
Optical Link
→ BPW34
→ MCP6022
→ XADC / FFT RX 연동
```

Interface Excel은 현재 변경사항을 `progress.md`에 먼저 기록하고,
문서 정리 단계에서 최신 RTL 기준으로 일괄 동기화한다.

---

# 9. Git / 문서 동기화 상태

TX-6 구현 파일은 Git 원격 반영 완료:

```text
rtl/tx/optical_tx_top.v
rtl/tx/tx_frame_generator.v
sim/tx/tb_optical_tx_top.v
tb_xpr/optical_Tx_top/optical_Tx_top.xpr
```

구현 Commit:

```text
cd0f5b5
```

검증 결과:

```text
PASS = 18
FAIL = 0
```

진행상황 운영 지침:

```text
docs/progress_지침.md
Commit = 08bb3f1
```

이번 진행상황 동기화 대상:

```text
progress.md
Notion Progress
Notion Testbench 결과
```

Interface Excel은 이번 단계에서 제외하고,
문서 정리 채팅에서 최신 RTL 기준으로 일괄 업데이트한다.

문서 Commit은 구현 Commit과 분리하여 관리한다.

---

# 10. 업데이트 규칙

- RTL은 Verilog `.v` 기준으로 관리한다.
- 설명 주석은 한글을 기본으로 한다.
- PASS되지 않은 항목은 완료 처리하지 않는다.
- 기존 PASS RTL/TB는 Regression 용도로 유지한다.
- 실제 저장소에 없는 모듈을 구현 완료 항목으로 기록하지 않는다.
- 인터페이스 변경은 `progress.md`에 즉시 기록한다. Interface Excel은 문서 정리 단계에서 최신 RTL 기준으로 일괄 동기화한다.
- `FS_HZ`, `FFT_N`, `SYMBOL_SAMPLES`, `F0/F1/FSYNC` 변경 시 공통 규격을 함께 갱신한다.
- Hardware 검증 전 Simulation PASS를 먼저 확보한다.
