"""LightLetter 송신 UI 서버. capture_test.py 의 미리보기를 웹 페이지로 보여 준다.

    python server.py                     # 0번 캡처보드, http://localhost:8766
    python server.py --device 1          # 번호는 python server.py --list 로 확인
    python server.py --image captures/synth_frame.png    # 보드 없이 저장한 프레임으로

수신 UI(rx/ui)와 같은 테마의 정적 페이지(web/)와 JSON/이미지 API 를 같이 낸다.
전처리 계산은 capture_test.py 의 함수를 그대로 쓴다.
"""
import argparse
import json
import sys
import threading
import time
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

import cv2
import numpy as np

import aruco_c
import capture_test as ct

WEB = Path(__file__).resolve().parent / "web"

# 단계 id, 화면에 쓰는 이름, 설명. 칸 창의 STAGES 순서와 같다.
ARUCO_STAGES = [
    ("raw", "원본 crop", "칸이 차지하는 원본 프레임 영역. 렌즈 왜곡으로 휜 테두리를 같이 그린다. (PC 시각화용)"),
    ("undistorted", "왜곡 보정", "같은 영역의 렌즈 왜곡을 편 영상. 테두리가 곧은 사각형이 된다. (PC 시각화용)"),
    ("homography", "homography 112×112", "보드가 만드는 칸 영상. 원본 프레임에서 112×112 를 바로 샘플한다."),
    ("threshold", "threshold + bbox", "luma → 반전 → threshold. 빨간 상자는 글자의 bbox."),
    ("cnn28", "CNN 입력 28×28", "bbox 를 22×22 에 맞추고 28×28 가운데에 배치한 CNN 입력."),
]
CENTER_STAGES = [
    ("roi", "ROI 112×112", "정중앙 224×224 를 112×112 로 nearest 다운스케일한 영상."),
    ("threshold", "threshold + bbox", "luma → 반전 → threshold. 빨간 상자는 글자의 bbox."),
    ("cnn28", "CNN 입력 28×28", "bbox 를 22×22 에 맞추고 28×28 가운데에 배치한 CNN 입력."),
]
STAGES = {"aruco": ARUCO_STAGES, "center": CENTER_STAGES}
TILE_GAP = 6


def backend():
    return cv2.CAP_DSHOW if sys.platform == "win32" else cv2.CAP_ANY


def list_devices(n=5):
    for i in range(n):
        cap = cv2.VideoCapture(i, backend())
        if not cap.isOpened():
            print(f"device {i}: -")
            continue
        ok, _ = cap.read()
        w, h = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH)), int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
        print(f"device {i}: {w}x{h}, read={'ok' if ok else 'fail'}")
        cap.release()


class Source:
    """캡처보드(또는 저장한 프레임)를 계속 읽어 가장 최근 프레임과 fps 를 보관한다."""

    def __init__(self, args):
        self.args, self.lock = args, threading.Lock()
        self.frame, self.fps, self.error = None, 0.0, None
        self.still, self.cap = None, None
        if args.image:
            self.still = cv2.imread(args.image, cv2.IMREAD_COLOR)
            if self.still is None:
                raise SystemExit(f"이미지를 읽을 수 없습니다: {args.image}")
            self.name = f"image {Path(args.image).name}"
        else:
            self.name = f"device {args.device}"
            # macOS 는 카메라 권한 요청을 메인 스레드에서만 띄울 수 있어서 여기서 연다
            self.cap = self._open()
        threading.Thread(target=self._run, daemon=True).start()

    def _open(self):
        a = self.args
        cap = cv2.VideoCapture(a.device, backend())
        if not cap.isOpened():
            return None
        if not a.no_mjpg:
            cap.set(cv2.CAP_PROP_FOURCC, cv2.VideoWriter_fourcc(*"MJPG"))
        cap.set(cv2.CAP_PROP_FRAME_WIDTH, a.width)
        cap.set(cv2.CAP_PROP_FRAME_HEIGHT, a.height)
        cap.set(cv2.CAP_PROP_FPS, a.fps)
        cap.set(cv2.CAP_PROP_BUFFERSIZE, 1)
        return cap

    def _run(self):
        cap = self.cap
        if self.still is None and cap is None:
            self.error = f"캡처보드(device {self.args.device})를 열 수 없습니다. --list 로 번호를 확인하세요."
            return
        t_prev, fails = time.perf_counter(), 0
        while True:
            if self.still is not None:
                ok, frame = True, self.still.copy()
                time.sleep(1 / 30)
            else:
                ok, frame = cap.read()
            if not ok:
                fails += 1
                if fails > 30:
                    self.error = "프레임을 계속 읽지 못했습니다. HDMI 신호와 케이블을 확인하세요."
                    return
                continue
            fails = 0
            now = time.perf_counter()
            with self.lock:
                self.fps = 0.9 * self.fps + 0.1 / max(now - t_prev, 1e-6)
                self.frame, self.error = frame, None
            t_prev = now

    def latest(self):
        with self.lock:
            return (None if self.frame is None else self.frame.copy()), self.fps


