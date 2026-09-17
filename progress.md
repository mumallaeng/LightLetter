# FPGA BFSK TX Project — Progress

> **Single Source of Truth**
>
> 이 문서는 `Critical-mankind/BFSK_Tx` 저장소의 **TX 구현 상태, 인터페이스, 공통 통신 규격, PASS/FAIL 이력**을 관리한다.
> 현재 저장소의 실제 파일과 검증 결과를 우선 기준으로 한다.
>
> **Last Updated:** 2026-09-17  
> **Document Version:** v2.1

---

# 0. 문서 범위

현재 `BFSK_Tx` 저장소는 **TX 전용 저장소**로 관리한다.

현재 구현 / 검증 완료 RTL:

```text
rtl/common/bfsk_params.vh
rtl/tx/crc8.v
rtl/tx/tx_frame_generator.v
rtl/tx/bfsk_mapper.v
rtl/tx/bfsk_carrier_gen.v
rtl/tx/tx_fsm.v
rtl/tx/optical_tx_top.v
rtl/tx/axi_lite_tx_wrapper.v
rtl/tx/optical_tx_axi_top.v
```

현재 검증 완료 Testbench:

```text
sim/tx/tb_crc8.v
sim/tx/tb_tx_frame_generator.v
sim/tx/tb_bfsk_mapper.v
sim/tx/tb_bfsk_carrier_gen.v
sim/tx/tb_tx_fsm.v
sim/tx/tb_optical_tx_top.v
sim/tx/tb_axi_lite_tx_wrapper.v
sim/tx/tb_optical_tx_axi_top.v
```

최근 구현 Commit:

```text
TX-4 Carrier Generator = 5886ce1
TX-5 TX FSM            = 92ece97
TX-6 Optical TX Top    = cd0f5b5
TX-7 AXI4-Lite Wrapper = 334e7f1
TX-8 AXI + Optical Top = 4e7bc82
```

> `rx_symbol_sync`, `rx_frame_decoder` 등 RX 구현 모듈은 현재 `BFSK_Tx` 저장소의 구현 파일이 아니므로 구현 완료 항목으로 관리하지 않는다.
> FFT/RX 관련 값은 TX와 맞춰야 하는 공통 통신 규격 기준값으로만 기록한다.

---

# 1. 현재 TX 구조

```text
Zynq PS / AXI4-Lite Master
  ↓
optical_tx_axi_top
  │
  ├─ axi_lite_tx_wrapper
  │    ↓
  │    char_id / char_valid / char_ready / tx_busy
  │    ↓
  └─ optical_tx_top
       │
       ├─ tx_fsm
       ├─ tx_frame_generator / crc8
       ├─ bfsk_mapper
       └─ bfsk_carrier_gen
  ↓
optical_tx / tx_enable
```

설계 원칙:

```text
- optical_tx_top의 char_id / char_valid / char_ready / tx_busy 인터페이스는 유지한다.
- AXI4-Lite Slave Wrapper는 optical_tx_top 외부에 둔다.
- TX-8에서는 axi_lite_tx_wrapper + optical_tx_top을 optical_tx_axi_top으로 통합한다.
- AXI Clock과 TX Clock은 동일 Clock Domain으로 사용한다.
- Zynq PS에서 CNN 결과를 AXI4-Lite Register Write로 전달한다.
- Wrapper가 char_valid 1-Clock Pulse를 생성한다.
```

Reset 연결:

```text
s_axi_aresetn = AXI4-Lite active-low Reset
optical_tx_top.rst = ~s_axi_aresetn
```

TX-6 완료 시점 정의:

```text
frame_gen_done
= 마지막 비트가 Mapper에 전달된 시점

frame_done
= 마지막 Optical Symbol의 Carrier 출력까지 끝난 시점

frame_done = last_symbol_pending && symbol_done
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
FSYNC_BIN          = 20
FFT_BLOCK_SAMPLES = 128
SYMBOL_SAMPLES    = 256
PREAMBLE_SYMBOLS  = 4
```

구형 값은 현재 규격으로 사용하지 않는다.

