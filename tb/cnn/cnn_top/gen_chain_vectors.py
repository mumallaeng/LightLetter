"""Reference streams for the CNN chain testbenches, from the ROM/vector files the RTL reads.

Writes rtl/cnn/rtl_ref/{ce1_stim,ce1_out,pool1_out,ce2_out,pool2_out}.mem, ce_params.txt
and tb/cnn/cnn_top/vectors/{fc1_out,fc2_out,logit_out,class_out}.mem for FRAMES = 2:
frame 0 is the image, frame 1 is its left-right mirror (rtl/cnn/rtl_ref/README.md).

Inputs: conv weights/biases from rtl_ref/conv{1,2}_weight.mem + conv{1,2}_bias_ce.mem,
FC layers from cnn/golden_model/vectors/fc{1,2,3}.txt, the image from the golden dump's
"input (quantized)" stage (pixel_in = value * 2^14). Conv SCALE_EXP per layer are
arguments because they are RTL parameters (conv_l1.v / conv_l2.v).

    python gen_chain_vectors.py [dump.json] [--conv1-scale 15] [--conv2-scale 16]
                                [--ref-dir rtl_ref] [--out-dir vectors] [--check]
--check: compute only and compare with the files already on disk (generator self test).
"""
import argparse
import json
import sys
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(HERE))
from gen_fc_golden import fc, read_fc_txt, read_mem  # noqa: E402

DUMP = ROOT / "tb/cnn_golden/results/layer_outputs/lenet5_3x3_schedule.json"
REF = ROOT / "rtl/cnn/rtl_ref"
OUT = HERE / "vectors"


def s_n(v, n):
    return v - (1 << n) if v >> (n - 1) else v


