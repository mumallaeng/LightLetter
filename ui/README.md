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

1. 중앙 정사각형 crop(1280×720 입력은 X=280..999의 720×720)
2. nearest-point 28×28 샘플링
3. `(77R + 150G + 29B + 128) >> 8`
4. `255 - luma`
5. 결과가 100 미만이면 0, 나머지는 반전 명암 유지

오른쪽 이미지는 사용자가 CNN 입력 형태를 확인하기 위한 PC 미리보기이며 FPGA 내부 784개 픽셀을 직접 전송한 것은 아니다.
