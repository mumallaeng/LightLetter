"""Export Output Buffer test vectors from the Python golden model dump.

Reads tb/cnn_golden/results/layer_outputs/lenet5_3x3_schedule.json (written by
cnn_golden.ipynb) and writes integer stimulus / expected values for
test_output_buffer.c:

    python export_ob_vectors.py [dump.json] [out_dir]

Vector file layout (whitespace separated integers):
    layer n c_out num_groups pack scale_exp
    bias[0..c_out-1]
    n_stim, then n_stim lines of "ch_result0 ch_result1 ch_result2" in arrival order
    n_exp,  then n_exp expected INT16 codes in output order (pixel -> channel)
"""
import json
import math
import sys
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
DUMP = HERE.parents[1] / "tb/cnn_golden/results/layer_outputs/lenet5_3x3_schedule.json"


def pow2_scale_of(values, tol=0.05):
    """Exponent of the coarsest power-of-two grid the values sit on.

    The dump comes from float32 math (the scale itself is computed with pow), so values
    are a hair off the ideal grid; accept a grid if every value is within `tol` of a step.
    """
    v = np.asarray(values, dtype=np.float64).ravel()
    v = v[v != 0]
    for exp in range(0, -40, -1):
        k = v * 2.0 ** -exp
        if np.abs(k - np.rint(k)).max() < tol:
            return exp
    raise ValueError("no power-of-two grid found")


def weight_scale_exp(w):
    return math.ceil(math.log2(np.abs(w).max() / 32767.0))


def export(layer, x_int, in_exp, w, bias, y_q, pack, out_path):
    """x_int: [C_IN,H,W] ints, w: [C_OUT,C_IN,3,3] floats, y_q: [C_OUT,H-2,W-2] quantized floats."""
    c_out, c_in = w.shape[:2]
    oh, ow = x_int.shape[1] - 2, x_int.shape[2] - 2
    n = oh * ow
    groups = math.ceil(c_in / 3)

    w_exp = weight_scale_exp(w)
    w_int = np.rint(w / 2.0 ** w_exp).astype(np.int64)
    acc_exp = in_exp + w_exp
    bias_int = np.rint(bias / 2.0 ** acc_exp).astype(np.int64)
    out_exp = pow2_scale_of(y_q)
    scale_exp = out_exp - acc_exp
    assert 0 <= scale_exp <= 31, scale_exp

    # per-input-channel 3x3 sums: tap[oc, ic, y, x]
    tap = np.zeros((c_out, c_in, oh, ow), dtype=np.int64)
    for ky in range(3):
        for kx in range(3):
            tap += w_int[:, :, ky, kx, None, None] * x_int[None, :, ky:ky + oh, kx:kx + ow]

    stim = []
    for g in range(groups):                      # arrival order: group -> pixel -> output channel
        for p in range(n):
            y, x = divmod(p, ow)
            for oc in range(c_out):
                row = [int(tap[oc, 3 * g + k, y, x]) if 3 * g + k < c_in else 0 for k in range(3)]
                stim.append(row)

    codes = np.rint(y_q / 2.0 ** out_exp).astype(np.int64)
    expected = [int(codes[oc, p // ow, p % ow]) for p in range(n) for oc in range(c_out)]

    with open(out_path, "w") as f:
        f.write(f"{layer} {n} {c_out} {groups} {pack} {scale_exp}\n")
        f.write(" ".join(str(int(b)) for b in bias_int) + "\n")
        f.write(f"{len(stim)}\n")
        f.writelines(f"{a} {b} {c}\n" for a, b, c in stim)
        f.write(f"{len(expected)}\n")
        f.writelines(f"{e}\n" for e in expected)

    peak = max(abs(v) for row in stim for v in row)
    print(f"{out_path.name}: layer {layer}, N={n}, C_OUT={c_out}, groups={groups}, "
          f"w=2^{w_exp}, acc=2^{acc_exp}, out=2^{out_exp}, scale_exp={scale_exp}, "
          f"|ch_result| max={peak} ({peak.bit_length() + 1} bits signed)")
    return out_exp


def main():
    dump = Path(sys.argv[1]) if len(sys.argv) > 1 else DUMP
    out_dir = Path(sys.argv[2]) if len(sys.argv) > 2 else HERE / "vectors"
    out_dir.mkdir(parents=True, exist_ok=True)

    d = json.loads(dump.read_text())
    stage = {s["name"]: np.array(s["values"], dtype=np.float64) for s in d["stages"]}  # [C,H,W]
    layer = {w["layer"]: w for w in d["weights"]}

    x1 = stage["input (quantized)"]
    in1_exp = pow2_scale_of(x1)
    x1_int = np.rint(x1 / 2.0 ** in1_exp).astype(np.int64)
    out1_exp = export(1, x1_int, in1_exp, np.array(layer["conv1"]["weight_raw"]),
                      np.array(layer["conv1"]["bias"]), stage["conv1 + ReLU (quantized)"],
                      3, out_dir / "ob_conv1.txt")

    x2 = stage["conv1 + MaxPool"]
    x2_int = np.rint(x2 / 2.0 ** out1_exp).astype(np.int64)
    export(2, x2_int, out1_exp, np.array(layer["conv2"]["weight_raw"]),
           np.array(layer["conv2"]["bias"]), stage["conv2 + ReLU (quantized)"],
           1, out_dir / "ob_conv2.txt")


if __name__ == "__main__":
    main()
