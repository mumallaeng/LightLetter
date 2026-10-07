"""FPGA HDMI 출력 -> 캡처보드 -> PC 화면 표시 + CNN 입력 확인.

    python capture_test.py               # 0번 장치, 1280x720
    python capture_test.py --device 1    # 다른 장치
    python capture_test.py --list        # 0~4번 장치 탐색만 하고 종료

키
    q / ESC : 종료
    s       : 현재 프레임 PNG + CNN 입력값(txt/npy)을 captures/ 에 저장
    p       : 현재 CNN 입력 28x28 값을 콘솔에 출력
    g       : 가이드 + 전처리 미리보기 on/off
    a       : 미리보기 경로 전환 ArUco 5칸 (기본) <-> 정중앙 ROI (이전 방식)

미리보기는 보드의 버튼 캡처 경로를 그대로 흉내 낸다.
    ArUco (aruco_c.py = src/aruco/*.c) : 마커 6개 검출 -> homography -> 칸 5개 112x112
        칸 창은 5단계를 보여 준다: 원본 crop -> 왜곡 보정 -> homography 112x112
        -> threshold + bbox -> CNN 28x28. 앞의 두 단계는 PC 시각화용이다. 보드는
        보정된 영상을 따로 만들지 않고 원본 프레임에서 112x112 를 바로 샘플한다.
    중앙 ROI (roi_dma.c)               : 정중앙 224x224 -> 112x112 nearest 다운스케일
    PL  (img_preprocess.v)   : luma -> 반전 -> threshold -> bbox -> 22x22 맞춤
                               -> 28x28 가운데 배치 -> x64.25 (2^-14 스케일)
캡처보드가 MJPG 로 압축한 영상에서 계산하므로 보드 안의 값과 몇 LSB 차이가
날 수 있다. 형태(bbox, 맞춤, 배치)는 같다. ArUco 는 프레임당 100 ms 이상 걸려
별도 스레드에서 돌고, 화면에는 가장 최근 결과를 겹쳐 그린다.
"""
import argparse
import threading
import time
from pathlib import Path

import cv2
import numpy as np

import aruco_c

IN_W, IN_H = 1280, 720

# roi_dma.h 와 같은 값 (1280x720 정중앙 224x224)
ROI_X0, ROI_Y0, ROI_SIZE = 528, 248, 224
ROI_W, ROI_H = 112, 112

# img_preprocess.v 와 같은 값
OUT_W, OUT_H = 28, 28
MARGIN = 3
THRESHOLD = 160
GLYPH_LIMIT = OUT_W - 2 * MARGIN

SAVE_DIR = Path(__file__).resolve().parent / "captures"

WIN_CAP = "FPGA Capture"
WIN_ROI = "ROI 112x112: RGB | threshold (x3)"
WIN_CNN = "CNN input 28x28 (x10)"
WIN_CELLS = "ArUco cells: raw | undistorted | homography | threshold | CNN input"

# 칸 창: 타일 한 변 (px), 단계 이름
TILE = 160
STAGES = ("raw", "undistorted", "homography", "threshold", "CNN 28x28")

MARKER_COLOR = (0, 255, 0)
CELL_COLOR = (0, 220, 255)
MISSING_COLOR = (0, 0, 255)


def open_device(index, width, height, fps, mjpg):
    cap = cv2.VideoCapture(index, cv2.CAP_DSHOW)
    if not cap.isOpened():
        return None
    if mjpg:
        # 720p60 은 대부분의 USB 캡처보드에서 MJPG 로만 나온다.
        cap.set(cv2.CAP_PROP_FOURCC, cv2.VideoWriter_fourcc(*"MJPG"))
    cap.set(cv2.CAP_PROP_FRAME_WIDTH, width)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, height)
    cap.set(cv2.CAP_PROP_FPS, fps)
    cap.set(cv2.CAP_PROP_BUFFERSIZE, 1)  # 지연 최소화
    return cap


