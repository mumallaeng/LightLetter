"""Test image stimulus + expected results for tb_cnn_top_img.v (cnn_top with real EMNIST images).

test/<folder>/*.png : 28x28 grayscale, upright (already transposed like tb/cnn_golden/data.py), folder = EMNIST byclass
                      index (10..35 = A..Z). The network is uppercase only (FC3 26 outputs, class 0 = 'A'), so
                      label = folder - 10; digit folders (0..9) are skipped. The first N_PER_CLASS files of each
                      folder (sorted by name) are used.

Input quantization (conv_l1 input scale 2^-14, rtl/cnn/rtl_ref/README.md):
    pixel_in = round(p / 255 * 2^14)          p = 0..255 -> 0..16384 (never a .5 tie, 255 is odd)

A bit-exact integer model of the whole chain (gen_chain_vectors.py conv / pool, gen_fc_golden.py FC,
conv SCALE_EXP from rtl_ref/ce_params.txt) (conv_l1 -> pool_l1 -> conv_l2 -> pool_l2 -> FC1..3 -> argmax) gives
the expected logits / class, so a wrong answer can be told apart: RTL != model -> RTL bug, RTL == model != label ->
the network itself misclassifies the image. The model is checked first against rtl/cnn/rtl_ref (ce1_stim ->
ce1_out / pool1_out / ce2_out / pool2_out) and vectors/logit_out.mem.

    python gen_img_vectors.py [N_PER_CLASS]      (default 5)

When test/ is not there, the saved set vectors/img_stim.mem + vectors/img_list.txt is used instead (the 70 images,
5 per class M..Z, of the first run; the letter column of img_list.txt gives the label). A run from test/ saves its
set there again.

Output (build/ is where run_sim.sh runs xsim):
    build/img_stim.mem    16 bit x N*784   pixel_in, image after image, 28x28 raster
    build/img_label.mem    8 bit x N       label = folder - 10 (0 = 'A')
    build/img_class.mem    8 bit x N       class of the integer model
    build/img_logit.mem   16 bit x N*26    logits of the integer model
    build/img_list.txt                      index, label, file
"""
import sys
from pathlib import Path

import numpy as np
from PIL import Image

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
RTL_REF = ROOT / "rtl/cnn/rtl_ref"
TEST = HERE / "test"
BUILD = HERE / "build"
SAVED = HERE / "vectors"
CLASSES = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
N_CLASS = len(CLASSES)

sys.path.insert(0, str(HERE))
from gen_fc_golden import read_fc_txt, read_mem, fc  # noqa: E402
from gen_chain_vectors import conv_params, conv, pool, s_n  # noqa: E402


def conv_scales():
    """conv SCALE_EXP the rtl_ref streams were made with (rtl_ref/ce_params.txt)."""
    sc = {}
    for line in (RTL_REF / "ce_params.txt").read_text().splitlines():
        for layer in ("CONV_L1", "CONV_L2"):
            if line.startswith(layer):
                sc[layer] = int(line.split("SCALE_EXP=")[1].split()[0])
    return sc["CONV_L1"], sc["CONV_L2"]


class Model:
    def __init__(self):
        self.c1 = conv_params(RTL_REF, 1, 1, 6)
        self.c2 = conv_params(RTL_REF, 2, 6, 16)
        self.s1, self.s2 = conv_scales()
        self.fc = [read_fc_txt(k) for k in (1, 2, 3)]

    def run(self, img):
        """img: int (28, 28) pixel_in -> dict of every stage."""
        c1 = conv(img[None].astype(np.int64), *self.c1, self.s1)
        p1 = pool(c1)
        c2 = conv(p1, *self.c2, self.s2)
        p2 = pool(c2)
        x = [int(v) for v in p2.reshape(-1)]    # och-major 5x5 raster = pool_l2 stream order
        y1 = fc(self.fc[0], x)
        y2 = fc(self.fc[1], y1)
        lg = fc(self.fc[2], y2)
        cls = max(range(len(lg)), key=lambda i: (lg[i], -i))
        return dict(c1=c1, p1=p1, c2=c2, p2=p2, logit=lg, cls=cls)


