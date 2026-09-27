# Git / 진행상황 동기화 운영 규칙

## 1. 기본 역할 분담

### 사용자

RTL / Testbench / Vivado Project 등 구현 및 검증 파일을 담당한다.

검증 완료 후 다음 순서로 Git에 반영한다.

```text
RTL 작성
→ Testbench 작성
→ Vivado / XSim 검증
→ PASS 확인
→ RTL / TB / XPR Git Commit
→ Push
→ ChatGPT에 진행상황 업데이트 요청
```

### ChatGPT

사용자의 코드 Push 이후 GitHub 원격 저장소를 직접 확인한다.

확인 대상:

```text
최신 Commit
실제 RTL 파일
실제 Testbench 파일
Vivado XPR
인터페이스 변경
progress.md
TX Interface Excel
```

검증된 내용만 완료 처리한다.

---

## 2. PASS 처리 규칙

다음 조건을 만족할 때만 `PASS / 완료`로 기록한다.

```text
1. RTL 파일 존재
2. Testbench 파일 존재
3. Vivado / XSim 실제 PASS 결과 존재
4. GitHub 원격 저장소에 구현 파일 반영 확인
```

코드 검토만으로 PASS 처리하지 않는다.

사용자가 제공한 Vivado/XSim 결과와 Git에 올라간 RTL/TB가 일치하는지 확인한다.

---

## 3. progress.md 기준

`progress.md`는 프로젝트의 Single Source of Truth로 사용한다.

진행상황 업데이트 시 반드시 다음을 확인한다.

```text
현재 실제 Git 파일 구조
최신 인터페이스 명세
최근 PASS 결과
설계 변경 이력
공통 Parameter
```

실제 저장소에 존재하지 않는 모듈을 구현 완료 항목으로 기록하지 않는다.

예:

```text
rx_symbol_sync.v가 저장소에 없으면
구현 완료 모듈처럼 progress.md에 기록하지 않는다.
```

향후 계획과 현재 구현은 명확히 구분한다.

---

## 4. Interface 기준

RTL 작성 및 문서 업데이트 시 다음 우선순위를 사용한다.

```text
1. 실제 최신 RTL 인터페이스
2. 최신 TX Interface Specification
3. progress.md
```

인터페이스 변경이 발생하면 반드시 다음 두 문서에 동시에 반영한다.

```text
progress.md
docs/BFSK_TX_Interface_Spec_Verilog.xlsx
```

변경 이력에는 최소한 다음을 기록한다.

```text
변경 전
변경 후
변경 이유
검증 결과
적용 날짜
```

---

## 5. 공통 규격 동기화

다음 값은 문서 전체에서 항상 동일해야 한다.

```text
FS_HZ
FFT_N
FFT_BLOCK_SAMPLES
SYMBOL_SAMPLES
F0_HZ
F1_HZ
FSYNC_HZ
F0_BIN
F1_BIN
FSYNC_BIN
PREAMBLE_SYMBOLS
CRC 규격
Bit Order
```

현재 기준:

```text
FS_HZ             = 160000
FFT_N             = 128

FFT_BLOCK_SAMPLES = 128
FFT_BLOCK_TIME    = 0.8 ms

SYMBOL_SAMPLES    = 256
SYMBOL_TIME       = 1.6 ms

PREAMBLE_SYMBOLS  = 4
PREAMBLE_TIME     = 6.4 ms

F0_HZ             = 10000
F1_HZ             = 20000
FSYNC_HZ          = 25000

F0_BIN            = 8
F1_BIN            = 16
FSYNC_BIN         = 20
```

다음 구형 값은 현재 규격으로 사용하지 않는다.

```text
FFT 256 point
FFT Block 256 Sample / 1.6 ms
Symbol 512 Sample / 3.2 ms
Preamble 12.8 ms
```

구형 값은 필요한 경우 변경 이력에만 남긴다.

---

