# CNN RTL 검증 벡터

이 폴더(`cnn/rtl_ref/`)의 파일은 전부 C 골든모델(`cnn/golden_model`)이 만든 `$readmemh` 데이터다.
RTL 은 이 값과 **bit-exact** 여야 한다.

재생성:

```
make -C cnn/golden_model -f cnn_chain.mk rtl-vectors   # 이 폴더 전체를 다시 만든다
```

---

## 1. RTL 이 직접 읽는 ROM

| 파일 | 줄 수 | 폭 | 내용 |
|---|---|---|---|
| `conv1_weight.mem` | 12 = 6 och × 2 grp | 432 bit | conv_l1 weight ROM |
| `conv2_weight.mem` | 32 = 16 och × 2 grp | 432 bit | conv_l2 weight ROM |
| `conv1_bias_ce.mem` | 6 | 32 bit | conv_l1 bias (INT32) |
| `conv2_bias_ce.mem` | 16 | 32 bit | conv_l2 bias (INT32) |

### weight ROM entry 배치 (`weight_rom.c` 와 동일)

줄 순서는 `[out_ch][grp]`, `grp` = `is_ch35` (0 = in_ch 0\~2, 1 = in_ch 3\~5).
한 줄 = 432 bit = lane 3개 × tap 9개 × INT16, **lane0 이 LSB 쪽**:

```
bit  431..288 : lane2 (in_ch grp*3+2)
bit  287..144 : lane1 (in_ch grp*3+1)
bit  143..  0 : lane0 (in_ch grp*3+0)
lane 안에서 bit 15..0 = tap0, 31..16 = tap1, ... 143..128 = tap8   (tap k = ky*3 + kx)
```

conv1 은 `C_IN = 1` 이라 grp0 의 lane0 만 값이 있고 나머지는 전부 0 (홀수 줄이 전부 0 인 이유).

검산 — `conv1_weight.mem` 1번째 줄 하위 36 hex = `0847 ed93 b7cf 4fee e9f1 ecd7 0195 2d2e 4035`
→ tap0..tap8 = `16437, 11566, 405, -4905, -5647, 20462, -18481, -4717, 2119`
(= `vectors/conv_l1.txt` 의 weight 첫 9개)

### 양자화 파라미터

| | conv_l1 | conv_l2 |
|---|---|---|
| 입력 스케일 | 2^-14 (image) | 2^-13 (= conv_l1 출력) |
| weight 스케일 | 2^-15 | 2^-14 |
| accumulator | 2^-29 | 2^-27 |
| 출력 스케일 | 2^-13 | 2^-11 |
| `SCALE_EXP` (quantizer shift) | **16** | **16** |

quantizer: ReLU → `>> SCALE_EXP` with **round-half-to-even** → `32767` clamp (`relu_quant.c:quantizer_comb`).

---

## 2. 단계 경계 골든 스트림

`FRAMES = 2`. frame 0 = 실제 이미지, frame 1 = 좌우 반전. 모든 파일이 frame 0 전부 → frame 1 전부 순서.

| 파일 | 줄 수 | 폭 | 신호 |
|---|---|---|---|
| `ce1_stim.mem` | 1568 | 16 bit | conv_l1 `pixel_in` — 28×28 raster, 마지막 픽셀에 `ch_done` |
| `ce1_out.mem` | 2704 | 49 bit | conv_l1 출력 = pool_l1 입력 |
| `pool1_out.mem` | 676 | 49 bit | pool_l1 출력 = conv_l2 입력 |
| `ce2_out.mem` | 3872 | 17 bit | conv_l2 출력 = pool_l2 입력 |
| `pool2_out.mem` | 800 | 17 bit | pool_l2 출력 |
| `ce_params.txt` | — | — | 위 개수/차원/`SCALE_EXP` |

### 패킹

```
49 bit : {ch_done, data2[15:0], data1[15:0], data0[15:0]}   (PACK 3 / LANES 3)
17 bit : {ch_done, data[15:0]}                              (PACK 1 / LANES 1)
```

### 순서

| 단계 | 출력 순서 | `ch_done` 위치 |
|---|---|---|
| conv_l1 | pass0 = {och0,1,2} 26×26 raster → pass1 = {och3,4,5} 26×26 raster | pass 마지막 픽셀 (1352 entry/frame) |
| pool_l1 | pass0 13×13 raster → pass1 13×13 raster | pass 마지막 (12,12) (338 entry/frame) |
| conv_l2 | och0 11×11 raster → … → och15 | 채널마다 마지막 픽셀 (1936 entry/frame) |
| pool_l2 | och0 5×5 raster → … → och15 | 채널마다 (4,4). 11 은 홀수라 마지막 행/열은 버린다 (400 entry/frame) |

### 캡처 조건

`in_valid` / `out_ready` **100%** 로 캡처했다. 파일에 있는 건 *값의 순서*지 클럭이 아니므로,
RTL 테스트벤치는 원하는 backpressure 를 걸고 transfer 단위로 비교하면 된다.
참고로 100%/100% 체인 전체 지연은 **20872 cycle** (2 frame, conv_l1 idle 프레임 게이트 포함).

### 프레임 게이트

`out_reorder` 는 한 프레임만 담고 conv FSM 으로 가는 backpressure 가 없다.
그래서 다음 이미지는 conv_l1 이 앞 프레임을 다 내보낸 뒤 시작한다 (`chain_l1_idle`).
RTL 도 같은 제약을 지켜야 `rb_overrun` 이 안 난다.

---

## 3. Output Buffer 단독 벡터 (기존)

`conv1_*.mem` / `conv2_*.mem` / `conv{1,2}_params.txt` 는 `tb_output_buffer.v` 전용으로,
`gen_rtl_vectors.c` 가 `vectors/ob_conv{1,2}.txt` 에서 만든다. 위 CE 벡터와는 별개 세트다.

> **주의 — 두 세트의 bias 가 다르다.**
> `rtl/cnn/mem/conv1_bias.mem` (팀원 output_buffer TB 용) 첫 값은 `-109703176`, `conv1_bias_ce.mem` 은 `-85067728`.
> `vectors/ob_conv{1,2}.txt` 가 오래된 덤프에서 만들어진 것이고 (지금 JSON 으로 재생성하면
> `conv_l{1,2}.txt` 와 같은 bias 가 나온다), `vectors/conv_l{1,2}.txt` 쪽이 현재 모델과 일치한다.
> CE RTL 은 `*_bias_ce.mem` 을 써야 하고, `ob_conv*.txt` 는 별도로 갱신이 필요하다.
