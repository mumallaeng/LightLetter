# CNN 골든모델 — LeNet-5 3x3_schedule 확정

팀 논의 끝에 **`LeNet-5 3x3_schedule`**(`conv_channels=[1,6,16]`, `padding=0`,
`pool_stride=2`, kernel 3×3, FC 400→120→84→36, ReLU+MaxPool, INT16 QAT)을 최종
구조로 확정했다. `cnn_golden/model.py`가 정의하는 구조(채널 1개, FC 676→256→64→36)는
옛 확정안이라 지금은 안 맞고, `data.py`/`model.py`/`train.py`/`tests/test_model.py`를
이 확정 구조에 맞춰 갱신하는 작업이 다음 단계다.

비교에 쓴 5개 후보와 실측 근거는 아래 "후보 구조 비교" 절에 그대로 남겨둔다(왜 이
구조를 골랐는지 추적하기 위함).

## 후보 구조 비교

`cnn_golden.ipynb`에서 50epoch까지 학습한 5개 구성. 전부 `padding=0`, INT16 QAT,
동일 hyperparameter(batch 128, Adam lr=0.001, seed 261014)다.

**평가 방식**: train에서 10%를 validation으로 떼어 매 epoch은 validation으로만 추적하고,
test는 학습이 끝난 뒤 (1) validation 문자 정확도가 제일 높았던 epoch, (2) 마지막(50) epoch,
이 두 시점만 각각 한 번씩 평가했다. 아래 "정확도" 표는 **(1) 기준(정직한 최종 보고값)**이
기본이고, 참고용으로 (2)도 같이 적었다.

### 구성 요약

| 후보 | conv 채널 | kernel | pool_stride | 입력 | FC 폭 | activation/pooling |
| --- | --- | ---: | ---: | ---: | --- | --- |
| 1-6-16 | 1→6→16 | 3×3 | 1 | 28×28 | 7744→256→64→36 | ReLU / MaxPool |
| 1-6-8-8 | 1→6→8→8 (conv 3층) | 3×3 | 1 | 28×28 | 2888→256→64→36 | ReLU / MaxPool |
| **LeNet-5 3x3_schedule (확정)** | 1→6→16 | 3×3(프로젝트 제약) | 2 | 28×28 | 400→120→84→36 | ReLU / MaxPool |
| LeNet-5 authentic (원 논문) | 1→6→16 | 5×5 | 2 | 32×32(2px 패딩) | 400→120→84→36 | scaled-tanh / 학습되는 평균풀링 |
| LeNet-5 modern | 1→6→16 | 5×5 | 2 | 32×32(2px 패딩) | 400→120→84→36 | ReLU / MaxPool |

3번째 행("LeNet-5 3x3_schedule", **확정 구조**)은 프로젝트의 kernel 3×3 제약 안에서 LeNet-5의 공간 축소 스케줄
(28→26→13→11→5, 최종 5×5×16=400)을 재현한 것이고, 4·5번째 행은 원 논문 스펙대로
32×32/5×5를 그대로 쓴 참고용 비교다(프로젝트 제약과는 별개).

### BRAM (weight-only, INT16, XC7Z020 630KB 예산 기준)

| 후보 | weights | bytes | BRAM 배율 |
| --- | ---: | ---: | ---: |
| 1-6-16 | 2,002,070 | 4,004,140 | **6.21x** |
| 1-6-8-8 | 759,078 | 1,518,156 | **2.35x** |
| LeNet-5 3x3_schedule | 62,022 | 124,044 | **0.19x** |
| LeNet-5 authentic (원 논문) | 63,676 | 127,352 | **0.20x** |
| LeNet-5 modern | 63,654 | 127,308 | **0.20x** |

### 정확도 (validation 기준 best epoch에서 test 1회 평가)

문자 정확도(letter_accuracy) 내림차순.

| 후보 | best epoch | 전체 | 숫자 | **문자** | balanced |
| --- | ---: | ---: | ---: | ---: | ---: |
| **LeNet-5 authentic (원 논문)** | 19 | 91.59% | 91.62% | **91.55%** | 92.31% |
| LeNet-5 modern | 19 | 91.83% | 92.22% | 91.11% | 92.50% |
| 1-6-16 | 19 | 91.43% | 91.68% | 90.97% | 92.24% |
| **LeNet-5 3x3_schedule (확정)** | 19 | 91.93% | 92.69% | 90.54% | 92.24% |
| 1-6-8-8 | 2 | 89.66% | 89.06% | 90.76% | 90.58% |