## 6. Git Commit 운영 방식

### 구현 Commit — 사용자

RTL/TB/XPR 등의 실제 구현 파일은 사용자가 Commit/Push한다.

예:

```text
feat: verify BFSK carrier generator
```

### 문서 Commit — ChatGPT

사용자의 구현 Push를 확인한 뒤 ChatGPT가 다음 문서를 업데이트한다.

```text
progress.md
docs/BFSK_TX_Interface_Spec_Verilog.xlsx
Notion 진행상황
Notion Testbench 결과
```

Git 문서는 별도의 문서 Commit으로 관리한다.

예:

```text
docs: sync TX-4 carrier progress and interface spec
```

구현 Commit과 문서 Commit을 하나로 합치지 않는다.

---

## 7. 업데이트 요청 시 작업 순서

사용자가 다음과 같이 요청한다.

```text
Git에 올렸어. 확인하고 진행상황 업데이트해줘.
```

ChatGPT는 다음 순서로 작업한다.

```text
1. GitHub 최신 HEAD 확인
2. 최신 Commit 확인
3. RTL / TB / XPR 확인
4. PASS 근거 확인
5. 실제 인터페이스 확인
6. progress.md 업데이트
7. Interface Excel 업데이트
8. Notion 업데이트
9. GitHub에 문서 Commit
10. 문서 Commit SHA 사용자에게 전달
```

---

## 8. 사용자 Pull 규칙

ChatGPT가 문서 Commit을 완료한 뒤 사용자는 다음 명령만 실행한다.

```powershell
git pull --ff-only
```

`--ff-only`를 사용하여 불필요한 Merge Commit 생성을 방지한다.

정상적인 작업 흐름:

```text
사용자 Implementation Push
        ↓
ChatGPT Verification
        ↓
ChatGPT Documentation Commit
        ↓
사용자 git pull --ff-only
        ↓
다음 RTL 작업 시작
```

---

## 9. 충돌 방지 규칙

진행상황 업데이트를 요청한 뒤 ChatGPT의 문서 Commit이 끝날 때까지 다음 파일은 사용자가 수정하지 않는다.

```text
progress.md
docs/BFSK_TX_Interface_Spec_Verilog.xlsx
```

가능하면 구현 Push 직후 Working Tree를 Clean 상태로 유지한다.

ChatGPT 문서 Commit을 Pull한 뒤 다음 작업을 시작한다.

사용자가 다음 RTL 작업을 먼저 시작해야 한다면 문서 파일은 수정하지 않는다.

---

## 10. ChatGPT Git 수정 범위

ChatGPT는 진행상황 업데이트 과정에서 기본적으로 다음 파일만 수정한다.

```text
progress.md
docs/BFSK_TX_Interface_Spec_Verilog.xlsx
```

RTL / Testbench / Vivado Project는 사용자의 명시적 요청 없이 수정하지 않는다.

기존 PASS RTL은 임의로 수정하거나 삭제하지 않는다.

---

## 11. 문서 Commit 실패 시 예외 처리

GitHub 연결 또는 파일 업로드 문제로 ChatGPT가 직접 문서 Commit을 만들 수 없는 경우에만 사용자에게 업데이트 파일을 제공한다.

이 경우 명확히 다음과 같이 알린다.

```text
자동 Commit 실패
→ 파일 생성 완료
→ 사용자가 직접 Commit 필요
```

평상시에는 사용자가 문서를 다시 Commit하도록 요구하지 않는다.

---

## 12. 진행상황 업데이트 결과 보고 형식

ChatGPT는 업데이트 완료 후 최소한 다음을 보고한다.

```text
확인한 구현 Commit
PASS 처리한 Module
업데이트한 문서
문서 Commit SHA
현재 개발 단계
다음 구현 대상
사용자 실행 명령
```

사용자 실행 명령은 기본적으로:

```powershell
git pull --ff-only
```

로 한다.
