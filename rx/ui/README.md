# LightLetter FPGA 수신 UI

Zybo의 HDMI 영상을 USB 캡처보드로 표시하고, 외부 버튼 이벤트에 맞춰 FPGA RTL과 동일한 방식의 28×28 전처리 미리보기를 생성한다. CNN 추론은 PC에서 수행하지 않으며 향후 FPGA CNN의 UART 결과만 표시한다.

## 실행

저장소 루트에서 다음을 실행한다.

```text
python ui/server.py
```

Chrome 또는 Edge에서 `http://localhost:8765`를 연다. 영상 장치와 Web Serial은 localhost 권한이 필요하다.

## 연결

```text
Pcam → Zybo
Zybo HDMI OUT → USB 캡처보드 → PC
Zybo USB-UART → PC
```

촬영 화면에서 캡처보드를 켜고, 수신 화면에서 UART를 연결한다.

## UART JSONL

각 이벤트는 JSON 한 개와 줄바꿈으로 전송한다. 일반 Vitis 로그가 섞여도 UI는 JSON 행만 처리한다.

```json
{"type":"capture"}
{"type":"recognition","char":"A","class_id":0,"crc_ok":true}
```

`capture` 이벤트를 받으면 현재 HDMI 프레임을 촬영한다. CNN 연결 전에는 미리보기만 갱신된다. `recognition` 이벤트는 FPGA CNN 결과이며 `class_id`만 보낼 경우 0=A부터 25=Z로 변환한다.

## 미리보기 전처리

브라우저가 캡처보드 프레임에 다음 RTL 계산을 재현한다.

1. 1280×720 프레임의 중앙 224×224 ROI(X=528, Y=248)를 112×112 nearest로 샘플링 (`roi_dma.c`)
2. `(77R + 150G + 29B + 128) >> 8`
3. `255 - luma`
4. 결과가 160 미만이면 0, 나머지는 반전 명암 유지
5. 0이 아닌 픽셀의 bbox를 긴 변 22픽셀에 맞춰 28×28 가운데에 배치 (`img_preprocess.v`). 줄이는 축은 출력 한 픽셀이 담당하는 소스 구간 `[floor(i·N/M), ceil((i+1)·N/M)−1]`의 최댓값, 늘리는 축은 nearest 한 샘플

`tx/ui/capture_test.py`의 `ps_downscale`, `rtl_threshold`, `rtl_fit`과 같은 계산이다. ArUco 5칸 경로는 이 화면에 없고 송신 UI(`tx/ui`)에서 본다.

오른쪽 이미지는 사용자가 CNN 입력 형태를 확인하기 위한 PC 미리보기이며 FPGA 내부 784개 픽셀을 직접 전송한 것은 아니다.


## 광통신 RX / FFT 연결 (2026-09-29)

현재 이 패키지의 vitis의 dashboard 앱과 JSONL로 연결합니다.
- UI: Chrome 또는 Edge, localhost:8765, UART 115200 / 8N1.
- Vitis Serial Terminal, PuTTY 등 동일 COM 포트를 사용하는 프로그램을 닫고 연결합니다.
- `{ "type":"rx", "seq":0, "frame_id":1, "data":72, "received_crc":85, "crc_ok":true, "overrun":false }`
- `rx.data`는 0~255의 바이트입니다. 공백, CR, LF를 보존하며 검증 실패 이벤트는 문자열에 추가하지 않습니다.
- FFT: type="fft", seq=스냅샷 번호, bins=128개 unsigned 40비트 정수 배열. 배열 인덱스가 bin 번호입니다.
- 전체 JSON 뒤에 LF를 붙입니다. 일반 로그 행은 무시하며 잘못된/불완전한 FFT는 그래프에 반영하지 않습니다.
- 스펙트럼은 최대값에 맞춰 자동 스케일됩니다. bin 선택 입력으로 실제 전력값을 확인합니다. 샘플링 주파수를 확정하기 전까지 X축은 bin 번호입니다.
- type="status"는 초기화 결과, 소프트웨어 큐 유실, PL 덮어쓰기, 수신 ACK/FFT timeout을 표시합니다.
- 기존 recognition / capture 이벤트와 카메라 기능은 유지됩니다.

이 UI가 연결될 때 이미 PS 앱이 실행 중이어도 다음 FFT/문자 이벤트부터 표시됩니다.
PS는 최대 초당 2회 FFT를 캡처하며 UART가 밀리면 문자 전송을 우선합니다.
보드와의 물리 UART 연결은 별도의 실제 장치 시험이 필요합니다.

FFT 표시 기본값은 DC(bin 0) 제외 + 선형 자동 스케일입니다. DC를 포함하거나 로그 스케일(log10(1+power), 보정된 dB 단위 아님)로 바꿀 수 있습니다. 수신 원본 128개 값은 변경하지 않으며 bin 0을 선택하면 DC 원본 값도 확인할 수 있습니다.
