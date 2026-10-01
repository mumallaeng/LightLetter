#!/usr/bin/env python3
"""Write an A4 PDF with the six whiteboard markers, ready to print and cut.

    python3 gen_marker_sheet.py                       # 60 mm markers, ids 0..5
    python3 gen_marker_sheet.py --size 50 --out a.pdf

The marker codes are read from ../src/aruco_dict.h, the same table the
detector uses. The PDF is vector, so the edges stay sharp at any printer
resolution. Print at 100 % (no "fit to page") and check the 100 mm ruler.
"""
import argparse
import re
from pathlib import Path

HERE = Path(__file__).resolve().parent
MM = 72.0 / 25.4          # PDF points per millimetre
A4_W, A4_H = 210.0, 297.0
COLS, ROWS = 2, 3
LABEL_H = 8.0             # strip under each marker for the id and the arrow


def load_codes():
    text = (HERE / "../src/aruco_dict.h").read_text()
    body = text[text.index("ac_dict[AC_DICT_SIZE]"):]
    return [int(v, 16) for v in re.findall(r"0x([0-9A-Fa-f]{4})", body)]


def rect(x, y, w, h):
    """x, y = top-left corner in mm from the page's top-left."""
    return "%.3f %.3f %.3f %.3f re f\n" % (x * MM, (A4_H - y - h) * MM, w * MM, h * MM)


def line(x0, y0, x1, y1):
    return "%.3f %.3f m %.3f %.3f l S\n" % (x0 * MM, (A4_H - y0) * MM, x1 * MM, (A4_H - y1) * MM)


def text(x, y, size, s):
    return "BT /F1 %d Tf %.3f %.3f Td (%s) Tj ET\n" % (size, x * MM, (A4_H - y) * MM, s)


def marker(x, y, side, code):
    cell = side / 6.0
    out = "0 g\n" + rect(x, y, side, side) + "1 g\n"
    eps = 0.02  # mm of overlap between rows, hides renderer seams
    for r in range(4):
        c = 0
        while c < 4:
            if not (code >> (15 - (r * 4 + c))) & 1:
                c += 1
                continue
            run = c
            while run < 4 and (code >> (15 - (r * 4 + run))) & 1:
                run += 1
            below = r < 3 and any((code >> (15 - ((r + 1) * 4 + k))) & 1 for k in range(c, run))
            out += rect(x + (c + 1) * cell, y + (r + 1) * cell, (run - c) * cell, cell + (eps if below else 0))
            c = run
    return out + "0 g\n"


def build(size, quiet, ids, codes):
    tile_w = size + 2 * quiet
    tile_h = size + 2 * quiet + LABEL_H
    x0 = (A4_W - COLS * tile_w) / 2
    y0 = (A4_H - ROWS * tile_h) / 2 - 4
    if x0 < 5 or y0 < 5:
        raise SystemExit("markers do not fit on A4: reduce --size or --quiet")

    s = "1 g\n" + rect(0, 0, A4_W, A4_H) + "0 g 0.5 w\n"
    for i, mid in enumerate(ids):
        tx = x0 + (i % COLS) * tile_w
        ty = y0 + (i // COLS) * tile_h
        s += marker(tx + quiet, ty + quiet, size, codes[mid])
        s += "0.6 G [2 2] 0 d\n"
        s += "%.3f %.3f %.3f %.3f re S\n" % (tx * MM, (A4_H - ty - tile_h) * MM, tile_w * MM, tile_h * MM)
        s += "[] 0 d 0 G\n"
        # arrow pointing at the marker's top edge, then the id
        ay = ty + size + 2 * quiet + 1.5
        ax = tx + quiet + 3
        s += "%.3f %.3f m %.3f %.3f l %.3f %.3f l f\n" % (
            ax * MM, (A4_H - ay) * MM,
            (ax - 2.5) * MM, (A4_H - ay - 4.5) * MM,
            (ax + 2.5) * MM, (A4_H - ay - 4.5) * MM)
        # the label has to stay inside the tile, so the font follows the tile width
        fs = 11 if tile_w >= 70 else 8
        s += text(ax + 4, ay + 4.2, fs, "TOP  ID %d  %g mm" % (mid, size))

    ry = A4_H - 9
    rx = (A4_W - 100) / 2
    s += "0 G 0.5 w\n" + line(rx, ry, rx + 100, ry)
    for t in range(0, 101, 10):
        s += line(rx + t, ry - (3 if t % 50 == 0 else 1.5), rx + t, ry)
    s += text(rx + 102, ry + 1, 8, "100 mm")
    s += text(rx - 48, ry + 1, 8, "print at 100 %, check:")
    return s


def write_pdf(path, content):
    data = content.encode("ascii")
    objs = [
        b"<< /Type /Catalog /Pages 2 0 R >>",
        b"<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 %.3f %.3f] /Contents 4 0 R "
        b"/Resources << /Font << /F1 5 0 R >> >> >>" % (A4_W * MM, A4_H * MM),
        b"<< /Length %d >>\nstream\n" % len(data) + data + b"endstream",
        b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>",
    ]
    out = bytearray(b"%PDF-1.4\n")
    offsets = []
    for i, o in enumerate(objs):
        offsets.append(len(out))
        out += b"%d 0 obj\n" % (i + 1) + o + b"\nendobj\n"
    xref = len(out)
    out += b"xref\n0 %d\n0000000000 65535 f \n" % (len(objs) + 1)
    for off in offsets:
        out += b"%010d 00000 n \n" % off
    out += b"trailer\n<< /Size %d /Root 1 0 R >>\nstartxref\n%d\n%%%%EOF\n" % (len(objs) + 1, xref)
    Path(path).write_bytes(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--size", type=float, default=60.0, help="marker side in mm, black border included")
    ap.add_argument("--quiet", type=float, default=10.0, help="white margin around the marker in mm")
    ap.add_argument("--ids", type=int, nargs=6, default=[0, 1, 2, 3, 4, 5])
    ap.add_argument("--out", default=str(HERE / "../print/aruco_markers_a4.pdf"))
    a = ap.parse_args()
    write_pdf(a.out, build(a.size, a.quiet, a.ids, load_codes()))
    print("wrote", Path(a.out).resolve())


if __name__ == "__main__":
    main()
