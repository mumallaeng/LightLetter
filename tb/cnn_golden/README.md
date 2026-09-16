# 확정 모델 — C2-P1-S1-F3, FC 676→256→64→36 (재검토 중)

`cnn_golden/model.py`가 정의하는 구조는 아직 이 문서와 동일한 채널 1개짜리 이전 확정안이다
(최신 커밋 기준). 이후 BRAM 예산(XC7Z020 630KB) 검토 과정에서 채널 1→6→16 확장이
정확도를 끌어올리는 것으로 확인됐고, 지금은 `cnn_golden.ipynb`에서 채널·패딩·FC폭 조합을
실측으로 재확정하는 중이다 — 결론이 나기 전까지 `data.py`/`model.py`/`train.py`/
`tests/test_model.py`는 건드리지 않고, 확정되는 순간 이 문서와 함께 갱신한다.

현재까지 노트북에서 실측한 결과 (10 epoch, 동일 hyperparameter):

| 구성 | flatten | weights | BRAM 배율 | 전체 정확도 | 문자 정확도 |
|---|---:|---:|---:|---:|---:|
| 채널 1개 (이 문서의 확정안) | 676 | 191,762 | 0.59x | 90.92% | 87.37% |
| 채널 1→6→16, padding=1 | 10,816 | 2,788,502 | 8.64x | 92.27% | 89.28% |
| 채널 1→6→16, padding=0 | 7,744 | 2,002,070 | 6.21x | 92.34% | 89.02% |
| 채널 1→6→16, padding=0, FC1=128 | 7,744 | 1,002,646 | **3.11x** | 92.16% | 89.37% |

채널 확장은 정확도를 확실히 올리지만 BRAM을 최대 8.6배까지 초과시킨다. padding 제거는
정확도 손실 없이 BRAM을 28% 줄이고, 여기에 FC1 폭을 256→128로 더 줄이면 weights가 거의
절반(2,002,070 → 1,002,646)이 되면서 BRAM 배율도 6.21x → 3.11x로 낮아진다 — 정확도는
전체 -0.18%p, 문자 +0.35%p로 노이즈 범위 안의 변화다. padding 제거+FC1 축소를 합치면
원래 확정안(8.64x) 대비 BRAM을 약 64% 줄인 셈이지만, 예산(630KB)은 여전히 3.11배
초과 상태라 FC를 더 줄이거나 INT8/INT4 양자화 등을 추가로 검토해야 한다.

16개 구조(C∈{2,3}·padding∈{0,1}·pool stride∈{1,2}·FC층∈{2,3})를 비교한 스윕과 FC 폭
비교(64→32→36 vs 256→64→36) 실험 끝에 이전 확정안(채널 1개)을 확정했었다. 그 탐색 과정의
코드·결과는 이 repo가 아니라 `Vault/projects/LightLetter/sweep-exploration/`(개인 학습 자료)에
있다. 과거 A계열 모델(`legacy_a_models/`)은 폐기했다.

데이터는 `cnn_golden/data.py`가 `torchvision.datasets.EMNIST(split="byclass")`로 직접
받아온다 — 팀원마다 다른 ZIP을 안 갖고 있어도 누구나 실행할 수 있게 하기 위해서다.
byclass는 62클래스(숫자 0-9, 대문자 10-35, 소문자 36-61) 순서라 36 미만만 남기면 재매핑
없이 그대로 쓸 수 있다. torchvision의 EMNIST 원본은 90도 회전+반전된 상태로 오므로
`data.py`가 전치(transpose)로 되돌린다(회전을 안 고치면 결과가 안 나오는 게 아니라 조용히
틀린 방향의 글자를 학습하게 된다). 이 파이프라인은 이전 팀 ZIP 기반 학습과 대조해서
loss curve·정확도가 소수점까지 동일하게 재현되는 것으로 검증했다.

## 실험 조건

| 항목 | 설정 |
|---|---|
| 데이터 | EMNIST ByClass, 숫자+대문자만 필터링 (36 class) |
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

`tb`에서 실행한다. `--data-root`는 torchvision이 EMNIST 원본을 내려받아 캐싱해두는
로컬 디렉터리로, 최초 실행 시에만 다운로드가 발생하고 이후엔 재사용한다.

```bash
.venv/bin/python -m unittest discover -s tests -v
.venv/bin/python -u -m cnn_golden.train \
  --data-root results/emnist \
  --output results/<run-name> --device mps
```

같은 명령을 다시 실행하면 완료 모델은 건너뛰고 미완료면 마지막 저장 epoch부터 재개한다.
동일 출력 경로에서 두 학습 프로세스를 동시에 실행하면 안 된다. 소스·환경·조건 manifest가
다르면 재사용을 거부한다. MPS의 bit-level 재현성은 보장하지 않는다.

- `results/<run-name>/ByClass-Uppercase-Digits/result.json`: 데이터 출처(data_source),
  전체 행 수, loss 이력, qat16/float_same_weights 점수, confusion matrix,
  inventory(weight-only BRAM 하한).
- `.../last.pt`: 학습 가중치·observer·Adam 상태·완료 epoch.
- `.../predictions.npz`: test CSV 순서의 전체 정답과 두 방식의 예측.

BRAM 필드는 INT16 weight만 연속 적재할 때의 하한이며, bias·feature·accumulator·banking·
제어 자원은 포함하지 않는다.

참고: [PyTorch Adam](https://docs.pytorch.org/docs/stable/generated/torch.optim.Adam),
[PyTorch FakeQuantize](https://docs.pytorch.org/docs/2.14/generated/torch.ao.quantization.fake_quantize.FakeQuantize.html).
