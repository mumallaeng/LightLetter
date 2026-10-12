"""Export Fully Connected test vectors and ROM files from the Python golden model dump.

Reads tx/cnn/model/cnn_golden/results/layer_outputs/lenet5_3x3_schedule.json and writes, per layer,
integer stimulus / expected values for test_fc.c plus the weight/bias ROM contents:

The project is uppercase-only (26 classes, 0='A'). A 26-class dump is used as it is. An older
36-class (digits + uppercase) dump keeps only rows 10..35 of the FC3 weight, bias and logits. The weight/output scales are taken from the full 36-row tensors first, because that
is what the trained observers saw - the letter logits stay bit-identical to the trained model.

    python export_fc_vectors.py [dump.json] [out_dir] [mem_dir]

Vector file layout (whitespace separated integers):
    layer n_in n_out l num_chunk scale_exp relu
    bias[0..n_out-1]
    n_in input codes in arrival order (channel-major: c*25 + y*5 + x for FC1)
    num_chunk * n_out rows of l weights: row g*n_out+n holds w[n, g*l .. g*l+l-1] (0 past n_in)
    n_out expected codes in neuron order

ROM files (.mem, $readmemh) for the shared engine: fc_weight.mem, one row of P weights per
(layer, group, input), lane 0 in the low 16 bits; fc_bias.mem, one row of P INT32 biases per
(layer, group). Row bases per layer are printed and must match fc_common.h FC_CFG.
"""
import json
import math
import sys
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
DUMP = HERE.parent / "model/cnn_golden/results/layer_outputs/lenet5_3x3_schedule.json"

# fc<K>.txt keeps the step 1 chunk layout (test/vector readers parse it); the RTL ROM is the shared P-lane one
FC_LANES = {1: 25, 2: 10, 3: 5}
P = 20  # shared engine lanes (fc_common.h FC_P)
ACC_W = 40  # fc_top ACC_W: accumulator width
# 36-class dump rows that are the uppercase letters A..Z
UPPERCASE = slice(10, 36)


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


def export(layer, x_int, in_exp, w, bias, y_q, out_dir):
    """x_int: [n_in] ints, w: [n_out, n_in] floats, y_q: [n_out] quantized floats.
    Writes fc<layer>.txt (the step 1 layout, still read by fc_vec_io.c) and returns the
    integer weights/biases/scales the shared ROM writer needs."""
    lanes = FC_LANES[layer]
    relu = 1 if layer < 3 else 0

    # scales from the tensors as trained (36 rows for FC3 in the old dump), then keep the uppercase rows
    w_exp = weight_scale_exp(w)
    out_exp = pow2_scale_of(y_q)
    if layer == 3 and w.shape[0] == 36:
        w, bias, y_q = w[UPPERCASE], bias[UPPERCASE], y_q[UPPERCASE]
    n_out, n_in = w.shape
    num_chunk = math.ceil(n_in / lanes)

    w_int = np.rint(w / 2.0 ** w_exp).astype(np.int64)
    acc_exp = in_exp + w_exp
    bias_int = np.rint(bias / 2.0 ** acc_exp).astype(np.int64)
    scale_exp = out_exp - acc_exp
    assert 0 <= scale_exp <= 31, scale_exp

    rows = []
    for g in range(num_chunk):
        for n in range(n_out):
            rows.append([int(w_int[n, g * lanes + i]) if g * lanes + i < n_in else 0 for i in range(lanes)])
    expected = np.rint(np.asarray(y_q, dtype=np.float64) / 2.0 ** out_exp).astype(np.int64)

    with open(out_dir / f"fc{layer}.txt", "w") as f:
        f.write(f"{layer} {n_in} {n_out} {lanes} {num_chunk} {scale_exp} {relu}\n")
        f.write(" ".join(str(int(b)) for b in bias_int) + "\n")
        f.write(" ".join(str(int(v)) for v in x_int) + "\n")
        f.writelines(" ".join(str(v) for v in row) + "\n" for row in rows)
        f.writelines(f"{int(e)}\n" for e in expected)

    acc_peak = int(np.abs(np.asarray(x_int, dtype=np.int64) @ w_int.T).max())
    # every input at 32767 against |w|: no frame can push a neuron's sum past this
    acc_bound = int((np.abs(w_int).sum(axis=1) * 32767 + np.abs(bias_int)).max())
    bound_bits = acc_bound.bit_length() + 1
    print(f"fc{layer}: {n_in}->{n_out}, groups={math.ceil(n_out / P)}, w=2^{w_exp}, acc=2^{acc_exp}, "
          f"out=2^{out_exp}, scale_exp={scale_exp}, |acc| max={acc_peak} ({acc_peak.bit_length() + 1} bits signed), "
          f"worst case {bound_bits} bits")
    if bound_bits > ACC_W:
        sys.exit(f"fc{layer}: worst-case sum needs {bound_bits} bits, more than ACC_W = {ACC_W}")
    return out_exp, dict(n_in=n_in, n_out=n_out, w=w_int, bias=bias_int, scale_exp=scale_exp, relu=relu)


