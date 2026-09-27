"""FC / argmax golden streams for tb_cnn_top.v (cnn_top: conv_l1 -> ... -> pool_l2 -> fc_top -> argmax).

rtl/cnn/rtl_ref/ already holds the golden streams up to pool_l2 (pool2_out.mem, FRAMES = 2). This script
continues from pool2_out.mem with the Fully Connected golden rule of cnn/golden_model (fc_layer.c):

    FCk[n] = quantize(bias[n] + sum_i x[i] * w[n][i], scale_exp, relu)
    quantize: (ReLU) -> round-half-to-even >> scale_exp -> clamp to [-32768, 32767]
    argmax  : index of the first maximum (a tie keeps the lower index, argmax.c / argmax.v)

weights / bias come from cnn/golden_model/vectors/fc{1,2,3}.txt (export_fc_vectors.py). The script also checks
that the ROM files the RTL reads (rtl/cnn/mem/fc{1,2,3}_{weight,bias}.mem) hold the same numbers.

    python gen_fc_golden.py            writes vectors/fc1_out.mem fc2_out.mem logit_out.mem class_out.mem

Output (one hex word per line, frame 0 then frame 1):
    fc1_out.mem    16 bit x 240   FC1 -> FC2 (fc_top l1_out_data), neuron order
    fc2_out.mem    16 bit x 168   FC2 -> FC3 (fc_top l2_out_data)
    logit_out.mem  16 bit x  52   FC3 -> argmax (logit_data, two's complement), 26 classes = A..Z
    class_out.mem   8 bit x   2   argmax cnn_result per frame (0 = 'A')
"""
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
RTL_REF = ROOT / "rtl/cnn/rtl_ref"
RTL_MEM = ROOT / "rtl/cnn/mem"
FC_VEC = ROOT / "cnn/golden_model/vectors"
OUT = HERE / "vectors"
FRAMES, N_P2 = 2, 400


def read_fc_txt(k):
    t = list(map(int, (FC_VEC / f"fc{k}.txt").read_text().split()))
    lay, n_in, n_out, lanes, chunk, scale_exp, relu = t[:7]
    i = 7
    bias = t[i:i + n_out]; i += n_out
    i += n_in                                   # stimulus of the FC-only test, not used here
    rom = t[i:i + chunk * n_out * lanes]        # row g*n_out + n, lane l = w[n][g*lanes + l]
    return dict(n_in=n_in, n_out=n_out, lanes=lanes, chunk=chunk, s=scale_exp, relu=relu, bias=bias, rom=rom)


def quant(acc, s, relu):
    if relu and acc < 0:
        acc = 0
    one = 1 << s
    q, rem = acc // one, acc % one              # floor, rem in [0, one)
    if rem > one // 2 or (rem == one // 2 and q & 1):
        q += 1
    return max(-32768, min(32767, q))


def fc(p, x):
    L, n_out = p["lanes"], p["n_out"]
    return [quant(p["bias"][n] + sum(x[k] * p["rom"][((k // L) * n_out + n) * L + k % L] for k in range(p["n_in"])),
                  p["s"], p["relu"]) for n in range(n_out)]


def s16(v):
    return v - (1 << 16) if v >> 15 else v


def s32(v):
    return v - (1 << 32) if v >> 31 else v


def read_mem(path):
    return [int(w, 16) for w in path.read_text().split() if not w.startswith("//")]


def check_rom(k, p):
    """rtl/cnn/mem/fcK_weight.mem: one row per chunk/neuron, lane 0 in the low 16 bits."""
    rows = read_mem(RTL_MEM / f"fc{k}_weight.mem")
    rom = [s16((r >> (16 * l)) & 0xFFFF) for r in rows for l in range(p["lanes"])]
    bias = [s32(v) for v in read_mem(RTL_MEM / f"fc{k}_bias.mem")]
    ok_w, ok_b = rom == p["rom"], bias == p["bias"]
    print(f"  rtl/cnn/mem/fc{k}_weight.mem {'==' if ok_w else '!='} golden, fc{k}_bias.mem {'==' if ok_b else '!='} golden")
    return ok_w and ok_b


def main():
    P = [read_fc_txt(k) for k in (1, 2, 3)]
    rom_ok = all(check_rom(k, p) for k, p in zip((1, 2, 3), P))

    p2 = read_mem(RTL_REF / "pool2_out.mem")    # {ch_done, data[15:0]}
    assert len(p2) == FRAMES * N_P2, len(p2)
    out = {"fc1_out": [], "fc2_out": [], "logit_out": [], "class_out": []}
    for f in range(FRAMES):
        x = [w & 0xFFFF for w in p2[f * N_P2:(f + 1) * N_P2]]
        y1 = fc(P[0], x); y2 = fc(P[1], y1); y3 = fc(P[2], y2)
        cls = max(range(len(y3)), key=lambda i: (y3[i], -i))
        out["fc1_out"] += y1; out["fc2_out"] += y2; out["logit_out"] += y3; out["class_out"].append(cls)
        print(f"  frame {f}: class {cls} ({chr(65 + cls)}), logit max {y3[cls]}, {len(y3)} logits")

    OUT.mkdir(exist_ok=True)
    for name, vals in out.items():
        width = 2 if name == "class_out" else 4
        (OUT / f"{name}.mem").write_text("".join(f"{v & ((1 << (4 * width)) - 1):0{width}x}\n" for v in vals))
        print(f"  wrote vectors/{name}.mem ({len(vals)} lines)")
    return 0 if rom_ok else 1


if __name__ == "__main__":
    sys.exit(main())