def conv_params(ref, k, c_in, c_out):
    rows = read_mem(ref / f"conv{k}_weight.mem")
    w = np.zeros((c_out, c_in, 3, 3), dtype=np.int64)
    for o in range(c_out):
        for g in range(2):
            r = rows[o * 2 + g]
            for lane in range(3):
                ci = g * 3 + lane
                if ci >= c_in:
                    continue
                for t in range(9):
                    w[o, ci, t // 3, t % 3] = s_n((r >> (144 * lane + 16 * t)) & 0xFFFF, 16)
    b = np.array([s_n(v, 32) for v in read_mem(ref / f"conv{k}_bias_ce.mem")], dtype=np.int64)
    return w, b


def quant_arr(acc, s):
    """ReLU -> round-half-to-even >> s -> clamp int16 (relu_quant / quantizer.v)."""
    acc = np.where(acc < 0, 0, acc)
    one = 1 << s
    q, rem = acc // one, acc % one
    q = q + ((rem > one // 2) | ((rem == one // 2) & (q & 1 == 1)))
    return np.clip(q, 0, 32767)


def conv(x, w, b, s):
    c_in, h, _ = x.shape
    o = h - 2
    acc = np.zeros((w.shape[0], o, o), dtype=np.int64) + b[:, None, None]
    for ci in range(c_in):
        for ky in range(3):
            for kx in range(3):
                acc += w[:, ci, ky, kx][:, None, None] * x[ci, ky:ky + o, kx:kx + o][None]
    return quant_arr(acc, s)


def pool(x):
    c, h, _ = x.shape
    o = h // 2
    return x[:, :2 * o, :2 * o].reshape(c, o, 2, o, 2).max(axis=(2, 4))


def pack3(a):
    """{ch_done, d2, d1, d0} per pixel, pass-major (channels 0-2 then 3-5); ch_done on the pass's last pixel."""
    c, h, w_ = a.shape
    out = []
    for p in range(c // 3):
        flat = [a[p * 3 + l].reshape(-1) for l in range(3)]
        for i in range(h * w_):
            word = int(flat[0][i]) | (int(flat[1][i]) << 16) | (int(flat[2][i]) << 32)
            if i == h * w_ - 1:
                word |= 1 << 48
            out.append(word)
    return out


def pack1(a):
    """{ch_done, data} per pixel, channel-major; ch_done on each channel's last pixel."""
    c, h, w_ = a.shape
    out = []
    for ch in range(c):
        flat = a[ch].reshape(-1)
        for i in range(h * w_):
            word = int(flat[i]) | ((1 << 16) if i == h * w_ - 1 else 0)
            out.append(word)
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("dump", nargs="?", type=Path, default=DUMP)
    ap.add_argument("--conv1-scale", type=int, default=16)
    ap.add_argument("--conv2-scale", type=int, default=16)
    ap.add_argument("--ref-dir", type=Path, default=REF)
    ap.add_argument("--out-dir", type=Path, default=OUT)
    ap.add_argument("--fc-dir", type=Path, default=None, help="directory with fc{1,2,3}.txt (default cnn/golden_model/vectors)")
    ap.add_argument("--stim", type=Path, default=None, help="use this ce1_stim.mem instead of the dump image")
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args()

    if a.stim:
        stim = [s_n(v, 16) for v in read_mem(a.stim)]
        frames = [np.array(stim[f * 784:(f + 1) * 784], dtype=np.int64).reshape(28, 28) for f in range(2)]
    else:
        d = json.loads(a.dump.read_text())
        inp = np.array([s["values"] for s in d["stages"] if s["name"] == "input (quantized)"][0], dtype=np.float64)[0]
        img = np.rint(inp * 16384).astype(np.int64)          # pixel_in = round(p/255 * 2^14)
        frames = [img, img[:, ::-1].copy()]
    w1, b1 = conv_params(a.ref_dir, 1, 1, 6)
    w2, b2 = conv_params(a.ref_dir, 2, 6, 16)
    fcs = [read_fc_txt(k, (a.fc_dir / f"fc{k}.txt") if a.fc_dir else None) for k in (1, 2, 3)]

    streams = dict(ce1_stim=[], ce1_out=[], pool1_out=[], ce2_out=[], pool2_out=[])
    fc1_out, fc2_out, logit_out, class_out = [], [], [], []
    for img in frames:
        c1 = conv(img[None], w1, b1, a.conv1_scale)
        p1 = pool(c1)
        c2 = conv(p1, w2, b2, a.conv2_scale)
        p2 = pool(c2)
        streams["ce1_stim"] += [int(v) & 0xFFFF for v in img.reshape(-1)]
        streams["ce1_out"] += pack3(c1)
        streams["pool1_out"] += pack3(p1)
        streams["ce2_out"] += pack1(c2)
        streams["pool2_out"] += pack1(p2)
        x = [int(v) for v in p2.reshape(-1)]                # channel-major 5x5 = pool_l2 stream order
        y1 = fc(fcs[0], x); y2 = fc(fcs[1], y1); lg = fc(fcs[2], y2)
        fc1_out += y1; fc2_out += y2; logit_out += lg
        class_out.append(max(range(len(lg)), key=lambda i: (lg[i], -i)))

    files = {
        a.ref_dir / "ce1_stim.mem": (streams["ce1_stim"], 4), a.ref_dir / "ce1_out.mem": (streams["ce1_out"], 13),
        a.ref_dir / "pool1_out.mem": (streams["pool1_out"], 13), a.ref_dir / "ce2_out.mem": (streams["ce2_out"], 5),
        a.ref_dir / "pool2_out.mem": (streams["pool2_out"], 5),
        a.out_dir / "fc1_out.mem": ([v & 0xFFFF for v in fc1_out], 4), a.out_dir / "fc2_out.mem": ([v & 0xFFFF for v in fc2_out], 4),
        a.out_dir / "logit_out.mem": ([v & 0xFFFF for v in logit_out], 4), a.out_dir / "class_out.mem": (class_out, 2),
    }
    params = (f"FRAMES=2\nCONV_L1 IN=28x28x1 OUT=26x26x6 PACK=3 SCALE_EXP={a.conv1_scale} NSTIM=1568 NOUT=2704\n"
              "POOL_L1 IN=26x26x6 OUT=13x13x6 LANES=3 NOUT=676\n"
              f"CONV_L2 IN=13x13x6 OUT=11x11x16 PACK=1 SCALE_EXP={a.conv2_scale} NOUT=3872\n"
              "POOL_L2 IN=11x11x16 OUT=5x5x16 LANES=1 NOUT=800\n")
    ok = True
    for path, (vals, width) in files.items():
        text = "".join(f"{v:0{width}x}\n" for v in vals)
        if a.check:
            same = path.exists() and path.read_text() == text
            ok &= same
            print(f"  {'same' if same else 'DIFF'}  {path.relative_to(ROOT)}  ({len(vals)} lines)")
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text)
            print(f"  wrote {path.relative_to(ROOT)}  ({len(vals)} lines)")
    if not a.check:
        old = (a.ref_dir / "ce_params.txt").read_text() if (a.ref_dir / "ce_params.txt").exists() else ""
        tail = "".join(l + "\n" for l in old.splitlines() if l.startswith("CHAIN_CYCLES"))
        (a.ref_dir / "ce_params.txt").write_text(params + tail)
        print("  wrote rtl/cnn/rtl_ref/ce_params.txt")
    print("classes:", class_out, "| conv1 SCALE_EXP", a.conv1_scale, "| conv2 SCALE_EXP", a.conv2_scale)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