```text
FFT 256 point
FFT Block 256 Sample / 1.6 ms
Symbol 512 Sample / 3.2 ms
Preamble 12.8 ms
```

---

# 3. Frame / CRC 규격

```text
PREAMBLE(SYNC ×4)
+ SFD(8)
+ Frame ID(8)
+ DATA(8)
+ CRC-8(8)
```

Frame Generator가 출력하는 Data Frame:

```text
SFD + Frame ID + DATA + CRC
```

CRC:

```text
POLY     = 8'h07
INIT     = 8'h00
XOROUT   = 8'h00
REFIN    = false
REFOUT   = false
ORDER    = MSB First
```

CRC 계산 대상:

```text
Frame ID + DATA
```

Golden Vector:

```text
Frame ID = 8'h00
DATA     = 8'h41
CRC      = 8'hC0
Frame    = 32'hD50041C0
```

---

# 4. TX Interface

## 4.1 AXI4-Lite Slave Wrapper

RTL:

```text
rtl/tx/axi_lite_tx_wrapper.v
```

Parameter:

```text
C_S_AXI_DATA_WIDTH = 32
C_S_AXI_ADDR_WIDTH = 4
```

Write Channel:

```text
s_axi_awaddr
s_axi_awvalid
s_axi_awready

s_axi_wdata
s_axi_wstrb
s_axi_wvalid
s_axi_wready

s_axi_bresp
s_axi_bvalid
s_axi_bready
```

Read Channel:

```text
s_axi_araddr
s_axi_arvalid
s_axi_arready

s_axi_rdata
s_axi_rresp
s_axi_rvalid
s_axi_rready
```

Wrapper ↔ `optical_tx_top`:

```text
char_id[7:0]  : Wrapper → optical_tx_top
char_valid    : Wrapper → optical_tx_top
char_ready    : optical_tx_top → Wrapper
tx_busy       : optical_tx_top → Wrapper
```

AXI Write 정책:

```text
AW와 W는 독립적으로 Handshake 가능
AW First 지원
W First 지원
AW/W 모두 수신 후 Write 실행
BRESP = OKAY
```

AXI Read 정책:

```text
AR Handshake 후 Register Read
RRESP = OKAY
```

## 4.2 AXI Register Map

| Address | Register | Access | Bit | 설명 |
|---|---|:---:|---|---|
| `0x00` | `TX_DATA` | R/W | `[7:0]` | Character ID / `char_id` |
| `0x04` | `TX_CTRL` | W | `[0]` | `START` |
| `0x08` | `TX_STATUS` | R | `[0]` | `READY` = `char_ready` |
| `0x08` | `TX_STATUS` | R | `[1]` | `BUSY` = `tx_busy` |

START 정책:

```text
TX_CTRL.START = 1 && char_ready = 1
→ char_valid 1-Clock Pulse

char_ready = 0
→ START Ignore
```

## 4.3 TX-8 AXI + Optical TX Top

RTL:

```text
rtl/tx/optical_tx_axi_top.v
```

외부 인터페이스:

```text
AXI4-Lite Slave Interface
+ optical_tx
+ tx_enable
```

통합 구조:

```text
AXI4-Lite
→ axi_lite_tx_wrapper
→ char_id / char_valid / char_ready / tx_busy
→ optical_tx_top
→ optical_tx / tx_enable
```

## 4.4 Frame Generator → BFSK Mapper

```text
tx_bit
tx_bit_valid
tx_bit_ready
Handshake = tx_bit_valid && tx_bit_ready
```

## 4.5 TX FSM → BFSK Mapper

```text
sync_valid
sync_ready
Handshake = sync_valid && sync_ready
Priority = SYNC > DATA
```

## 4.6 BFSK Mapper → Carrier Generator

```text
symbol_type[1:0]
symbol_valid
symbol_start
symbol_done
```

Symbol Encoding:

```text
00 = IDLE
01 = BIT0
10 = BIT1
11 = SYNC
```

## 4.7 Carrier Generator → Optical Driver

```text
optical_tx
tx_enable
```

