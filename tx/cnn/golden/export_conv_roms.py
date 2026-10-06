"""Export the conv weight/bias ROM files from the Python golden model dump.

Reads tx/cnn/model/cnn_golden/results/layer_outputs/lenet5_3x3_schedule.json and writes

    tx/cnn/rtl/rtl_ref/conv1_weight.mem     12 rows  [och][grp], 432 bit  (reference copy of weight_rom_l1)
    tx/cnn/rtl/rtl_ref/conv2_weight.mem     32 rows  [och][grp], 432 bit  (reference copy of weight_rom_l2)
    tx/cnn/rtl/rtl_ref/conv1_bias_ce.mem     6 rows  INT32 at the conv1 accumulator scale
    tx/cnn/rtl/rtl_ref/conv2_bias_ce.mem    16 rows  INT32 at the conv2 accumulator scale
    tx/cnn/rtl/mem/l2_weight_ic{0,1,2}.mem  32 rows  [och][grp], 144 bit = lane 0/1/2 = weight_rom_l2 BRAM bank (addr {och, grp})
    tx/cnn/rtl/mem/l2_weight_chNN.mem        2 rows  (grp 0, grp 1) per output channel (old per-channel ROM layout)
    tx/cnn/rtl/rtl_ref/conv1_weight_rom_l1.txt   the case constants weight_rom_l1.v must carry (it has no .mem)

Row layout (tx/cnn/rtl/rtl_ref/README.md): lane l = in_ch grp*3+l in bits [144*l +: 144],
tap k = ky*3+kx in bits [16*k +: 16] of its lane, lane 0 / tap 0 at the LSB. conv1 has one
input channel, so only grp 0 lane 0 carries values. Prints the SCALE_EXP each conv layer's
quantizer needs (conv_l1.v / conv_l2.v parameters).

    python export_conv_roms.py [dump.json] [ref_dir] [mem_dir]
"""
import json
import math
import sys
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
DUMP = HERE.parents[1] / "model/cnn_golden/results/layer_outputs/lenet5_3x3_schedule.json"
REF = HERE.parents[1] / "rtl/rtl_ref"
MEM = HERE.parents[1] / "rtl/mem"


def pow2_scale_of(values, tol=0.05):
    v = np.asarray(values, dtype=np.float64).ravel()
    v = v[v != 0]
    for exp in range(0, -40, -1):
        k = v * 2.0 ** -exp
        if np.abs(k - np.rint(k)).max() < tol:
            return exp
    raise ValueError("no power-of-two grid found")


def weight_scale_exp(w):
    return math.ceil(math.log2(np.abs(w).max() / 32767.0))


def row_hex(w_int, och, grp):
    word = 0
    c_in = w_int.shape[1]
    for lane in range(3):
        ci = grp * 3 + lane
        if ci >= c_in:
            continue
        for k in range(9):
            word |= (int(w_int[och, ci, k // 3, k % 3]) & 0xFFFF) << (144 * lane + 16 * k)
    return f"{word:0108x}"


def main():
    dump = Path(sys.argv[1]) if len(sys.argv) > 1 else DUMP
    ref_dir = Path(sys.argv[2]) if len(sys.argv) > 2 else REF
    mem_dir = Path(sys.argv[3]) if len(sys.argv) > 3 else MEM
    ref_dir.mkdir(parents=True, exist_ok=True)
    mem_dir.mkdir(parents=True, exist_ok=True)
    d = json.loads(dump.read_text())
    stage = {s["name"]: np.array(s["values"], dtype=np.float64) for s in d["stages"]}
    layer = {w["layer"]: w for w in d["weights"]}

    in_exp = pow2_scale_of(stage["input (quantized)"])
    for k, name in ((1, "conv1"), (2, "conv2")):
        w = np.array(layer[name]["weight_raw"], dtype=np.float64)
        b = np.array(layer[name]["bias"], dtype=np.float64)
        w_exp = weight_scale_exp(w)
        w_int = np.rint(w / 2.0 ** w_exp).astype(np.int64)
        acc_exp = in_exp + w_exp
        b_int = np.rint(b / 2.0 ** acc_exp).astype(np.int64)
        out_exp = pow2_scale_of(stage[f"{name} + ReLU (quantized)"])
        c_out = w.shape[0]
        rows = [row_hex(w_int, och, grp) for och in range(c_out) for grp in range(2)]
        (ref_dir / f"{name}_weight.mem").write_text("".join(r + "\n" for r in rows))
        (ref_dir / f"{name}_bias_ce.mem").write_text("".join(f"{int(v) & 0xFFFFFFFF:08x}\n" for v in b_int))
        if k == 1:
            lines = [f"// weight_rom_l1.v case constants for {dump.name} (144 bit = 9 taps x INT16, tap 0 at the LSB)",
                     f"// conv1: input 2^{in_exp}, weight 2^{w_exp}, accumulator 2^{acc_exp}, output 2^{out_exp} -> SCALE_EXP {out_exp - acc_exp}"]
            lines += [f"{och}: weight_out = 144'h{rows[2 * och][-36:]};" for och in range(c_out)]
            (ref_dir / "conv1_weight_rom_l1.txt").write_text("\n".join(lines) + "\n")
        else:
            for lane in range(3):  # 432 bit row = lane2 | lane1 | lane0 (hex, MSB first)
                (mem_dir / f"l2_weight_ic{lane}.mem").write_text(
                    "".join(r[len(r) - 36 * (lane + 1):len(r) - 36 * lane] + "\n" for r in rows))
            for och in range(c_out):
                (mem_dir / f"l2_weight_ch{och:02d}.mem").write_text(rows[2 * och] + "\n" + rows[2 * och + 1] + "\n")
        print(f"{name}: in 2^{in_exp} w 2^{w_exp} acc 2^{acc_exp} out 2^{out_exp} -> SCALE_EXP {out_exp - acc_exp}"
              f" | {len(rows)} rows, |w| max {int(np.abs(w_int).max())}, |b| max {int(np.abs(b_int).max())}")
        in_exp = out_exp


if __name__ == "__main__":
    main()