def self_check(m):
    """The integer model against rtl_ref (FRAMES = 2)."""
    stim = [s_n(v, 16) for v in read_mem(RTL_REF / "ce1_stim.mem")]
    ce1 = read_mem(RTL_REF / "ce1_out.mem")
    p1g = read_mem(RTL_REF / "pool1_out.mem")
    ce2 = read_mem(RTL_REF / "ce2_out.mem")
    p2g = read_mem(RTL_REF / "pool2_out.mem")
    lgg = [s_n(v, 16) for v in read_mem(HERE / "vectors/logit_out.mem")]
    ok = True
    for f in range(2):
        r = m.run(np.array(stim[f * 784:(f + 1) * 784]).reshape(28, 28))

        def lanes3(a, words):                   # {ch_done, d2, d1, d0}, pass-major
            got = [a[p * 3 + l].reshape(-1) for p in range(2) for l in range(3)]
            exp = [[(wd >> (16 * l)) & 0xFFFF for wd in words[p * len(words) // 2:(p + 1) * len(words) // 2]]
                   for p in range(2) for l in range(3)]
            return all(list(g) == e for g, e in zip(got, exp))

        chk = {
            "C1": lanes3(r["c1"], ce1[f * 1352:(f + 1) * 1352]),
            "P1": lanes3(r["p1"], p1g[f * 338:(f + 1) * 338]),
            "C2": list(r["c2"].reshape(-1)) == [w & 0xFFFF for w in ce2[f * 1936:(f + 1) * 1936]],
            "P2": list(r["p2"].reshape(-1)) == [w & 0xFFFF for w in p2g[f * 400:(f + 1) * 400]],
            "LG": r["logit"] == lgg[f * N_CLASS:(f + 1) * N_CLASS],
        }
        print(f"  self check frame {f}: " + " ".join(f"{k} {'ok' if v else 'MISMATCH'}" for k, v in chk.items())
              + f"  class {r['cls']}")
        ok = ok and all(chk.values())
    return ok


def main():
    n_per = int(sys.argv[1]) if len(sys.argv) > 1 else 5
    m = Model()
    if not self_check(m):
        print("integer model does not match rtl_ref - stop")
        return 1

    images = []                                 # (pixel_in 28x28, label, file)
    if TEST.is_dir():
        for d in sorted(TEST.iterdir(), key=lambda p: int(p.name)):
            if not d.is_dir():
                continue
            if int(d.name) < 10:
                print(f"  skip test/{d.name}/ (digit, not a class of the uppercase network)")
                continue
            for f in sorted(d.glob("*.png"))[:n_per]:
                p = np.asarray(Image.open(f).convert("L"), dtype=np.int64)
                assert p.shape == (28, 28), (f, p.shape)
                img = (p * 16384 * 2 + 255) // (255 * 2)    # round(p / 255 * 2^14)
                images.append((img, int(d.name) - 10, f.relative_to(HERE).as_posix()))
        saved = [f"{i:3d} {l:2d} {CLASSES[l]} {n}" for i, (_, l, n) in enumerate(images)]
        (SAVED / "img_stim.mem").write_text("".join(f"{v:04x}\n" for im, _, _ in images for v in im.reshape(-1)))
        (SAVED / "img_list.txt").write_text("\n".join(saved) + "\n")
    else:
        print(f"  test/ not found - using the saved set vectors/img_stim.mem + img_list.txt (N_PER_CLASS ignored)")
        px = read_mem(SAVED / "img_stim.mem")
        for k, line in enumerate((SAVED / "img_list.txt").read_text().split("\n")):
            if line.strip():
                _, _, ch, name = line.split()
                images.append((np.array(px[k * 784:(k + 1) * 784], dtype=np.int64).reshape(28, 28),
                               CLASSES.index(ch), name))

    stim, labels, classes, logits, lines = [], [], [], [], []
    for img, label, name in images:
        r = m.run(img)
        idx = len(labels)
        stim += list(img.reshape(-1))
        labels.append(label)
        classes.append(r["cls"])
        logits += r["logit"]
        lines.append(f"{idx:3d} {label:2d} {CLASSES[label]} {name}")

    BUILD.mkdir(exist_ok=True)
    (BUILD / "img_stim.mem").write_text("".join(f"{v & 0xFFFF:04x}\n" for v in stim))
    (BUILD / "img_label.mem").write_text("".join(f"{v:02x}\n" for v in labels))
    (BUILD / "img_class.mem").write_text("".join(f"{v:02x}\n" for v in classes))
    (BUILD / "img_logit.mem").write_text("".join(f"{v & 0xFFFF:04x}\n" for v in logits))
    (BUILD / "img_list.txt").write_text("\n".join(lines) + "\n")
    hit = sum(l == c for l, c in zip(labels, classes))
    print(f"  {len(labels)} images, integer model {hit}/{len(labels)} correct")
    print(f"N_IMG={len(labels)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