---

# 5. TX 개발 진행상황

| 단계 | 모듈 | 결과 | 주요 검증 | Commit |
|---|---|---|---|---|
| TX-1 | CRC-8 | PASS | `00 + 41 → C0` | `266eeca` |
| TX-2 | Frame Generator | PASS | `D50041C0`, `D51234F1`, Backpressure | `abdd1cf` |
| TX-3 | BFSK Mapper | PASS 24/0 | IDLE / BIT0 / BIT1 / SYNC / SYNC Priority | `eb25eed` |
| TX-4 | Carrier Generator | PASS 27/0 | 10 / 20 / 25 kHz, IDLE, 1.6 ms | `5886ce1` |
| TX-5 | TX FSM | PASS 15/0 | SYNC ×4, Frame Start, Frame ID Roll-over | `92ece97` |
| TX-6 | Optical TX Top | PASS 18/0 | `D50041C0`, 816 Rising Edge, 최종 Optical 완료 | `cd0f5b5` |
| TX-7 | AXI4-Lite Wrapper | PASS 17/0 | Register R/W, START, READY/BUSY, AW First, W First | `334e7f1` |
| TX-8 | AXI4-Lite + Optical TX Integration | PASS 48/0 | AXI→Wrapper→Optical TX end-to-end, 3 Frame, AW/W 순서, Status | `4e7bc82` |

## TX-6 Optical TX Top 핵심 보정

문제:

```text
tx_frame_generator.frame_done은 마지막 Bit가 Mapper에 전달된 시점이므로
마지막 Optical Symbol의 1.6 ms 출력 완료보다 빠를 수 있음.
```

변경:

```text
frame_gen_done = tx_frame_generator.frame_done
frame_gen_done 발생 → last_symbol_pending = 1
최종 frame_done = last_symbol_pending && symbol_done
```

검증:

```text
Preamble SYNC ×4 PASS
Data Symbol 32개 PASS
D50041C0 PASS
frame_gen_start = 1회 PASS
최종 frame_done = 1회 PASS
Optical Rising Edge = 816 PASS
Frame ID 00 → 01 PASS
PASS = 18 / FAIL = 0
```

## TX-7 AXI4-Lite Wrapper — PASS

파일:

```text
RTL: rtl/tx/axi_lite_tx_wrapper.v
TB : sim/tx/tb_axi_lite_tx_wrapper.v
XPR: tb_xpr/tb_axi_lite_tx_wrapper/tb_axi_lite_tx_wrapper.xpr
```

Vivado / XSim:

```text
AXI LITE TX WRAPPER TEST RESULT : PASS
PASS = 17
FAIL = 0
```

검증:

```text
Reset PASS
TX_DATA Write / Readback PASS
STATUS READY/BUSY Read PASS
START → char_valid 1 Clock Pulse PASS
Busy 중 START Ignore PASS
AW First PASS
W First PASS
AXI Write Response OKAY PASS
AXI Read Response OKAY PASS
```

### TX-7 Testbench 수정 이력

초기 문제:

```text
axi_write task가 AWVALID / WVALID을 동시에 올린 뒤
AW Handshake와 W Handshake를 순차적으로 기다렸다.

DUT가 AW와 W를 같은 Cycle에 모두 수락하면
W Handshake가 이미 완료되었는데 TB가 이후 W를 다시 기다리면서
Simulation이 정지할 수 있었다.
```

수정:

```text
AW/W 동시 Write에서는 awready && wready를 함께 확인하고,
같은 Rising Edge에서 AW/W Handshake가 모두 완료된 것으로 처리하도록 수정.
```

결과:

```text
AW/W 동시 수락 정상
AW First PASS
W First PASS
PASS = 17 / FAIL = 0
```

---

## TX-8 AXI4-Lite + Optical TX Integration — PASS

파일:

```text
RTL: rtl/tx/optical_tx_axi_top.v
TB : sim/tx/tb_optical_tx_axi_top.v
XPR: tb_xpr/tb_optical_tx_axi_top/tb_optical_tx_axi_top.xpr
```