참고: 마지막(epoch 50) 시점 test는 전부 문자 정확도가 87~88%대로 더 낮다(초반에 letter
정확도가 최고점을 찍고 이후 계속 떨어지는 경향이 5개 후보 전부에서 재현됨 — 숫자 정확도는
계속 오르는데 문자 정확도만 내려가는 트레이드오프).

### 정리

- **BRAM은 LeNet-5 계열 세 개가 압도적으로 작다** (0.2x 안팎, 1-6-16 대비 약 30배 작음) —
  Conv 채널 수는 같은데(1→6→16), `pool_stride=2`로 flatten이 400까지 줄어든 게 핵심.
- **문자 정확도는 LeNet-5 authentic (원 논문)이 1위(91.55%)**, 근소한 차이로 modern(91.11%),
  1-6-16(90.97%), LeNet-5 3x3_schedule(90.54%) 순. 5개 중 4개가 90.5~91.6% 구간에 몰려있어
  차이가 노이즈 수준에 가깝다.
- **1-6-8-8은 BRAM은 중간(2.35x)인데 정확도·안정성 모두 제일 떨어진다** — best epoch이
  2로 극단적으로 이르고, 그 지점의 다른 지표(전체 89.66%, 숫자 89.06%)도 낮다.
- **BRAM과 정확도를 같이 보면 LeNet-5 authentic (원 논문)이 가장 균형 잡힌 후보**: 가장 작은
  축에 속하는 BRAM(0.20x)이면서 문자 정확도 1위. 다만 scaled-tanh + 학습되는 평균풀링은
  하드웨어로 구현하기에 ReLU+MaxPool보다 복잡하다는 점은 감안해야 한다.

## 데이터 파이프라인

데이터는 `cnn_golden/data.py`가 `torchvision.datasets.EMNIST(split="byclass")`로 직접
받아온다 — 팀원마다 다른 ZIP을 안 갖고 있어도 누구나 실행할 수 있게 하기 위해서다.
byclass는 62클래스(숫자 0-9, 대문자 10-35, 소문자 36-61) 순서라 36 미만만 남기면 재매핑
없이 그대로 쓸 수 있다. torchvision의 EMNIST 원본은 90도 회전+반전된 상태로 오므로
`data.py`가 전치(transpose)로 되돌린다(회전을 안 고치면 결과가 안 나오는 게 아니라 조용히
틀린 방향의 글자를 학습하게 된다). 이 파이프라인은 이전 팀 ZIP 기반 학습과 대조해서
loss curve·정확도가 소수점까지 동일하게 재현되는 것으로 검증했다.

## 양자화(QAT) 메모

INT16은 FP16이 아니다. tensor별 대칭·power-of-two scale과 round-to-even,
[-32768,32767] clipping, straight-through gradient를 사용한다.
활성값 observer는 학습 데이터의 running maximum을 사용하고 평가 때 고정한다.
weight scale은 매 update의 현재 가중치로 계산하고 epoch 끝에 한 번 갱신한다.
이 점수는 INT16 경계 오차를 모사한 예측이며 정수 누산·bias 양자화·RTL 검증이 아니다.

BRAM 필드는 INT16 weight만 연속 적재할 때의 하한이며, bias·feature·accumulator·banking·
제어 자원은 포함하지 않는다.

## 실행

지금은 비교/탐색이 전부 `cnn_golden.ipynb`에서 이뤄진다(`tb`에서 `lightletter-tb` 커널로
연다). `cnn_golden/train.py` CLI(`--data-root`/`--output` 등)는 옛 채널 1개 기준이라 최종 후보가 정해지면 그에 맞춰 갱신한다.

참고: [PyTorch Adam](https://docs.pytorch.org/docs/stable/generated/torch.optim.Adam),
[PyTorch FakeQuantize](https://docs.pytorch.org/docs/2.14/generated/torch.ao.quantization.fake_quantize.FakeQuantize.html).
