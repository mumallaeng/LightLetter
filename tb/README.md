# 확정 모델 — C2-P1-S1-F3, FC 676→256→64→36

`cnn_golden/model.py`가 정의하는 단일 구조가 현재 확정 후보다. Conv 2층(3×3, zero-padding
1, stride 1, 층마다 출력 채널 1개) → 각 Conv 뒤 ReLU → MaxPool(2×2, stride 1) →
Flatten(26×26=676) → FC1(676→256) → ReLU → FC2(256→64) → ReLU → FC3(64→36) → argmax.

16개 구조(C∈{2,3}·padding∈{0,1}·pool stride∈{1,2}·FC층∈{2,3})를 비교한 스윕과 FC 폭
비교(64→32→36 vs 256→64→36) 실험 끝에 이 구성으로 확정했다. 그 탐색 과정의 코드·결과는
이 repo가 아니라 `Vault/projects/LightLetter/sweep-exploration/`(개인 학습 자료)에 있다.
과거 A계열 모델(`legacy_a_models/`)은 폐기했다.

## 실험 조건

| 항목 | 설정 |
|---|---|
| 데이터 | ByClass-Uppercase-Digits (36 class) |
| Conv | 2층, 층마다 입력/출력 채널 1, kernel 3×3, stride 1, zero-padding 1 |
| Pool | 모든 Conv → ReLU 뒤 MaxPool 2×2, stride 1 |
| FC | Flatten(676) → 256 → ReLU → 64 → ReLU → 36 |
| 학습 | 전체 train, batch 128, 10 epoch, 문자 가중 CrossEntropyLoss, backpropagation |
| 문자 손실 가중치 | 숫자 1, 문자는 (숫자 표본 수)/(문자 표본 수) — train 그룹 총량 기준 자동 계산 |
| Conv 초기화 | 중심 tap 1, 나머지 weight/bias 0; 첫 batch Conv gradient 검증 |
| Optimizer | Adam lr 0.001, betas=(0.9,0.999), eps=1e-8, weight_decay=0 |
| 부가 설정 | 증강·스케줄러 없음, seed 261014, 마지막 epoch 사용, test 튜닝 없음 |
| Device | GPU MPS 또는 CUDA; CPU 학습 fallback 금지 |
| QAT | 첫 batch부터 signed INT16 가중치·활성값 fake quantization |

INT16은 FP16이 아니다. tensor별 대칭·power-of-two scale과 round-to-even,
[-32768,32767] clipping, straight-through gradient를 사용한다.
활성값 observer는 학습 데이터의 running maximum을 사용하고 평가 때 고정한다.
weight scale은 매 update의 현재 가중치로 계산하고 epoch 끝에 한 번 갱신한다.
Float 비교는 동일 QAT 가중치에서 fake quantization만 끈 결과이며, 별도로
학습한 FP32 baseline과의 비교가 아니다. bias와 MAC 누산은 FP32다.
이 점수는 INT16 경계 오차를 모사한 예측이며 정수 누산·bias 양자화·RTL 검증이 아니다.

## 실행 및 결과

`tb`에서 실행한다. 기존 전체 데이터 캐시를 재사용하며 원본 데이터는 변경하지 않는다.

```bash
.venv/bin/python -m unittest discover -s tests -v
.venv/bin/python -u -m cnn_golden.train \
  --cache results/260913-full-data \
  --output results/<run-name> --device mps
```

같은 명령을 다시 실행하면 완료 모델은 건너뛰고 미완료면 마지막 저장 epoch부터 재개한다.
동일 출력 경로에서 두 학습 프로세스를 동시에 실행하면 안 된다. 소스·환경·조건 manifest가
다르면 재사용을 거부한다. MPS의 bit-level 재현성은 보장하지 않는다.

- `results/<run-name>/ByClass-Uppercase-Digits/result.json`: 데이터 출처, 전체 행 수,
  loss 이력, qat16/float_same_weights 점수, confusion matrix, inventory(weight-only
  BRAM 하한).
- `.../last.pt`: 학습 가중치·observer·Adam 상태·완료 epoch.
- `.../predictions.npz`: test CSV 순서의 전체 정답과 두 방식의 예측.

BRAM 필드는 INT16 weight만 연속 적재할 때의 하한이며, bias·feature·accumulator·banking·
제어 자원은 포함하지 않는다.

참고: [PyTorch Adam](https://docs.pytorch.org/docs/stable/generated/torch.optim.Adam),
[PyTorch FakeQuantize](https://docs.pytorch.org/docs/2.14/generated/torch.ao.quantization.fake_quantize.FakeQuantize.html).
