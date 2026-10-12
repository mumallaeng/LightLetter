"""보드 없이 CNN 인식 결과를 보는 소프트웨어 추론. 보드 ROM 과 같은 JSON 가중치로 INT16 정수 연산을 한다.

가중치는 tx/cnn/model/cnn_golden/results/layer_outputs/lenet5_3x3_schedule.json 이고, 정수 변환
(가중치 2^w_exp, bias 는 입력·가중치 scale 곱, 출력 scale_exp)은 tx/cnn/golden/export_conv_roms.py,
export_fc_vectors.py 와 같은 식이다. 그래서 JSON 에서 만든 .mem 을 올린 보드와 같은 logit 이 나온다.
"""
import hashlib
import json
import math
from pathlib import Path

import numpy as np

DEFAULT_WEIGHTS = (Path(__file__).resolve().parents[1]
                   / "cnn/model/cnn_golden/results/layer_outputs/lenet5_3x3_schedule.json")


def _pow2_scale_of(values, tol=0.05):
    v = np.asarray(values, dtype=np.float64).ravel()
    v = v[v != 0]
    for exp in range(0, -40, -1):
        k = v * 2.0 ** -exp
        if np.abs(k - np.rint(k)).max() < tol:
            return exp
    raise ValueError("no power-of-two grid found")


def _weight_exp(w):
    return math.ceil(math.log2(np.abs(w).max() / 32767.0))


def _shift_round(acc, s):
    """round-half-to-even >> s (relu_quant / quantizer.v)."""
    one = 1 << s
    q, rem = acc // one, acc % one
    return q + ((rem > one // 2) | ((rem == one // 2) & (q & 1 == 1)))


def _conv(x, w, b, s):
    c_in, h, _ = x.shape
    o = h - 2
    acc = np.zeros((w.shape[0], o, o), dtype=np.int64) + b[:, None, None]
    for ci in range(c_in):
        for ky in range(3):
            for kx in range(3):
                acc += w[:, ci, ky, kx][:, None, None] * x[ci, ky:ky + o, kx:kx + o][None]
    return np.clip(_shift_round(np.maximum(acc, 0), s), 0, 32767)


def _pool(x):
    c, h, _ = x.shape
    o = h // 2
    return x[:, :2 * o, :2 * o].reshape(c, o, 2, o, 2).max(axis=(2, 4))


class Infer:
    def __init__(self, path=DEFAULT_WEIGHTS):
        path = Path(path)
        raw = path.read_bytes()
        d = json.loads(raw)
        self.path, self.sha = path, hashlib.sha256(raw).hexdigest()[:8]
        self.names = d["class_names"]
        self.accuracy = d.get("source", {}).get("test_accuracy_qat16")
        stage = {s["name"]: np.array(s["values"], dtype=np.float64) for s in d["stages"]}
        layer = {w["layer"]: w for w in d["weights"]}

        in_exp = _pow2_scale_of(stage["input (quantized)"])
        self.layers = []
        for name in ("conv1", "conv2", "fc1", "fc2", "fc3"):
            w = np.array(layer[name]["weight_raw"], dtype=np.float64)
            b = np.array(layer[name]["bias"], dtype=np.float64)
            w_exp = _weight_exp(w)
            w_int = np.rint(w / 2.0 ** w_exp).astype(np.int64)
            acc_exp = in_exp + w_exp
            b_int = np.rint(b / 2.0 ** acc_exp).astype(np.int64)
            y = stage[{"conv1": "conv1 + ReLU (quantized)", "conv2": "conv2 + ReLU (quantized)",
                       "fc1": "fc1 (quantized+ReLU)", "fc2": "fc2 (quantized+ReLU)",
                       "fc3": "fc3 (quantized)"}[name]]
            out_exp = _pow2_scale_of(y)
            self.layers.append((name, w_int, b_int, out_exp - acc_exp))
            in_exp = out_exp

    def logits(self, cnn28):
        """cnn28: 28x28 정수 (2^-14 scale, capture_test.cnn_scale 의 출력). 길이 26 의 INT16 logit."""
        x = np.asarray(cnn28, dtype=np.int64)[None]
        for name, w, b, s in self.layers[:2]:
            x = _pool(_conv(x, w, b, s))
        x = x.reshape(-1)
        for i, (name, w, b, s) in enumerate(self.layers[2:]):
            acc = w @ x + b
            relu = i < 2
            q = _shift_round(np.maximum(acc, 0) if relu else acc, s)
            x = np.clip(q, 0 if relu else -32768, 32767)
        return x

    def predict(self, cnn28, top=3):
        lg = self.logits(cnn28)
        order = sorted(range(len(lg)), key=lambda i: (-lg[i], i))      # 동점이면 낮은 class (argmax.v 와 같음)
        return [dict(cls=int(i), letter=self.names[i], logit=int(lg[i])) for i in order[:top]]
