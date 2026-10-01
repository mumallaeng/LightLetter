# BFSK Rx — 최종 RX 작업본

TEMP에서 실제 사용한 자체 FFT + 광통신 RX + Zynq PS + UI 구성입니다.
원본 작업 폴더는 변경하지 않고 복사했습니다. 비트스트림/ELF/UI 버전을 맞춰 묶었습니다.

```text
BFSK_Rx/
├─ rtl/           자체 FFT, RX, 스냅샷 RTL (수정 기준)
├─ constraints/   Zybo Z7-20 핀·클럭 제약
├─ vivado/        BD, XADC 설정, 프로젝트 생성 스크립트
├─ hardware/      현재 플랫폼의 비트스트림 포함 XSA
├─ vitis/src/     현재 실행 앱의 HAL / DRV / APP / main
├─ prebuilt/      대응하는 BIT, JSON·FFT ELF, FSBL, PS 초기화
├─ ui/            문자·FFT 웹 UI (DC 제외·로그 스케일 포함)
├─ tests/         스냅샷 RTL / PS·UI 테스트
├─ docs/          연결·검증 설명과 구현 리포트
└─ tools/         패키지 무결성 검사
```

## 현재 데이터 흐름

XADC → 자체 fft_top → rx_top → rx_latch → GPIO0 → PS → UI 문자

FFT power/valid → fft_snapshot_buffer → GPIO1/2 → PS → UI FFT

GPIO0=0x41200000, GPIO1=0x41210000, GPIO2=0x41220000.
GPIO1 CH1 제어/CH2 상태, GPIO2 CH1 전력 하위32/CH2 상위8.

## 바로 실행

이미 구성된 Vitis 실행 환경에서는 prebuilt/design_1_wrapper.bit로 FPGA를 구성하고
대응하는 PS 초기화 후 prebuilt/BFSK_RX_UART.elf를 Cortex-A9 0에 다운로드합니다.
BIT만 다운로드해서는 PS 앱이 실행되지 않습니다. prebuilt는 이번 JSON+FFT 버전입니다.

START_UI.cmd 또는 `python ui/server.py`를 실행하고 Chrome/Edge에서
http://localhost:8765 를 엽니다. UART 연결에서 보드 COM 포트를 선택합니다.
115200/8N1이며 같은 COM을 사용 중인 Vitis Serial Terminal/PuTTY는 닫습니다.
다른 UI 서버가 8765에서 실행 중이면 먼저 종료하거나 그 UI의 경로를 확인하세요.

## 소스에서 Vivado 다시 만들기

Vivado 2020.2 Command Prompt에서 CREATE_VIVADO.cmd를 실행하거나
`vivado -mode batch -source vivado/create_project.tcl`을 실행합니다.
Git 저장소에는 생성 프로젝트를 넣지 않습니다. 처음에는 CREATE_VIVADO.cmd를 실행하고,
이후 생성된 vivado/project/rx_final.xpr를 열면 됩니다.
vivado/design_1은 원본 BD/IP 설정 보관용이며, 작업용 BD는 project 안에 있습니다.
재생성 시 recreate_bd.tcl이 원본과 같은 IP 설정·배선·주소를 구성합니다.
Generate Output Products 후 Generate Bitstream을 실행합니다.
Export Hardware에서는 Include bitstream을 선택합니다.
이 프로젝트는 rtl/ 파일을 직접 참조하므로 오래된 imports 복사본과 혼동하지 않아도 됩니다.

## 소스에서 Vitis 다시 만들기

Vitis 2020.2 Command Prompt에서 CREATE_VITIS.cmd 또는
`xsct vitis/create_workspace.tcl`을 실행합니다.
생성된 vitis/workspace를 Vitis에서 열어 사용합니다.
생성 스크립트는 기존 workspace를 덮어쓰지 않습니다.
다른 Vitis 인스턴스가 workspace를 점유하지 않도록 실행하세요.
vitis/src는 배포 소스이고, 새 workspace의 앱에는 복사됩니다. 이후 편집 시
어느 쪽을 수정했는지 구분해야 합니다.

## 주의할 실행 버전

과거 CSV ELF를 실행하면 UART 원문에만 보이고 문자열/FFT는 갱신되지 않습니다.
이번 ELF는 type=rx / fft / status JSONL을 보냅니다.
PC의 파일을 바꾸는 것만으로 보드 앱이 바뀌지 않으므로 새 ELF를 재다운로드해야 합니다.
PL과 PS가 함께 변경될 때는 BIT와 XSA/BSP도 맞춰야 합니다.

## 검증

`python tools/verify_package.py`: 파일 체크섬과 XSA/BIT, ELF 버전 표식 검사.
`python tests/run_host_tests.py`: Python/GCC/Node 필요, 보드 없이 PS·UI 프로토콜 검증.
Vivado Command Prompt에서 tests/run_snapshot.cmd: 버퍼 261개 읽기 검증.
세부 검증 범위는 docs/VALIDATION.md를 보세요.

## 커밋 구분과 출처

가져온 기준본은 첫 `regacy` 커밋에 모으고 이후 변경은 분야별로 분리했습니다.
상세 출처와 분류 기준은 [docs/PROVENANCE.md](docs/PROVENANCE.md)를 보세요.
