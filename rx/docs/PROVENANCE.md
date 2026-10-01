# 출처와 커밋 분류

`regacy`는 요청한 커밋 이름을 그대로 사용한 것으로, 이번 공동 변경 전의 가져온 기준본을 뜻합니다.
파일을 모두 새로 작성했다는 뜻이 아닙니다. 원본의 저작권 주석은 보존합니다.

| 순서 | 구분 | 내용 |
|---|---|---|
| 1 | regacy | 기존 FFT/RX RTL, XADC 설정, 핀 제약, 기존 RX 테스트, 수정 전 UART와 UI |
| 2 | firmware 구조 분리 | UART를 HAL/DRV/APP로 분리한 변경 |
| 3 | RTL | GPIO 스냅샷 버퍼와 단독 검증 |
| 4 | Vivado | 스냅샷 GPIO/최종 BD 및 프로젝트 재생성 |
| 5 | firmware 기능 | 비동기 ACK/스냅샷, JSONL, 큐, UART 전송과 테스트 |
| 6 | UI | 문자/FFT 연결, DC 제외 및 로그 표시 |
| 7 | release | 대응하는 BIT/XSA/ELF와 플랫폼 재생성 |
| 8 | docs | 구성·검증·출처 설명, 체크섬과 제외 규칙 |

## 가져온 파일

- FFT/RX, XADC와 제약: `TEMP/BFSK_Rx_FINAL/tb_xpr/2026_final_project`에서 실제 사용한 기준본.
  이 기록은 가져온 위치를 나타내며, 각 RTL의 외부 Git 원본 커밋까지 단정하지 않습니다.
- UART 원문: [mumallaeng/LightLetter](https://github.com/mumallaeng/LightLetter/tree/ab0922572f6dabb7570dd47a0470534b379276fb/vitis/BFSK_RX_UART/src),
  커밋 `ab0922572f6dabb7570dd47a0470534b379276fb`.
  로컬 `BFSK_RX_UART/original/src`의 6개 파일을 첫 커밋에 보존했고,
  두 번째 커밋부터 같은 경로에서 구조를 바꿨으므로 수정 전 코드는 Git 이력에서 확인할 수 있습니다.
- UI 원문: [mumallaeng/LightLetter feat/ui-dashboard](https://github.com/mumallaeng/LightLetter/tree/54271a4307fc3492f8417fbd15a71771ee6b8b1a/ui),
  커밋 `54271a4307fc3492f8417fbd15a71771ee6b8b1a`의 5개 파일.
  파일 이름만으로 신규/기존을 구분하지 않고 원문을 먼저 커밋한 뒤 실제 수정 차이를 남겼습니다.

## 최종본과의 관계

기준 패키지는 `TEMP/RX_FINAL_20260929`입니다. RTL·앱 소스·UI 동작 코드와
배포 BIT/XSA/ELF는 그 패키지와 동일합니다. README/출처·검증 문서와 체크섬은
Git용으로 조정했으며 Vivado 생성 프로젝트와 Vitis workspace는 추적하지 않습니다.

BD는 공동 작업 후 최종 연결이므로 `regacy`에 넣지 않았습니다. 과거 BD를 추정해
만들지 않고 저장된 최종본과 재생성 Tcl을 Vivado 커밋에 넣었습니다.
빌드 산출물은 외부 IP와 공동 변경을 함께 포함하므로 별도 release 커밋에 모았습니다.
원본 경로·해시는 `SOURCE_MANIFEST.json`, 현재 저장소 해시는 루트 `SHA256SUMS.json`에 있습니다.
