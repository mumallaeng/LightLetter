"""Export Fully Connected test vectors and ROM files from the Python golden model dump.

Reads tb/cnn_golden/results/layer_outputs/lenet5_3x3_schedule.json and writes, per layer,
integer stimulus / expected values for test_fc.c plus the weight/bias ROM contents:

    python export_fc_vectors.py [dump.json] [out_dir] [mem_dir]

Vector file layout (whitespace separated integers):
    layer n_in n_out l num_chunk scale_exp relu
    bias[0..n_out-1]
    n_in input codes in arrival order (channel-major: c*25 + y*5 + x for FC1)
    num_chunk * n_out rows of l weights: row g*n_out+n holds w[n, g*l .. g*l+l-1] (0 past n_in)
    n_out expected codes in neuron order

ROM files (.mem, $readmemh): fcK_weight.mem one row per chunk/neuron, lane 0 in the low
16 bits; fcK_bias.mem one INT32 per neuron.
"""
import json
import math
import sys
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
DUMP = HERE.parents[1] / "tb/cnn_golden/results/layer_outputs/lenet5_3x3_schedule.json"

# per-layer multiplier count: FC1 400->120, FC2 120->84, FC3 84->36 (sum = 40 DSP)
FC_LANES = {1: 25, 2: 10, 3: 5}


def pow2_scale_of(values, tol=0.05):
    """Exponent of the coarsest power-of-two grid the values sit on (dump is float32)."""
    v = np.asarray(values, dtype=np.float64).ravel()
    v = v[v != 0]
    for exp in range(0, -40, -1):
        k = v * 2.0 ** -exp
        if np.abs(k - np.rint(k)).max() < tol:
            return exp
    raise ValueError("no power-of-two grid found")


def weight_scale_exp(w):
    return math.ceil(math.log2(np.abs(w).max() / 32767.0))


def hex_word(vals, bits=16):
    """Pack vals into one hex word, vals[0] in the low bits (two's complement)."""
    word = 0
    for i, v in enumerate(vals):
        word |= (int(v) & ((1 << bits) - 1)) << (i * bits)
    return f"{word:0{(len(vals) * bits + 3) // 4}x}"


def export(layer, x_int, in_exp, w, bias, y_q, out_dir, mem_dir):
    """x_int: [n_in] ints, w: [n_out, n_in] floats, y_q: [n_out] quantized floats."""
    n_out, n_in = w.shape
    lanes = FC_LANES[layer]
    num_chunk = math.ceil(n_in / lanes)
    relu = 1 if layer < 3 else 0

    w_exp = weight_scale_exp(w)
    w_int = np.rint(w / 2.0 ** w_exp).astype(np.int64)
    acc_exp = in_exp + w_exp
    bias_int = np.rint(bias / 2.0 ** acc_exp).astype(np.int64)
    out_exp = pow2_scale_of(y_q)
    scale_exp = out_exp - acc_exp
    assert 0 <= scale_exp <= 31, scale_exp

    # chunk-major weight rows: row g*n_out + n, lanes past n_in are zero padded
    rows = []
    for g in range(num_chunk):
        for n in range(n_out):
            rows.append([int(w_int[n, g * lanes + i]) if g * lanes + i < n_in else 0
                         for i in range(lanes)])

    expected = np.rint(np.asarray(y_q, dtype=np.float64) / 2.0 ** out_exp).astype(np.int64)

    with open(out_dir / f"fc{layer}.txt", "w") as f:
        f.write(f"{layer} {n_in} {n_out} {lanes} {num_chunk} {scale_exp} {relu}\n")
        f.write(" ".join(str(int(b)) for b in bias_int) + "\n")
        f.write(" ".join(str(int(v)) for v in x_int) + "\n")
        f.writelines(" ".join(str(v) for v in row) + "\n" for row in rows)
        f.writelines(f"{int(e)}\n" for e in expected)

    with open(mem_dir / f"fc{layer}_weight.mem", "w") as f:
        f.writelines(hex_word(row) + "\n" for row in rows)
    with open(mem_dir / f"fc{layer}_bias.mem", "w") as f:
        f.writelines(f"{int(b) & 0xFFFFFFFF:08x}\n" for b in bias_int)

    acc_peak = int(np.abs(np.asarray(x_int, dtype=np.int64) @ w_int.T).max())
    print(f"fc{layer}: {n_in}->{n_out}, L={lanes}, chunks={num_chunk}, "
          f"w=2^{w_exp}, acc=2^{acc_exp}, out=2^{out_exp}, scale_exp={scale_exp}, "
          f"rom {len(rows)}x{lanes * 16}bit, |acc| max={acc_peak} "
          f"({acc_peak.bit_length() + 1} bits signed)")
    return out_exp


def main():
    dump = Path(sys.argv[1]) if len(sys.argv) > 1 else DUMP
    out_dir = Path(sys.argv[2]) if len(sys.argv) > 2 else HERE / "vectors"
    mem_dir = Path(sys.argv[3]) if len(sys.argv) > 3 else HERE.parents[1] / "rtl/cnn/mem"
    out_dir.mkdir(parents=True, exist_ok=True)
    mem_dir.mkdir(parents=True, exist_ok=True)

    d = json.loads(dump.read_text())
    stage = {s["name"]: np.array(s["values"], dtype=np.float64) for s in d["stages"]}
    layer = {w["layer"]: w for w in d["weights"]}

    # FC1 input: flatten of conv2 pool output, already channel-major (c*25 + y*5 + x)
    x = stage["flatten"].ravel()
    in_exp = pow2_scale_of(x)
    x_int = np.rint(x / 2.0 ** in_exp).astype(np.int64)

    for i, name in ((1, "fc1"), (2, "fc2"), (3, "fc3")):
        y_q = stage[f"{name} (quantized+ReLU)" if i < 3 else "fc3 (quantized)"].ravel()
        out_exp = export(i, x_int, in_exp,
                         np.array(layer[name]["weight_raw"], dtype=np.float64),
                         np.array(layer[name]["bias"], dtype=np.float64),
                         y_q, out_dir, mem_dir)
        x_int, in_exp = np.rint(y_q / 2.0 ** out_exp).astype(np.int64), out_exp


if __name__ == "__main__":
    main()
