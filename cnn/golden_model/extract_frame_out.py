"""Pull one frame's final outputs out of a conv_l1 / conv_l2 / pool_l1 clock log CSV.

    python extract_frame_out.py logs/conv_l2_real_A.csv            (frame 0)
    python extract_frame_out.py logs/conv_l1_real_A.csv --frame 1
    python extract_frame_out.py logs/pool_l1_real_A.csv

The layer is taken from the file name (conv_l1_ / conv_l2_ / pool_l1_). Reads the rows where an output
was taken (out_frame == frame) and writes, next to the CSV:

    <name>_f<frame>_out.csv  one row per output value:
                             cycle, pos, y, x, och, code, out_ch_done, python, diff
    <name>_f<frame>_out.txt  the same values as channel maps (conv_l1: 6 x 26 x 26, conv_l2: 16 x 11 x 11,
                             pool_l1: 6 x 13 x 13) plus a summary

conv_l1 FIFO entries carry 3 values (och 0-2 / 3-5, out_data0..2); conv_l2 entries carry 1 (out_data);
pool_l1 outputs carry 3 values (pass 0: ch 0-2, pass 1: ch 3-5, pool_data0..2).
Python expected codes exist only for the real input (and ch_done_off, same input), frame 0:
conv_l1 / conv_l2 -> "convN + ReLU (quantized)", pool_l1 -> "conv1 + MaxPool" (end of vectors/conv_l1.txt).
For any other run the python / diff columns stay empty.
"""
import argparse
import csv
from pathlib import Path

HERE = Path(__file__).resolve().parent
LAYERS = {
    "conv_l1": dict(c_out=6, out_h=26, out_w=26, vectors=HERE / "vectors/conv_l1.txt"),
    "conv_l2": dict(c_out=16, out_h=11, out_w=11, vectors=HERE / "vectors/conv_l2.txt"),
    "pool_l1": dict(c_out=6, out_h=13, out_w=13, vectors=HERE / "vectors/conv_l1.txt", pool=True),
}


def load_python_codes(path, pool=False):
    """conv codes [c_out][h-2][w-2] (or, with pool, the MaxPool codes after them) as {(och, pos): code}"""
    t = [int(v) for v in path.read_text().split()]
    c_in, h, w, c_out = t[0:4]
    n = (h - 2) * (w - 2)
    start = 6 + c_in * h * w + c_out * c_in * 9 + c_out
    if pool:
        start += c_out * n
        n = ((h - 2) // 2) * ((w - 2) // 2)
    codes = t[start:start + c_out * n]
    return {(oc, p): codes[oc * n + p] for oc in range(c_out) for p in range(n)}


def output_pos(r, out_w):
    """output pixel index of one CSV row"""
    if "out_py" in r:
        return int(r["out_py"]) * out_w + int(r["out_px"])
    return int(r["out_pos"])


def output_values(r):
    """(och, code) pairs carried by one CSV row's output"""
    if "pool_data0" in r:
        base = int(r["out_pass"]) * 3
        return [(base + j, int(r[f"pool_data{j}"])) for j in range(3)]
    if "out_data0" in r:
        base = int(r["out_och"])
        return [(base + j, int(r[f"out_data{j}"])) for j in range(3)]
    return [(int(r["out_och"]), int(r["out_data"]))]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("csv", type=Path)
    ap.add_argument("--frame", type=int, default=0)
    ap.add_argument("--vectors", type=Path, help="Python expected codes (default: by layer)")
    a = ap.parse_args()

    layer = next((k for k in LAYERS if a.csv.name.startswith(k + "_")), None)
    if layer is None:
        raise SystemExit(f"{a.csv.name}: file name must start with conv_l1_, conv_l2_ or pool_l1_")
    L = LAYERS[layer]
    c_out, out_h, out_w = L["c_out"], L["out_h"], L["out_w"]
    vectors = a.vectors or L["vectors"]

    rows = []
    with open(a.csv, newline="") as f:
        for r in csv.DictReader(f):
            if r["out_frame"] != "" and int(r["out_frame"]) == a.frame:
                rows.append(r)

    py = None
    if ("_real_" in a.csv.name or "_ch_done_off_" in a.csv.name) and a.frame == 0 and vectors.exists():
        py = load_python_codes(vectors, L.get("pool", False))

    grid = [[["  ."] * out_w for _ in range(out_h)] for _ in range(c_out)]
    out_rows, exact, diff_cnt, done_cnt = [], 0, 0, 0
    for r in rows:
        pos = output_pos(r, out_w)
        y, x = divmod(pos, out_w)
        for och, code in output_values(r):
            grid[och][y][x] = code
            p = py[(och, pos)] if py else None
            d = code - p if py else None
            exact += d == 0
            diff_cnt += d is not None and d != 0
            done = r["out_ch_done"] if "out_ch_done" in r else r["pool_ch_done"]
            done_cnt += done == "1"
            out_rows.append([r["cycle"], pos, y, x, och, code, done,
                             "" if p is None else p, "" if d is None else d])

    stem = a.csv.with_suffix("")
    out_csv = Path(f"{stem}_f{a.frame}_out.csv")
    out_txt = Path(f"{stem}_f{a.frame}_out.txt")

    with open(out_csv, "w", newline="") as f:
        wr = csv.writer(f)
        wr.writerow(["cycle", "pos", "y", "x", "och", "code", "out_ch_done", "python", "diff"])
        wr.writerows(out_rows)

    cycles = [int(r["cycle"]) for r in rows]
    summary = [
        f"# {a.csv.name} frame {a.frame}: final outputs, {c_out} ch x {out_h} x {out_w}",
        f"# values      : {len(out_rows)} / {c_out * out_h * out_w}",
        f"# cycles      : {min(cycles)} .. {max(cycles)}" if cycles else "# cycles      : -",
        f"# out_ch_done : {done_cnt} values (last pixel of each pass/frame)",
    ]
    if py:
        summary.append(f"# vs Python   : {exact} exact, {diff_cnt} different")
    else:
        summary.append("# vs Python   : no Python reference for this run / frame")

    lines = summary + [""]
    for oc in range(c_out):
        lines.append(f"och {oc:2d}")
        lines.append("      " + " ".join(f"x{x:<4d}" for x in range(out_w)))
        for y in range(out_h):
            lines.append(f"  y{y:<2d} " + " ".join(f"{v:>5}" for v in grid[oc][y]))
        lines.append("")
    out_txt.write_text("\n".join(lines), encoding="utf-8")

    print("\n".join(summary))
    print(f"-> {out_txt}\n-> {out_csv}")


if __name__ == "__main__":
    main()
