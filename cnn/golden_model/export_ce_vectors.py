"""Export end-to-end CE (conv1 -> MaxPool -> conv2) test vectors from the Python golden model dump.

Reads cnn/algorithm_test/model_parameter/lenet5_3x3_schedule.json and writes the integer
input / weight / bias / expected codes for test_ce.c:

    python export_ce_vectors.py [dump.json] [out_file]

Quantization is the same as export_ob_vectors.py (the Output Buffer vectors), so both
tests see the same integer model.

Vector file layout (whitespace separated integers), one block per layer (conv1, conv2):
    c_in h w c_out pack scale_exp
    input codes   [c_in][h][w]
    weights       [c_out][c_in][3][3]
    bias          [c_out]
    conv codes    [c_out][h-2][w-2]            (conv + ReLU, quantized)
    pool codes    [c_out][(h-2)/2][(w-2)/2]    (2x2 MaxPool of the conv codes)
"""
import json
import sys
from pathlib import Path

import numpy as np

from export_ob_vectors import pow2_scale_of, weight_scale_exp

HERE = Path(__file__).resolve().parent
DUMP = HERE.parent / "algorithm_test/model_parameter/lenet5_3x3_schedule.json"


def to_codes(v, exp):
    return np.rint(np.asarray(v) / 2.0 ** exp).astype(np.int64)


def write_ints(f, a):
    a = np.asarray(a).ravel()
    for i in range(0, len(a), 16):
        f.write(" ".join(str(int(v)) for v in a[i:i + 16]) + "\n")


def export(f, x_int, in_exp, w, bias, y_q, y_pool, pack, name):
    c_out, c_in = w.shape[:2]
    _, h, wd = x_int.shape

    w_exp = weight_scale_exp(w)
    w_int = to_codes(w, w_exp)
    acc_exp = in_exp + w_exp
    bias_int = to_codes(bias, acc_exp)
    out_exp = pow2_scale_of(y_q)
    scale_exp = out_exp - acc_exp
    assert 0 <= scale_exp <= 31, scale_exp
    assert x_int.min() >= -32768 and x_int.max() <= 32767
    assert np.abs(w_int).max() <= 32767
    assert np.abs(bias_int).max() < 2 ** 31

    f.write(f"{c_in} {h} {wd} {c_out} {pack} {scale_exp}\n")
    write_ints(f, x_int)
    write_ints(f, w_int)
    write_ints(f, bias_int)
    write_ints(f, to_codes(y_q, out_exp))
    write_ints(f, to_codes(y_pool, out_exp))

    print(f"{name}: in {c_in}x{h}x{wd} (codes {x_int.min()}..{x_int.max()}), C_OUT={c_out}, "
          f"w=2^{w_exp}, acc=2^{acc_exp}, out=2^{out_exp}, scale_exp={scale_exp}, PACK={pack}")
    return out_exp


def main():
    dump = Path(sys.argv[1]) if len(sys.argv) > 1 else DUMP
    out = Path(sys.argv[2]) if len(sys.argv) > 2 else HERE / "vectors/ce_lenet5.txt"
    out.parent.mkdir(parents=True, exist_ok=True)

    d = json.loads(dump.read_text())
    stage = {s["name"]: np.array(s["values"], dtype=np.float64) for s in d["stages"]}  # [C,H,W]
    layer = {w["layer"]: w for w in d["weights"]}

    with open(out, "w", newline="\n") as f:
        x1 = stage["input (quantized)"]
        in1_exp = pow2_scale_of(x1)
        out1_exp = export(f, to_codes(x1, in1_exp), in1_exp,
                          np.array(layer["conv1"]["weight_raw"]), np.array(layer["conv1"]["bias"]),
                          stage["conv1 + ReLU (quantized)"], stage["conv1 + MaxPool"], 3, "conv1")

        x2 = stage["conv1 + MaxPool"]
        export(f, to_codes(x2, out1_exp), out1_exp,
               np.array(layer["conv2"]["weight_raw"]), np.array(layer["conv2"]["bias"]),
               stage["conv2 + ReLU (quantized)"], stage["conv2 + MaxPool"], 1, "conv2")

    print(f"wrote {out}")


if __name__ == "__main__":
    main()
