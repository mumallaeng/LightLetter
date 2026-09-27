"""Integer FC model shared by the cnn_top vector generators.

read_fc_txt(k) parses cnn/golden_model/vectors/fcK.txt (layout in export_fc_vectors.py)
and fc(p, x) evaluates one layer exactly as the C golden model / RTL do: INT32 bias plus
the integer products, then round-half-to-even shift by scale_exp, ReLU when the layer
has one, clamp to INT16. read_mem() reads a $readmemh file into a list of ints.
"""
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
FC_TXT = ROOT / "cnn/golden_model/vectors"


def read_mem(path):
    return [int(t, 16) for t in Path(path).read_text().split()]


def read_fc_txt(k, path=None):
    tok = (Path(path) if path else FC_TXT / f"fc{k}.txt").read_text().split()
    layer, n_in, n_out, lanes, num_chunk, scale_exp, relu = (int(t) for t in tok[:7])
    pos = 7
    bias = [int(t) for t in tok[pos:pos + n_out]]; pos += n_out
    x = [int(t) for t in tok[pos:pos + n_in]]; pos += n_in
    rows = [int(t) for t in tok[pos:pos + num_chunk * n_out * lanes]]; pos += num_chunk * n_out * lanes
    expected = [int(t) for t in tok[pos:pos + n_out]]
    # rows are chunk-major: row g*n_out+n holds w[n][g*lanes .. g*lanes+lanes-1]
    w = [[0] * n_in for _ in range(n_out)]
    for g in range(num_chunk):
        for n in range(n_out):
            base = (g * n_out + n) * lanes
            for i in range(lanes):
                col = g * lanes + i
                if col < n_in:
                    w[n][col] = rows[base + i]
    return dict(layer=layer, n_in=n_in, n_out=n_out, scale_exp=scale_exp, relu=relu,
                bias=bias, w=w, x=x, expected=expected)


def quant(acc, s, relu):
    if relu and acc < 0:
        acc = 0
    one = 1 << s
    q, rem = acc >> s, acc & (one - 1)           # arithmetic shift = floor
    half = one >> 1
    if rem > half or (rem == half and (q & 1)):
        q += 1
    lo = 0 if relu else -32768
    return max(lo, min(32767, q))


def fc(p, x):
    assert len(x) == p["n_in"], (len(x), p["n_in"])
    out = []
    for n in range(p["n_out"]):
        acc = p["bias"][n] + sum(int(a) * int(b) for a, b in zip(x, p["w"][n]))
        out.append(quant(acc, p["scale_exp"], p["relu"]))
    return out
