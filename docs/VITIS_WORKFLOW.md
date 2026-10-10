# Vitis 생성·갱신·실행

## 실행 순서
1. 갱신할 워크스페이스를 사용하는 Vitis를 닫는다.
2. TX는 tx/CREATE_VITIS.cmd, RX는 rx/CREATE_VITIS.cmd를 실행한다.
3. BUILD_SUCCESS와 SUCCESS를 확인한 뒤 같은 폴더의 OPEN_VITIS.cmd를 실행한다.
4. Run/Debug 설정에서 실제 대상 보드를 선택하고 Program FPGA를 활성화한다. 스크립트는 보드에 자동 다운로드하지 않는다.

| 구분 | 입력 XSA | 생성 워크스페이스 | 앱 |
|---|---|---|---|
| TX | tx/vivado/export/tx_top_wrapper.xsa | tx/vitis/workspace | tx_fpga |
| RX | rx/vivado/export/FFT_RX_FINAL.xsa | rx/vitis/workspace | BFSK_RX_UART |

Vitis 2020.2 기본 설치 경로 C:/Xilinx/Vitis/2020.2를 사용한다.
입력 XSA에는 비트스트림이 포함되어야 한다. XSA의 내용이나 팀 원본 소스를 스크립트가 수정하지 않는다.
플랫폼 갱신과 앱 빌드 후 플랫폼 hw의 비트스트림 및 ps7_init.tcl을 앱 _ide 폴더에 동기화한다.
XSCT 종료 코드 외에 이번 실행의 완료 표식도 확인하여 오류를 성공으로 표시하지 않는다.

## 소스 보존
신규 앱은 TX의 tx/vitis/tx_fpga/src 또는 RX의 rx/vitis/src를 링크한다.
기존 앱의 소스는 재가져오기하지 않아 워크스페이스에서 수정한 파일을 덮어쓰지 않는다.
예전 방식으로 소스를 복사한 앱은 원본 폴더 변경이 자동 반영되지 않을 수 있다.
이 경우 기존 수정사항을 비교·보존한 뒤 새 워크스페이스를 생성한다. 기존 폴더를 자동 삭제하거나 초기화하지 않는다.
링크된 소스를 Vitis에서 편집하면 원본도 변경된다.

## 별도 검증 경로
기본 경로 대신 사용할 절대경로를 환경변수 BFSK_VITIS_WORKSPACE로 지정할 수 있다.
CREATE와 OPEN 모두 같은 환경변수를 사용한다. 지정하지 않으면 위 표의 경로를 사용한다.
자동 실행에서는 CREATE_VITIS.cmd --no-pause로 종료 대기를 생략할 수 있다.

## 2026-10-10 검증
- 기존 사용자 워크스페이스와 분리된 임시 경로에서 TX 생성 후 갱신, RX 최초 생성 및 재실행의 ARM ELF 빌드를 확인했다.
- 최초 TX 검사에서 export 하위에 비트스트림이 없고 XSCT 오류가 CMD 성공으로 표시되는 문제를 발견했다. 플랫폼 hw 경로 사용과 완료 표식 검사로 보완 후 성공을 확인했다.
- TX/RX 각각 플랫폼 hw와 앱 _ide의 bit 및 ps7_init.tcl SHA-256 일치를 확인했다.
- 기존 팀 RTL, C 소스, 카메라/CNN 코드와 XSA는 수정하지 않았다.
- TX 검증은 현재 로컬 XSA를 입력으로 사용했다. 해당 XSA 변경은 이 커밋에 포함하지 않으며, 원격 XSA로 동일한 빌드 결과나 통합 TX 보드 동작을 검증한 것으로 확대하지 않는다.
- OPEN의 경로·존재 검사 코드를 검토했다. 이 작업에서는 GUI를 새로 열거나 보드 다운로드를 수행하지 않았다.
