# LightLetter macOS 수신 UI 초안

`tb/.venv/bin/python ui/server.py`를 `LightLetter` 루트에서 실행하고 macOS Chrome/Edge에서 `http://localhost:8765`를 연다. Web Serial과 카메라에는 브라우저 권한이 필요하다. 실제 UART 장치는 아직 연결·검증하지 않았다.

카메라에서 `28×28 입력 캡처`를 누르면 로컬 PyTorch 서버가 `C2-P0-S1-F3` INT16 QAT 체크포인트로 추론한다. 이 모델은 ByClass-Uppercase-Digits 전체 test에서 88.7469%였다. macOS 소프트웨어 사전 테스트이며 FPGA RTL 결과가 아니다. 예측은 오탐 후보 라벨링에 사용하고 UART 수신 문자열에는 자동으로 추가하지 않는다. 모델 가중치는 `tb/results/260914-uppercase-qat16-retrain/ByClass-Uppercase-Digits/C2-P0-S1-F3/last.pt`에서 읽는다.

오탐 검토 탭의 `한 글자 촬영 · 임시 인식`을 누르면 로컬 CNN 결과가 그 탭 오른쪽과 수신 대시보드의 최근 인식 문자·누적 문자열에 함께 추가된다. 상태의 `최근 결과 출처`는 `Mac 임시 CNN`이다. 이 버튼은 UART, 광통신, FPGA 추론을 통과하지 않으며 표시 흐름만 확인한다. 수신 대시보드에는 별도 카메라가 없다.

- `recognition` JSONL: `{"type":"recognition","id":"capture-001","char":"A","class_id":10,"crc_ok":true}`
- `fft` JSONL: `{"type":"fft","id":"capture-001","bins":[0.0,...]}`. 반드시 비음수 수치 128개.
- UART 속도는 현재 UI 시범값 115200 baud. 펌웨어와 합의 전까지 확정 사양이 아니다.
- FFT x축은 0–127번 bin. `Fs`가 없으므로 Hz로 표시하지 않는다.
- 오탐 후보는 같은 `id`의 로컬 CNN 예측 또는 UART 인식 결과와 28×28 카메라 캡처가 있을 때만 저장한다. `predictionSource`로 둘을 구분한다. 시범 UI에서 카메라가 자체 생성한 ID를 FPGA/펌웨어가 넘기도록 연결하는 통합 작업이 남아 있다.
- 브라우저 `localStorage`에 수집 후보가 저장되며 JSON/PNG로 내보낼 수 있다. 브라우저 데이터 삭제 전 JSON 백업이 필요하다.
- 카메라 전처리는 중앙 정사각형 crop → 28×28 resize → grayscale → 밝기 128 기준 이진화이다. 검은 픽셀은 모델 입력 0, 밝은 픽셀은 1이다. API 픽셀은 0/255 uint8로 전달되고 서버가 255로 나눈다. 이는 이전의 색 반전 입력을 대체한다. EMNIST 학습 데이터는 대체로 검은 배경·밝은 글씨라 흰 배경에 검은 글씨를 그대로 0/1화하면 극성이 반대일 수 있다. 실제 카메라 정확도를 다시 측정해야 하며, RTL 추론 및 EMNIST 학습 입력과 같은지 확인하기 전에는 재학습에 사용하면 안 된다.
- 카메라 선택에서 macOS가 노출하는 영상 장치를 고를 수 있다. iPhone을 연속성 카메라로 설정한 뒤 목록을 새로고침하고 iPhone을 선택한다. 카메라 권한 허용 전에는 이름이 보이지 않을 수 있다.
- UI에는 학습 실행 기능이 없다. 오프라인 라벨 검수 → 데이터 분리 → QAT 재학습 → 평가 → RTL 가중치 갱신이 별도로 필요하다.