구현 Commit:

```text
4e7bc82
feat: integrate AXI4-Lite with optical TX and verify end-to-end
```

통합 구조:

```text
Zynq PS / AXI4-Lite Master
→ axi_lite_tx_wrapper
→ char_id / char_valid / char_ready / tx_busy
→ optical_tx_top
→ optical_tx / tx_enable
```

Reset 연결:

```text
s_axi_aresetn = AXI active-low Reset
optical_tx_top.rst = ~s_axi_aresetn
```

Clock Domain:

```text
AXI Clock = TX Clock = s_axi_aclk
동일 Clock Domain으로 통합
```

TB 검증 항목:

```text
Reset 후 STATUS READY
AXI Write DATA=0x41 + START
READY / BUSY Status 변화
SYNC Symbol ×4
Data Symbol 32개
Frame D50041C0 복원
Optical Rising Edge 816
Busy 중 START Ignore
Frame ID 00 → 01
두 번째 Character AW First 전체 송신
세 번째 Character W First 전체 송신
연속 송신 Frame ID 증가
AXI Read / Write Response OKAY
```

Simulation 가속:

```text
System Clock = 100 MHz 유지
TB 전용으로 FS / BFSK 주파수를 100배 Scaling

FS_HZ  = 16 MHz
F0_HZ  = 1 MHz
F1_HZ  = 2 MHz
FSYNC   = 2.5 MHz
Symbol  = 16 us

주파수 비율은 유지되므로 Symbol당 Rising Edge는 동일:
BIT0 = 16
BIT1 = 32
SYNC = 40
```

실제 10 / 20 / 25 kHz 및 Symbol 1.6 ms Timing은 TX-4 / TX-6에서 이미 검증 완료하였다.

Vivado / XSim:

```text
OPTICAL TX AXI TOP TEST RESULT : PASS
PASS = 48
FAIL = 0
```

결론:

```text
TX-8 AXI4-Lite + Optical TX 전체 통합 Simulation PASS
AXI Register Write에서 Optical Carrier 출력까지 End-to-End 검증 완료
TX-9 Hardware Verification 진행 가능
```

---

# 6. 설계 / 인터페이스 변경 이력