def list_devices(n=5):
    for i in range(n):
        cap = cv2.VideoCapture(i, cv2.CAP_DSHOW)
        if not cap.isOpened():
            print(f"device {i}: -")
            continue
        ok, _ = cap.read()
        w = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
        h = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
        print(f"device {i}: {w}x{h}, read={'ok' if ok else 'fail'}")
        cap.release()


def ps_downscale(frame):
    """roi_dma.c: 각 칸의 가운데 픽셀을 고르는 112x112 nearest 다운스케일 (BGR 유지)."""
    h, w = frame.shape[:2]
    if (w, h) != (IN_W, IN_H):
        frame = cv2.resize(frame, (IN_W, IN_H), interpolation=cv2.INTER_NEAREST)
    xs = ROI_X0 + ((2 * np.arange(ROI_W) + 1) * ROI_SIZE) // (2 * ROI_W)
    ys = ROI_Y0 + ((2 * np.arange(ROI_H) + 1) * ROI_SIZE) // (2 * ROI_H)
    return frame[ys[:, None], xs[None, :]]


def rtl_threshold(roi):
    """img_preprocess.v 입력단: luma -> 반전 -> threshold (112x112 uint8)."""
    px = roi.astype(np.int32)
    b, g, r = px[..., 0], px[..., 1], px[..., 2]
    luma = (77 * r + 150 * g + 29 * b + 128) >> 8
    inv = 255 - luma
    return np.where(inv < THRESHOLD, 0, inv).astype(np.uint8)


