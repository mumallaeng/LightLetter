# LightLetter 송신 UI

Zybo HDMI 출력을 USB 캡처보드로 받아, 보드의 버튼 캡처 경로(ArUco 5칸 crop → `img_preprocess.v`)를 PC에서 그대로 재현해 보여 준다. 수신 UI(`rx/ui`)와 같은 테마의 웹 페이지이고, 보고 싶은 전처리 단계만 체크해서 띄운다.

## 실행

```text
pip install -r requirements.txt
python server.py                 # 0번 장치, http://localhost:8766
python server.py --list          # 0~4번 장치를 찾아 번호 확인
python server.py --device 1      # 다른 장치
python server.py --image captures/synth_frame.png    # 보드 없이 저장한 프레임으로
```

화면이 안 뜨면 `--list` 로 캡처보드 번호를 확인하고 `--device` 로 지정한다. 노트북 내장 웹캠이 0번이면 캡처보드는 1번 이상이다. 다른 프로그램(OBS 등)이 같은 캡처보드를 쓰고 있으면 닫고, 영상이 계속 안 나오면 `--no-mjpg` 를 붙인다.

Chrome 또는 Edge에서 `http://localhost:8766` 을 연다. 캡처보드는 Windows(DirectShow)에서 연다. 다른 OS에서는 `--image` 로 화면만 확인할 수 있다.

보드 없이 화면을 시험하려면 합성 프레임을 만든다. 실제 보드 영상이 아니라서 값 비교용이 아니다.

```text
python synth_frame.py            # captures/synth_frame.png
```

## 화면

| 영역 | 내용 |
| --- | --- |
| 촬영 화면 | 마커 6개와 칸 5개의 crop 영역을 겹쳐 보여 준다 |
| 표시할 전처리 단계 | 체크한 단계만 아래 패널에 표시한다. 선택은 브라우저에 저장된다 |
| 전처리 단계 | 단계마다 칸 5개를 한 줄로 보여 준다 |
| 시스템 상태 | 입력, 프레임 크기, ArUco 결과, 마커, 맞춤 오차와 시간 |
| CNN 입력값 | 칸을 골라 28×28 값을 텍스트로 본다 |
| 현재 프레임 저장 | `captures/` 에 프레임, 칸별 PNG, CNN 입력값을 저장한다 (`capture_test.py` 의 `s` 키와 같음) |

전처리 단계는 모드에 따라 다르다.

| 모드 | 단계 |
| --- | --- |
| ArUco 5칸 | 원본 crop, 왜곡 보정, homography 112×112, threshold + bbox, CNN 입력 28×28 |
| 정중앙 ROI | ROI 112×112, threshold + bbox, CNN 입력 28×28 |

원본 crop과 왜곡 보정은 PC 시각화용이다. 보드는 보정된 영상을 따로 만들지 않고 원본 프레임에서 112×112 를 바로 샘플한다.

## 구성

- `server.py`: 정적 페이지(`web/`)와 이미지·JSON API. 계산은 `capture_test.py` 의 함수를 그대로 쓴다.
- `web/`: 수신 UI(`rx/ui/style.css`)와 같은 테마에 전처리 단계 표시만 더한 페이지
- `capture_test.py`: OpenCV 창으로 같은 미리보기를 띄우는 기존 도구
- `aruco_c.py`: 보드의 ArUco C 코드를 numpy 로 옮긴 것