class State:
    def __init__(self, args):
        self.src = Source(args)
        self.worker = ct.ArucoWorker()
        self.mode = "aruco"
        self.lock = threading.Lock()

    def snapshot(self):
        """가장 최근 프레임과 그 프레임의 계산 결과. center 모드는 요청 때 계산한다."""
        raw, fps = self.src.latest()
        if raw is None:
            return None, fps, None
        if self.mode == "aruco":
            self.worker.submit(raw)
            return raw, fps, self.worker.latest()
        return raw, fps, ct.cnn_pipeline(raw)


def message_tile(text, w=520, h=160):
    img = np.full((h, w, 3), 40, np.uint8)
    cv2.putText(img, text, (16, h // 2), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (255, 255, 255), 1, cv2.LINE_AA)
    return img


def aruco_stage_image(ar, stage):
    """칸 5개의 같은 단계 타일을 가로로 이어 붙인다."""
    if ar is None:
        return message_tile("ArUco 계산 대기 중")
    if ar["cnn"] is None:
        return message_tile(f"crop 실패: {aruco_c.ERR_NAME[ar['rc']]}  (마커 {len(ar['markers'])}/6)")
    idx = [s[0] for s in ARUCO_STAGES].index(stage)
    T = ct.TILE
    tiles = [ct.cell_tiles(ar, k)[idx] for k in range(aruco_c.NUM_CELLS)]
    gap = np.full((T, TILE_GAP, 3), 24, np.uint8)
    row = []
    for i, t in enumerate(tiles):
        row += [t] if i == 0 else [gap, t]
    return np.hstack(row)


def center_stage_image(result, stage):
    if result is None:
        return message_tile("프레임 대기 중")
    roi, proc, out8, _, info = result
    if stage == "roi":
        return cv2.resize(roi, None, fx=3, fy=3, interpolation=cv2.INTER_NEAREST)
    if stage == "threshold":
        view = cv2.resize(cv2.cvtColor(proc, cv2.COLOR_GRAY2BGR), None, fx=3, fy=3,
                          interpolation=cv2.INTER_NEAREST)
        if info is not None:
            x, y, w, h = info["bbox"]
            cv2.rectangle(view, (x * 3, y * 3), ((x + w) * 3 - 1, (y + h) * 3 - 1), (0, 0, 255), 1)
        return view
    return cv2.resize(cv2.cvtColor(out8, cv2.COLOR_GRAY2BGR), (ct.OUT_W * 10, ct.OUT_H * 10),
                      interpolation=cv2.INTER_NEAREST)


def encode(img, ext=".png", quality=80):
    params = [cv2.IMWRITE_JPEG_QUALITY, quality] if ext == ".jpg" else []
    return cv2.imencode(ext, img, params)[1].tobytes()


def cnn_text(state, cell):
    raw, _, res = state.snapshot()
    if raw is None or res is None:
        return "프레임 대기 중"
    if state.mode == "aruco":
        if res["cnn"] is None:
            return f"crop 실패: {aruco_c.ERR_NAME[res['rc']]}"
        return f"cell {cell}\n" + ct.format_cnn(res["cnn"][cell][2], res["cnn"][cell][3])
    return ct.format_cnn(res[3], res[4])


def save_capture(state):
    raw, _, res = state.snapshot()
    if raw is None:
        return None
    ct.SAVE_DIR.mkdir(exist_ok=True)
    stem = ct.SAVE_DIR / time.strftime("capture_%Y%m%d_%H%M%S")
    cv2.imwrite(f"{stem}.png", raw)
    if state.mode == "aruco":
        ct.save_aruco(stem, ct.run_aruco(raw))
    else:
        roi, _, _, cnn, info = res
        cv2.imwrite(f"{stem}_roi112.png", roi)
        np.save(f"{stem}_cnn28.npy", cnn)
        Path(f"{stem}_cnn28.txt").write_text(ct.format_cnn(cnn, info) + "\n", encoding="utf-8")
    return sorted(p.name for p in ct.SAVE_DIR.glob(stem.name + "*"))


def make_handler(state):
    class Handler(SimpleHTTPRequestHandler):
        def __init__(self, *a, **kw):
            super().__init__(*a, directory=str(WEB), **kw)

        def log_message(self, *a):
            pass

        def send_bytes(self, body, ctype, code=200):
            self.send_response(code)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)

        def send_json(self, obj, code=200):
            self.send_bytes(json.dumps(obj, ensure_ascii=False).encode("utf-8"),
                            "application/json; charset=utf-8", code)

        def do_GET(self):
            u = urlparse(self.path)
            q = parse_qs(u.query)
            if not u.path.startswith("/api/"):
                return super().do_GET()
            try:
                self.api_get(u.path, q)
            except (BrokenPipeError, ConnectionResetError):
                pass

        def api_get(self, path, q):
            if path == "/api/status":
                raw, fps, res = state.snapshot()
                ar = res if state.mode == "aruco" else None
                aruco = None
                if ar is not None:
                    aruco = dict(rc=aruco_c.ERR_NAME[ar["rc"]], markers=[m["id"] for m in ar["markers"]],
                                 fit_px=round(float(ar["res"]["fit_rms_px"]), 2), ms=round(ar["ms"]))
                return self.send_json(dict(
                    source=state.src.name, error=state.src.error, fps=round(fps, 1), mode=state.mode,
                    frame=None if raw is None else [raw.shape[1], raw.shape[0]], aruco=aruco,
                    stages={m: [dict(id=i, label=l, desc=d) for i, l, d in s] for m, s in STAGES.items()},
                    cells=aruco_c.NUM_CELLS))
            if path == "/api/frame.jpg":
                raw, fps, res = state.snapshot()
                if raw is None:
                    return self.send_bytes(encode(message_tile(state.src.error or "프레임 대기 중"), ".jpg"), "image/jpeg")
                guide = q.get("guide", ["1"])[0] == "1"
                ct.draw_overlay(raw, fps, guide and state.mode == "center")
                if state.mode == "aruco" and res is not None and guide:
                    ct.draw_aruco(raw, res)
                return self.send_bytes(encode(raw, ".jpg"), "image/jpeg")
            if path.startswith("/api/stage/") and path.endswith(".png"):
                stage = path[len("/api/stage/"):-4]
                if stage not in [s[0] for s in STAGES[state.mode]]:
                    return self.send_bytes(b"unknown stage", "text/plain", 404)
                raw, _, res = state.snapshot()
                img = aruco_stage_image(res, stage) if state.mode == "aruco" else center_stage_image(res, stage)
                return self.send_bytes(encode(img), "image/png")
            if path == "/api/cnn.txt":
                cell = min(max(int(q.get("cell", ["0"])[0]), 0), aruco_c.NUM_CELLS - 1)
                return self.send_bytes(cnn_text(state, cell).encode("utf-8"), "text/plain; charset=utf-8")
            self.send_bytes(b"not found", "text/plain", 404)

        def do_POST(self):
            u = urlparse(self.path)
            n = int(self.headers.get("Content-Length", 0))
            body = json.loads(self.rfile.read(n) or b"{}")
            if u.path == "/api/mode" and body.get("mode") in STAGES:
                state.mode = body["mode"]
                return self.send_json(dict(mode=state.mode))
            if u.path == "/api/capture":
                files = save_capture(state)
                return self.send_json(dict(files=files, dir=str(ct.SAVE_DIR)), 200 if files else 503)
            self.send_bytes(b"not found", "text/plain", 404)

    return Handler


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--device", type=int, default=0)
    ap.add_argument("--width", type=int, default=ct.IN_W)
    ap.add_argument("--height", type=int, default=ct.IN_H)
    ap.add_argument("--fps", type=int, default=60)
    ap.add_argument("--no-mjpg", action="store_true", help="MJPG 요청을 끈다 (화면이 안 나오면 시도)")
    ap.add_argument("--image", help="캡처보드 대신 저장한 프레임(PNG 등)을 계속 보여 준다")
    ap.add_argument("--port", type=int, default=8766)
    ap.add_argument("--list", action="store_true", help="0~4번 장치를 탐색해 번호를 보여 주고 종료")
    args = ap.parse_args()
    if args.list:
        list_devices()
        return
    state = State(args)
    print(f"LightLetter 송신 UI: http://localhost:{args.port}  ({state.src.name})", flush=True)
    ThreadingHTTPServer(("127.0.0.1", args.port), make_handler(state)).serve_forever()


if __name__ == "__main__":
    main()