def rtl_fit(proc):
    """img_preprocess.v 출력단: bbox -> 22x22 맞춤 -> 28x28 배치 (스케일 전 8비트).

    반환: (28x28 uint8, info dict 또는 전경이 없으면 None)
    """
    out = np.zeros((OUT_H, OUT_W), np.uint8)
    ys, xs = np.nonzero(proc)
    if xs.size == 0:
        return out, None

    min_x, max_x = int(xs.min()), int(xs.max())
    min_y, max_y = int(ys.min()), int(ys.max())
    bw, bh = max_x - min_x + 1, max_y - min_y + 1

    wide = (max_x - min_x) >= (max_y - min_y)
    short, long_ = (bh, bw) if wide else (bw, bh)
    # ST_FIT: c*long <= short*22 를 만족하는 가장 큰 c (1..22), 없으면 1
    fit = max(1, min(GLYPH_LIMIT, (short * GLYPH_LIMIT) // long_))

    if wide:
        gw, gh = GLYPH_LIMIT, fit
        ox, oy = (OUT_W - GLYPH_LIMIT) // 2, (OUT_H - fit) >> 1
    else:
        gw, gh = fit, GLYPH_LIMIT
        ox, oy = (OUT_W - fit) >> 1, (OUT_H - GLYPH_LIMIT) // 2

    # ST_X/Y_STORE: map[i] = min + floor(i * bbox / glyph)
    xm = min_x + (np.arange(gw) * bw) // gw
    ym = min_y + (np.arange(gh) * bh) // gh
    out[oy:oy + gh, ox:ox + gw] = proc[ym[:, None], xm[None, :]]

    info = dict(bbox=(min_x, min_y, bw, bh), glyph=(gw, gh), offset=(ox, oy))
    return out, info


def cnn_scale(p8):
    """cnn_pixel = (p << 6) + (p >> 2): cnn_ip 에 실제로 들어가는 16비트 값."""
    p = p8.astype(np.uint16)
    return (p << 6) + (p >> 2)


def cnn_pipeline(frame):
    roi = ps_downscale(frame)
    proc = rtl_threshold(roi)
    out8, info = rtl_fit(proc)
    return roi, proc, out8, cnn_scale(out8), info


def format_cnn(cnn, info):
    lines = []
    if info is None:
        lines.append("전경 없음 -> CNN 입력 전부 0")
    else:
        x, y, w, h = info["bbox"]
        lines.append(f"bbox(112 기준) x={x} y={y} {w}x{h}  "
                     f"glyph {info['glyph'][0]}x{info['glyph'][1]}  "
                     f"offset {info['offset']}")
    lines.append("CNN input 28x28 (16-bit, 2^-14 scale, 0 은 '.')")
    for row in cnn:
        lines.append(" ".join(f"{v:5d}" if v else "    ." for v in row))
    return "\n".join(lines)


def show_preview(roi, proc, out8, info):
    view = np.hstack([roi, cv2.cvtColor(proc, cv2.COLOR_GRAY2BGR)])
    view = cv2.resize(view, None, fx=3, fy=3, interpolation=cv2.INTER_NEAREST)
    if info is not None:
        x, y, w, h = info["bbox"]
        for dx in (0, ROI_W * 3):
            cv2.rectangle(view, (x * 3 + dx, y * 3),
                          ((x + w) * 3 - 1 + dx, (y + h) * 3 - 1), (0, 0, 255), 1)
    cv2.imshow(WIN_ROI, view)
    cv2.imshow(WIN_CNN, cv2.resize(out8, (OUT_W * 10, OUT_H * 10),
                                   interpolation=cv2.INTER_NEAREST))


class ArucoWorker:
    """보드의 aruco_crop_run 을 별도 스레드에서 돌린다. 화면은 가장 최근 결과를 쓴다."""

    def __init__(self):
        self._lock = threading.Lock()
        self._wake = threading.Event()
        self._frame = None
        self._result = None
        self._stop = False
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()

    def submit(self, frame):
        """가장 최근 프레임만 남긴다. 계산 중에 들어온 프레임은 덮어쓴다."""
        with self._lock:
            self._frame = frame
        self._wake.set()

    def latest(self):
        with self._lock:
            return self._result

    def close(self):
        self._stop = True
        self._wake.set()
        self._thread.join(timeout=1)

    def _run(self):
        while True:
            self._wake.wait()
            self._wake.clear()
            if self._stop:
                return
            with self._lock:
                frame, self._frame = self._frame, None
            if frame is None:
                continue
            result = run_aruco(frame)
            with self._lock:
                self._result = result


def run_aruco(frame):
    """프레임 하나에 보드와 같은 ArUco 크롭 + img_preprocess 를 돌린다."""
    t0 = time.perf_counter()
    dbg = {}
    board = to_board_frame(frame)
    rc, cells, res, markers = aruco_c.crop_run(board, dbg)
    cnn = [cell_cnn(c) for c in cells] if rc == aruco_c.OK else None
    views = None
    if rc == aruco_c.OK:
        views = [(view_raw(board, aruco_c.cell_outline(res, k)),
                  view_undistorted(board, aruco_c.cell_outline_ideal(res, k)))
                 for k in range(aruco_c.NUM_CELLS)]
    return dict(rc=rc, cells=cells, res=res, markers=markers, debug=dbg, cnn=cnn,
                views=views, ms=(time.perf_counter() - t0) * 1000)


def fit_tile(img, size=TILE):
    """비율을 유지해 size x size 타일 가운데에 넣는다. (tile, scale, ox, oy)."""
    h, w = img.shape[:2]
    s = size / max(h, w)
    nw, nh = max(1, round(w * s)), max(1, round(h * s))
    tile = np.full((size, size, 3), 40, np.uint8)
    ox, oy = (size - nw) // 2, (size - nh) // 2
    tile[oy:oy + nh, ox:ox + nw] = cv2.resize(img, (nw, nh), interpolation=cv2.INTER_AREA)
    return tile, s, ox, oy


def view_raw(board, poly, pad=8):
    """1단계: 칸이 차지하는 원본 프레임 영역. 렌즈 왜곡으로 휜 테두리를 함께 그린다."""
    h, w = board.shape[:2]
    x0, y0 = np.maximum(np.floor(poly.min(axis=0)).astype(int) - pad, 0)
    x1, y1 = np.minimum(np.ceil(poly.max(axis=0)).astype(int) + pad, (w, h))
    tile, s, ox, oy = fit_tile(board[y0:y1, x0:x1])
    pts = np.round((poly - (x0, y0)) * s + (ox, oy)).astype(np.int32)
    cv2.polylines(tile, [pts], True, CELL_COLOR, 1)
    return tile


def view_undistorted(board, poly, pad=8):
    """2단계: 같은 영역의 왜곡을 편 영상 (homography 전). 테두리는 곧은 사각형이 된다."""
    x0, y0 = poly.min(axis=0) - pad
    x1, y1 = poly.max(axis=0) + pad
    s = TILE / max(x1 - x0, y1 - y0)
    nw, nh = max(1, round((x1 - x0) * s)), max(1, round((y1 - y0) * s))
    u = x0 + (np.arange(nw) + 0.5) / s
    v = y0 + (np.arange(nh) + 0.5) / s
    ideal = np.stack(np.broadcast_arrays(u[None, :], v[:, None]), axis=-1)
    raw = aruco_c.distort_points(ideal).astype(np.float32)
    img = cv2.remap(board, raw[..., 0], raw[..., 1], cv2.INTER_LINEAR,
                    borderMode=cv2.BORDER_REPLICATE)
    tile = np.full((TILE, TILE, 3), 40, np.uint8)
    ox, oy = (TILE - nw) // 2, (TILE - nh) // 2
    tile[oy:oy + nh, ox:ox + nw] = img
    pts = np.round((poly - (x0, y0)) * s + (ox, oy)).astype(np.int32)
    cv2.polylines(tile, [pts], True, CELL_COLOR, 1)
    return tile


def to_board_frame(frame):
    """보드 frame store 와 같은 1280x720 으로 맞춘다 (ArUco 는 이 크기만 받는다)."""
    h, w = frame.shape[:2]
    if (w, h) != (IN_W, IN_H):
        frame = cv2.resize(frame, (IN_W, IN_H), interpolation=cv2.INTER_NEAREST)
    return frame


def cell_cnn(cell):
    """ArUco 칸 하나 -> img_preprocess.v -> (threshold, 28x28 8비트, cnn 16비트, info)."""
    proc = rtl_threshold(cell)
    out8, info = rtl_fit(proc)
    return proc, out8, cnn_scale(out8), info


def draw_aruco(frame, ar):
    """검출된 마커, 안 보여서 투영으로 채운 마커, 칸 5개 크롭 영역을 겹쳐 그린다."""
    h, w = frame.shape[:2]
    s = np.array([w / IN_W, h / IN_H])

    def pt(p):
        return tuple(int(round(v)) for v in np.asarray(p) * s)

    found = set()
    for m in ar["markers"]:
        found.add(m["id"])
        quad = np.round(m["corners"] * s).astype(np.int32)
        cv2.polylines(frame, [quad], True, MARKER_COLOR, 2)
        cv2.circle(frame, pt(m["corners"][0]), 5, MARKER_COLOR, -1)   # 마커의 왼쪽 위 꼭짓점
        org = (int(quad[:, 0].min()), int(quad[:, 1].min()) - 8)     # 마커 바로 위
        cv2.putText(frame, f"id{m['id']}", org, cv2.FONT_HERSHEY_SIMPLEX, 0.6, (0, 0, 0), 4)
        cv2.putText(frame, f"id{m['id']}", org, cv2.FONT_HERSHEY_SIMPLEX, 0.6, MARKER_COLOR, 2)

    res = ar["res"]
    if ar["rc"] == aruco_c.OK:
        raw = aruco_c.distort_points(res["center"])
        for mid in range(aruco_c.NUM_MARKERS):
            if mid not in found:
                c = pt(raw[mid])
                cv2.drawMarker(frame, c, MISSING_COLOR, cv2.MARKER_TILTED_CROSS, 18, 2)
                cv2.putText(frame, f"id{mid}?", (c[0] + 10, c[1] - 10),
                            cv2.FONT_HERSHEY_SIMPLEX, 0.6, MISSING_COLOR, 2)
        for k in range(aruco_c.NUM_CELLS):
            poly = np.round(aruco_c.cell_outline(res, k) * s).astype(np.int32)
            cv2.polylines(frame, [poly], True, CELL_COLOR, 2)
            cv2.putText(frame, str(k), tuple(poly[0] + (6, 22)),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.7, CELL_COLOR, 2)
        status = (f"ArUco OK  {res['n_markers']}/6 markers  fit {res['fit_rms_px']:.2f}px  "
                  f"{ar['ms']:.0f} ms")
        color = MARKER_COLOR
    else:
        status = (f"ArUco {aruco_c.ERR_NAME[ar['rc']]}  {len(ar['markers'])}/6 markers  "
                  f"{ar['ms']:.0f} ms")
        color = MISSING_COLOR
    cv2.putText(frame, status, (20, 80), cv2.FONT_HERSHEY_SIMPLEX, 0.9, (0, 0, 0), 5)
    cv2.putText(frame, status, (20, 80), cv2.FONT_HERSHEY_SIMPLEX, 0.9, color, 2)


def cell_tiles(ar, k):
    """칸 k의 단계별 타일 5개 (STAGES 순서). threshold 타일에는 bbox를 그린다."""
    T = TILE
    proc, out8, _, info = ar["cnn"][k]
    raw, undist = ar["views"][k]
    rgb = cv2.resize(ar["cells"][k], (T, T), interpolation=cv2.INTER_NEAREST)
    thr = cv2.resize(cv2.cvtColor(proc, cv2.COLOR_GRAY2BGR), (T, T),
                     interpolation=cv2.INTER_NEAREST)
    if info is not None:
        s = T / proc.shape[1]
        bx, by, bw, bh = info["bbox"]
        cv2.rectangle(thr, (int(bx * s), int(by * s)),
                      (int((bx + bw) * s) - 1, int((by + bh) * s) - 1), (0, 0, 255), 1)
    cnn = cv2.resize(cv2.cvtColor(out8, cv2.COLOR_GRAY2BGR), (T, T),
                     interpolation=cv2.INTER_NEAREST)
    return [raw, undist, rgb, thr, cnn]


def put_label(canvas, text, x, y):
    cv2.putText(canvas, text, (x, y), cv2.FONT_HERSHEY_SIMPLEX, 0.45, (0, 0, 0), 3)
    cv2.putText(canvas, text, (x, y), cv2.FONT_HERSHEY_SIMPLEX, 0.45, CELL_COLOR, 1)


def show_cells(ar, strip=None):
    """칸 5개를 열로, 위에서 아래로 STAGES 순서: 원본 crop, 왜곡 보정, homography 112,
    threshold + bbox, CNN 28x28. strip이 칸 번호면 그 칸의 5단계를 첫 행에 가로로 놓고
    아래에는 homography, threshold, CNN 3행만 남긴다 (4x5)."""
    T, GAP, HEAD = TILE, 6, 26
    n = aruco_c.NUM_CELLS
    first = 2 if strip is not None else 0       # 아래 격자에서 건너뛸 앞쪽 단계 수
    rows = len(STAGES) - first
    top = (HEAD + T + GAP) if strip is not None else 0
    canvas = np.full((top + HEAD + rows * T + (rows - 1) * GAP, n * T + (n - 1) * GAP, 3),
                     40, np.uint8)
    ok = ar["cnn"] is not None
    if strip is not None:
        cv2.putText(canvas, f"cell {strip}: " + " > ".join(STAGES), (4, 19),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.6, CELL_COLOR, 1)
        if ok:
            for j, img in enumerate(cell_tiles(ar, strip)):
                canvas[HEAD:HEAD + T, j * (T + GAP):j * (T + GAP) + T] = img
    for k in range(n):
        x = k * (T + GAP)
        cv2.putText(canvas, f"cell {k}", (x + 4, top + 19), cv2.FONT_HERSHEY_SIMPLEX, 0.6,
                    CELL_COLOR, 1)
        if not ok:
            continue
        y = top + HEAD
        for img, name in zip(cell_tiles(ar, k)[first:], STAGES[first:]):
            canvas[y:y + T, x:x + T] = img
            if k == 0:
                put_label(canvas, name, x + 4, y + T - 6)
            y += T + GAP
    if not ok:
        cv2.putText(canvas, f"crop failed: {aruco_c.ERR_NAME[ar['rc']]}", (20, top + HEAD + 60),
                    cv2.FONT_HERSHEY_SIMPLEX, 1.0, MISSING_COLOR, 2)
    cv2.imshow(WIN_CELLS, canvas)


def format_aruco(ar):
    lines = [f"ArUco rc={aruco_c.ERR_NAME[ar['rc']]}  markers="
             f"{[m['id'] for m in ar['markers']]}  fit={ar['res']['fit_rms_px']:.3f}px  "
             f"{ar['ms']:.0f} ms"]
    for wnd in ar["debug"].get("windows", []):
        lines.append(f"  threshold win {wnd['win']}: candidates {wnd['candidates']}, "
                     f"identified {wnd['identified']}, pool {wnd['pool']}")
    for m in ar["markers"]:
        lines.append(f"  id {m['id']}: center ({m['center'][0]:.1f}, {m['center'][1]:.1f})")
    if ar["cnn"] is not None:
        for k, (_, _, cnn, info) in enumerate(ar["cnn"]):
            lines.append(f"--- cell {k}")
            lines.append(format_cnn(cnn, info))
    return "\n".join(lines)


def save_aruco(stem, ar):
    if ar["cnn"] is not None:
        for k, (_, _, cnn, _) in enumerate(ar["cnn"]):
            raw, undist = ar["views"][k]
            cv2.imwrite(f"{stem}_cell{k}_raw.png", raw)
            cv2.imwrite(f"{stem}_cell{k}_undistorted.png", undist)
            cv2.imwrite(f"{stem}_cell{k}.png", ar["cells"][k])
            np.save(f"{stem}_cell{k}_cnn28.npy", cnn)
    Path(f"{stem}_aruco.txt").write_text(format_aruco(ar) + "\n", encoding="utf-8")


def draw_overlay(frame, fps, guide):
    h, w = frame.shape[:2]
    if guide:
        sx, sy = w / IN_W, h / IN_H
        p0 = (int(ROI_X0 * sx), int(ROI_Y0 * sy))
        p1 = (int((ROI_X0 + ROI_SIZE) * sx) - 1, int((ROI_Y0 + ROI_SIZE) * sy) - 1)
        cv2.rectangle(frame, p0, p1, (0, 255, 0), 2)
    cv2.putText(frame, f"{w}x{h}  {fps:5.1f} fps", (20, 40),
                cv2.FONT_HERSHEY_SIMPLEX, 1, (255, 255, 255), 2)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--device", type=int, default=0)
    ap.add_argument("--width", type=int, default=IN_W)
    ap.add_argument("--height", type=int, default=IN_H)
    ap.add_argument("--fps", type=int, default=60)
    ap.add_argument("--no-mjpg", action="store_true", help="MJPG 요청을 끈다 (화면이 안 나오면 시도)")
    ap.add_argument("--list", action="store_true", help="장치 목록만 출력")
    ap.add_argument("--image", help="캡처보드 대신 저장한 프레임(PNG 등)을 계속 보여준다")
    ap.add_argument("--strip", choices=("none", "first", "last"), default="none",
                    help="칸 창 첫 행에 첫(first)/마지막(last) 칸의 5단계를 가로로 놓는다")
    args = ap.parse_args()

    if args.list:
        list_devices()
        return

    still = None
    if args.image:
        still = cv2.imread(args.image, cv2.IMREAD_COLOR)
        if still is None:
            print(f"이미지를 읽을 수 없습니다: {args.image}")
            return
        print(f"image {args.image}: {still.shape[1]}x{still.shape[0]}")
    else:
        cap = open_device(args.device, args.width, args.height, args.fps, not args.no_mjpg)
        if cap is None:
            print(f"캡처보드(device {args.device})를 열 수 없습니다. --list 로 번호를 확인하세요.")
            return
        print(f"device {args.device}: "
              f"{int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))}x{int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))}")
    cv2.namedWindow(WIN_CAP, cv2.WINDOW_NORMAL)

    strip = {"none": None, "first": 0, "last": aruco_c.NUM_CELLS - 1}[args.strip]
    guide = True
    mode = "aruco"                              # "aruco" | "center"
    worker = ArucoWorker()
    fps, t_prev, fails = 0.0, time.perf_counter(), 0
    while True:
        if still is not None:
            ok, frame = True, still.copy()
            time.sleep(1 / 30)
        else:
            ok, frame = cap.read()
        if not ok:
            fails += 1
            if fails > 30:
                print("프레임을 계속 읽지 못했습니다. HDMI 신호/케이블을 확인하세요.")
                break
            continue
        fails = 0

        now = time.perf_counter()
        fps = 0.9 * fps + 0.1 / max(now - t_prev, 1e-6)
        t_prev = now

        raw = frame.copy()                      # 저장·미리보기는 오버레이 없는 원본으로
        draw_overlay(frame, fps, guide and mode == "center")

        if mode == "aruco":
            worker.submit(raw)
            ar = worker.latest()
            if guide and ar is not None:
                draw_aruco(frame, ar)
                show_cells(ar, strip)
        else:
            roi, proc, out8, cnn, info = cnn_pipeline(raw)
            if guide:
                show_preview(roi, proc, out8, info)
        cv2.putText(frame, f"[a] mode: {mode}", (20, frame.shape[0] - 20),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.8, (255, 255, 255), 2)
        cv2.imshow(WIN_CAP, frame)

        key = cv2.waitKey(1) & 0xFF
        if key in (ord("q"), 27):
            break
        if key == ord("p"):
            if mode == "aruco":
                print(format_aruco(run_aruco(raw)))
            else:
                print(format_cnn(cnn, info))
        if key == ord("s"):
            SAVE_DIR.mkdir(exist_ok=True)
            stem = SAVE_DIR / time.strftime("capture_%Y%m%d_%H%M%S")
            cv2.imwrite(f"{stem}.png", raw)
            if mode == "aruco":
                save_aruco(stem, run_aruco(raw))
                print("saved", f"{stem}.png", "+ cell0..4.png / cell*_cnn28.npy / aruco.txt")
            else:
                cv2.imwrite(f"{stem}_roi112.png", roi)
                np.save(f"{stem}_cnn28.npy", cnn)
                Path(f"{stem}_cnn28.txt").write_text(format_cnn(cnn, info) + "\n",
                                                     encoding="utf-8")
                print("saved", f"{stem}.png", "+ roi112.png / cnn28.npy / cnn28.txt")
        if key == ord("a"):
            mode = "center" if mode == "aruco" else "aruco"
            close_previews()
        if key == ord("g"):
            guide = not guide
            if not guide:
                close_previews()

    worker.close()
    if still is None:
        cap.release()
    cv2.destroyAllWindows()


def close_previews():
    for name in (WIN_ROI, WIN_CNN, WIN_CELLS):
        try:
            cv2.destroyWindow(name)
        except cv2.error:
            pass                                # 아직 안 열린 창


if __name__ == "__main__":
    main()