| 날짜 | 영역 | 변경 전 | 변경 후 | 이유 / 결과 |
|---|---|---|---|---|
| 2026-09-14 | FFT | 256 point | 128 point | 현재 공통 규격 확정 |
| 2026-09-14 | FFT Block | 256 Sample / 1.6 ms | 128 Sample / 0.8 ms | 128-point FFT 기준 |
| 2026-09-14 | Symbol | 512 Sample / 3.2 ms | 256 Sample / 1.6 ms | 2 × 128-sample FFT Block |
| 2026-09-14 | Preamble | 12.8 ms | 6.4 ms | SYNC ×4 × 1.6 ms |
| 2026-09-16 | TX FSM → Mapper | SYNC 경로 없음 | `sync_valid / sync_ready` | Preamble SYNC 요청 전용 Handshake |
| 2026-09-17 | Carrier Counter | `+ 1'b0` | `+ 1'b1` | `symbol_done` 미발생 버그 수정 |
| 2026-09-17 | TX FSM | 미구현 | Preamble / Frame 순서 FSM | PASS=15 / FAIL=0 |
| 2026-09-17 | Frame 완료 의미 | Generator `frame_done` | `frame_gen_done` + 최종 `frame_done` 분리 | 마지막 Optical Symbol 완료까지 대기 |
| 2026-09-17 | Optical Frame Done | 마지막 Bit 전달 시점 | `last_symbol_pending && symbol_done` | 실제 Carrier 완료 기준 |
| 2026-09-17 | CNN → TX 입력 | 직접 `char_id / char_valid` | Zynq PS → AXI4-Lite Wrapper → `optical_tx_top` | PS에서 CNN 결과 전달 |
| 2026-09-17 | `optical_tx_top` Interface | 변경 검토 | `char_id / char_valid / char_ready / tx_busy` 유지 | TX-6 PASS 구조 보존 |
| 2026-09-17 | 개발 순서 | TX-7 Hardware | TX-7 AXI Wrapper → TX-8 통합 → TX-9 Hardware | PS 연동 우선 |
| 2026-09-17 | TX-7 Wrapper | 미구현 | `axi_lite_tx_wrapper.v` | PASS=17 / FAIL=0 / `334e7f1` |
| 2026-09-17 | AXI Register Map | 미확정 | `0x00 DATA`, `0x04 CTRL`, `0x08 STATUS` | Character / START / READY / BUSY 접근 |
| 2026-09-17 | AXI Write 순서 | AW/W 동시 전제 가능성 | AW와 W 독립 Pending 처리 | AW First / W First 모두 PASS |
| 2026-09-17 | START 정책 | 미확정 | `char_ready=1`일 때만 `char_valid` 1-Clock Pulse | Busy 중 START Ignore PASS |
| 2026-09-17 | TX-7 TB `axi_write` | AW와 W Handshake를 순차 대기 | AW/W 동시 수락을 한 번에 처리 | 이미 완료된 W Handshake를 놓치는 TB 정지 문제 해결 |
| 2026-09-17 | TX-8 Integration Top | Wrapper / Optical Top 개별 검증 | `optical_tx_axi_top.v`로 AXI4-Lite + Optical TX 통합 | Commit `4e7bc82`, TB/XPR 반영 |
| 2026-09-17 | Reset 연결 | AXI active-low / TX active-high 분리 | `tx_rst = ~s_axi_aresetn` | 단일 외부 Reset으로 Wrapper + TX Core 통합 |
| 2026-09-17 | TX-8 Simulation | 실제 1.6 ms Symbol | TB에서 FS/BFSK 주파수 100× Scaling | End-to-end Simulation 시간 단축, Symbol당 Edge 수 유지 |
| 2026-09-17 | TX-8 End-to-End 검증 | 결과 확인 대기 | AXI → Wrapper → Optical TX 전체 PASS | PASS=48 / FAIL=0 |

---

# 7. 현재 단계 요약

```text
TX-1 CRC-8               PASS
TX-2 Frame Generator     PASS
TX-3 BFSK Mapper         PASS
TX-4 Carrier Generator   PASS
TX-5 TX FSM              PASS
TX-6 Optical TX Top      PASS
TX-7 AXI4-Lite Wrapper   PASS
TX-8 AXI + Optical Top   PASS 48 / 0
TX-9 Hardware            다음 단계
```

---

# 8. 다음 작업

## TX-9 Hardware Verification

```text
1. Zynq PS에서 AXI4-Lite Register Write
   - 0x00 TX_DATA
   - 0x04 TX_CTRL.START
   - 0x08 TX_STATUS 확인
2. XDC 작성
   - Clock / Reset / optical_tx
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

Interface Excel:

```text
TX-7 Register Map + TX-8 외부 AXI4-Lite Top Port를
문서 정리 단계에서 최신 RTL 기준으로 일괄 반영한다.
```

---

# 9. Git / 문서 동기화 상태

TX-8 구현 / TB 파일은 Git 원격 반영 완료:

```text
rtl/tx/optical_tx_axi_top.v
sim/tx/tb_optical_tx_axi_top.v
tb_xpr/tb_optical_tx_axi_top/tb_optical_tx_axi_top.xpr
```

구현 Commit:

```text
4e7bc82
feat: integrate AXI4-Lite with optical TX and verify end-to-end
```

검증 결과:

```text
OPTICAL TX AXI TOP TEST RESULT : PASS
PASS = 48
FAIL = 0
```

현재 검증 문서 상태:

```text
TX-8 RTL / TB / XPR = 확인 완료
TX-8 End-to-End Simulation = PASS 48 / FAIL 0
TX-8 공식 PASS 처리 = 완료
TX-9 Hardware Verification = 다음 단계
```

이번 문서 동기화 대상:

```text
progress.md
Notion Progress
Notion Testbench 결과
```

Interface Excel은 이번 단계에서 직접 수정하지 않는다.

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
