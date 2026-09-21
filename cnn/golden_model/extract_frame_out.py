"""Pull one frame's final outputs out of a conv_l2 clock log CSV.

    python extract_frame_out.py logs/conv_l2_real_A.csv            (frame 0)
    python extract_frame_out.py logs/conv_l2_real_A.csv --frame 1

Reads the rows where the FIFO output was taken (out_frame == frame) and writes, next to the CSV:

    <name>_f<frame>_out.csv  one row per output value:
                             cycle, pos, y, x, och, code, out_ch_done, python, diff
    <name>_f<frame>_out.txt  the same values as 16 channel maps of 11 x 11, plus a summary

Python expected codes (vectors/conv_l2.txt) exist only for the real input, frame 0; for any other
run the python / diff columns stay empty.
"""
import argparse
import csv
from pathlib import Path

HERE = Path(__file__).resolve().parent
C_OUT, OUT_H, OUT_W = 16, 11, 11
N = OUT_H * OUT_W


def load_python_codes(path):
    """conv codes [16][11][11] from vectors/conv_l2.txt, as {(och, pos): code}"""
    t = [int(v) for v in path.read_text().split()]
    c_in, h, w, c_out = t[0:4]
    start = 6 + c_in * h * w + c_out * c_in * 9 + c_out
    codes = t[start:start + c_out * N]
    return {(oc, p): codes[oc * N + p] for oc in range(c_out) for p in range(N)}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("csv", type=Path)
    ap.add_argument("--frame", type=int, default=0)
    ap.add_argument("--vectors", type=Path, default=HERE / "vectors/conv_l2.txt")
    a = ap.parse_args()

    rows = []
    with open(a.csv, newline="") as f:
        for r in csv.DictReader(f):
            if r["out_frame"] != "" and int(r["out_frame"]) == a.frame:
                rows.append(r)

    py = None
    if "_real_" in a.csv.name and a.frame == 0 and a.vectors.exists():
        py = load_python_codes(a.vectors)

    grid = [[["  ."] * OUT_W for _ in range(OUT_H)] for _ in range(C_OUT)]
    out_rows, exact, diff_cnt = [], 0, 0
    for r in rows:
        pos, och, code = int(r["out_pos"]), int(r["out_och"]), int(r["out_data"])
        y, x = divmod(pos, OUT_W)
        grid[och][y][x] = code
        p = py[(och, pos)] if py else None
        d = code - p if py else None
        exact += d == 0
        diff_cnt += d is not None and d != 0
        out_rows.append([r["cycle"], pos, y, x, och, code, r["out_ch_done"],
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
        f"# {a.csv.name} frame {a.frame}: final outputs (FIFO out_data), 16 ch x 11 x 11",
        f"# values      : {len(rows)} / {C_OUT * N}",
        f"# cycles      : {min(cycles)} .. {max(cycles)}" if cycles else "# cycles      : -",
        f"# out_ch_done : {sum(r['out_ch_done'] == '1' for r in rows)} values (last pixel x 16 och)",
    ]
    if py:
        summary.append(f"# vs Python   : {exact} exact, {diff_cnt} different")
    else:
        summary.append("# vs Python   : no Python reference for this run / frame")

    lines = summary + [""]
    for oc in range(C_OUT):
        lines.append(f"och {oc:2d}")
        lines.append("      " + " ".join(f"x{x:<4d}" for x in range(OUT_W)))
        for y in range(OUT_H):
            lines.append(f"  y{y:<2d} " + " ".join(f"{v:>5}" for v in grid[oc][y]))
        lines.append("")
    out_txt.write_text("\n".join(lines), encoding="utf-8")

    print("\n".join(summary))
    print(f"-> {out_txt}\n-> {out_csv}")


if __name__ == "__main__":
    main()