def write_shared_rom(layers, mem_dir):
    """fc_weight.mem: row = ROM_BASE[l] + g * n_in + i holds w[g*P + lane][i], lane 0 low.
    fc_bias.mem: row = BIAS_BASE[l] + g holds bias[g*P + lane], INT32 per lane."""
    w_rows, b_rows, rom_base, bias_base = [], [], [], []
    for L in layers:
        groups = math.ceil(L["n_out"] / P)
        rom_base.append(len(w_rows)); bias_base.append(len(b_rows))
        for g in range(groups):
            b_rows.append([int(L["bias"][g * P + lane]) if g * P + lane < L["n_out"] else 0 for lane in range(P)])
            for i in range(L["n_in"]):
                w_rows.append([int(L["w"][g * P + lane, i]) if g * P + lane < L["n_out"] else 0 for lane in range(P)])
    with open(mem_dir / "fc_weight.mem", "w") as f:
        f.writelines(hex_word(r, 16) + "\n" for r in w_rows)
    with open(mem_dir / "fc_bias.mem", "w") as f:
        f.writelines(hex_word(r, 32) + "\n" for r in b_rows)
    print(f"fc_weight.mem {len(w_rows)} rows x {P * 16} bit, fc_bias.mem {len(b_rows)} rows x {P * 32} bit; "
          f"ROM_BASE {rom_base}, BIAS_BASE {bias_base}, SCALE_EXP {[L['scale_exp'] for L in layers]}")


def main():
    dump = Path(sys.argv[1]) if len(sys.argv) > 1 else DUMP
    out_dir = Path(sys.argv[2]) if len(sys.argv) > 2 else HERE / "vectors"
    mem_dir = Path(sys.argv[3]) if len(sys.argv) > 3 else HERE.parent / "rtl/mem"
    out_dir.mkdir(parents=True, exist_ok=True)
    mem_dir.mkdir(parents=True, exist_ok=True)

    d = json.loads(dump.read_text())
    stage = {s["name"]: np.array(s["values"], dtype=np.float64) for s in d["stages"]}
    layer = {w["layer"]: w for w in d["weights"]}

    # FC1 input: flatten of conv2 pool output, already channel-major (c*25 + y*5 + x)
    x = stage["flatten"].ravel()
    in_exp = pow2_scale_of(x)
    x_int = np.rint(x / 2.0 ** in_exp).astype(np.int64)

    layers = []
    for i, name in ((1, "fc1"), (2, "fc2"), (3, "fc3")):
        y_q = stage[f"{name} (quantized+ReLU)" if i < 3 else "fc3 (quantized)"].ravel()
        out_exp, L = export(i, x_int, in_exp,
                            np.array(layer[name]["weight_raw"], dtype=np.float64),
                            np.array(layer[name]["bias"], dtype=np.float64),
                            y_q, out_dir)
        if i == 3 and y_q.shape[0] == 36:
            y_q = y_q[UPPERCASE]
        layers.append(L)
        x_int, in_exp = np.rint(y_q / 2.0 ** out_exp).astype(np.int64), out_exp
    write_shared_rom(layers, mem_dir)


if __name__ == "__main__":
    main()
