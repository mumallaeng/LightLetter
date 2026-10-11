"""LightLetter 송신 UI 서버. capture_test.py 의 미리보기를 웹 페이지로 보여 준다.

    python server.py                     # 브라우저가 캡처보드를 열어 영상을 보낸다, http://localhost:8766
    python server.py --device 1          # 서버가 직접 연다 (번호는 --list 로 확인)
    python server.py --image captures/synth_frame.png    # 보드 없이 저장한 프레임으로
    python server.py --uart COM5         # 보드 UART 를 읽어 보드(PL/CPU) 인식 결과를 보여 준다

수신 UI(rx/ui)와 같은 테마의 정적 페이지(web/)와 JSON/이미지 API 를 같이 낸다.
전처리 계산은 capture_test.py 의 함수를 그대로 쓴다. CNN 인식은 PC 에서 하지 않고 보드 결과만 쓴다.
보드는 캡처마다 {"type":"tx_capture", ...} 한 줄을 보낸다 (capture_ctrl.c).
"""
import argparse
import copy
import json
import re
import sys
import threading
import time
import traceback
from collections import deque
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
    ("cnn28", "CNN 입력 28×28", "bbox 를 22×22 에 맞추고(줄이는 축은 구간 최댓값) 28×28 가운데에 배치한 CNN 입력."),
]
CENTER_STAGES = [
    ("roi", "ROI 112×112", "정중앙 224×224 를 112×112 로 nearest 다운스케일한 영상."),
    ("threshold", "threshold + bbox", "luma → 반전 → threshold. 빨간 상자는 글자의 bbox."),
    ("cnn28", "CNN 입력 28×28", "bbox 를 22×22 에 맞추고(줄이는 축은 구간 최댓값) 28×28 가운데에 배치한 CNN 입력."),
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
    try:
        from serial.tools import list_ports
    except ImportError:
        print("UART: pyserial 이 없어 COM 포트를 찾지 못했습니다 (pip install pyserial)")
        return
    for p in list_ports.comports():
        print(f"UART {p.device}: {p.description}")


def letter(cls):
    return chr(ord("A") + cls) if isinstance(cls, int) and 0 <= cls < 26 else "?"


BOARD_TYPES = ("tx_capture", "recognition")
CELL_LINE = re.compile(r"^capture: cell (\d+)$")                   # cnn_ctrl_report 앞 줄
ARUCO_FAIL_LINE = re.compile(r"^capture: ArUco crop failed \((-?\d+)\)")


def parse_board_line(line):
    """보드 UART 한 줄 -> tx_capture / recognition dict. 다른 줄(텍스트 로그 등)은 None."""
    line = line.strip()
    if not line.startswith("{"):
        return None
    try:
        o = json.loads(line)
    except ValueError:
        return None
    if not isinstance(o, dict) or o.get("type") not in BOARD_TYPES:
        return None
    return o


class Board:
    """보드 UART 를 읽어 마지막 캡처 결과 하나를 보관한다. 다음 캡처가 오면 바뀐다.

    결과는 두 가지 줄로 만든다.
      tx_capture  : 캡처 1회 요약 (capture_ctrl.c, 새 펌웨어). 실패, CPU 결과, 시간까지 있다.
      recognition : 칸마다 "capture: cell k" 다음에 오는 줄 (cnn_ctrl_report, 이전 펌웨어도 보냄).
    새 펌웨어는 tx_capture 를 먼저 보내고 recognition 을 칸마다 보내므로 둘을 같은 캡처로 묶는다.
    """

    KEYS = {"c"}        # 웹에서 보낼 수 있는 키. 'p' 는 카메라를 멈추므로 막는다.

    def __init__(self, args, on_result):
        import serial   # --uart 를 줄 때만 pyserial 이 필요하다
        self.serial_mod, self.port, self.baud = serial, args.uart, args.baud
        self.on_result, self.lock = on_result, threading.Lock()
        self.ser, self.error = None, None
        self.log = deque(maxlen=200)
        self.last, self.count = None, 0
        self.cell = None        # 바로 앞 "capture: cell k" 의 k
        self.awaiting = False   # tx_capture 를 받았고 그 캡처의 recognition 줄을 기다리는 중
        threading.Thread(target=self._run, daemon=True).start()

    def _run(self):
        buf = b""
        while True:
            if self.ser is None:
                try:
                    # serial_for_url: COM5 같은 포트 이름과 시험용 loop:// 를 둘 다 연다
                    self.ser = self.serial_mod.serial_for_url(self.port, self.baud, timeout=0.2)
                    self.error = None
                except (self.serial_mod.SerialException, OSError) as e:
                    self.error = f"{self.port} 를 열 수 없습니다 (Vitis 터미널 등 다른 프로그램을 닫으세요): {e}"
                    time.sleep(2)
                    continue
            try:
                buf += self.ser.read(self.ser.in_waiting or 1)
            except (self.serial_mod.SerialException, OSError) as e:
                self.error = f"{self.port} 연결이 끊겼습니다: {e}"
                self._close()
                time.sleep(2)
                continue
            while b"\n" in buf:
                raw, buf = buf.split(b"\n", 1)
                self._line(raw.decode("utf-8", errors="replace").rstrip("\r"))
            if len(buf) > 8192:         # 줄바꿈 없는 쓰레기 데이터
                buf = b""

    def _line(self, line):
        with self.lock:
            self.log.append(line)
        try:
            self._parse(line.strip())
        except Exception:               # 한 줄이 잘못돼도 읽기 스레드가 죽지 않게 한다
            traceback.print_exc()
            with self.lock:
                self.log.append(f"[server] 이 줄을 처리하지 못했습니다: {line[:80]}")

    def _set(self, result):
        with self.lock:
            self.count += 1
            result["id"] = self.count   # seq 는 보드 리셋 때 다시 1 부터라 화면 갱신 판단은 id 로
            self.last = result

    def _parse(self, line):
        m = CELL_LINE.match(line)
        if m:
            self.cell = int(m.group(1))
            return
        m = ARUCO_FAIL_LINE.match(line)
        if m:       # 이전 펌웨어는 이 텍스트 줄만 보낸다. 새 펌웨어는 바로 뒤 tx_capture 가 덮어쓴다.
            self.awaiting = False
            self._set(self.on_result(dict(type="tx_capture", ok=False, stage="aruco",
                                          rc=int(m.group(1)), cell=-1)))
            return
        o = parse_board_line(line)
        if o is None:
            return
        if o["type"] == "tx_capture":
            self.awaiting = bool(o.get("cells"))
            self._set(self.on_result(o))
            return
        # recognition: 칸 하나의 PL 결과
        k, self.cell = self.cell, None
        if k is None or (k == 0 and not self.awaiting):
            # 새 캡처 (이전 펌웨어), 또는 칸 번호 없는 'd' 테스트 프레임
            self._set(self.on_result(dict(type="tx_capture", ok=True, cells=[])))
        self.awaiting = False
        k = k or 0
        with self.lock:
            r = self.last
            if not any(c["cell"] == k for c in r["cells"]):
                r["cells"].append(make_cell(k, o.get("class_id"), None, None))
                r["id"] = self.count = self.count + 1     # 칸이 늘어난 것도 화면 갱신

    def _close(self):
        try:
            self.ser.close()
        except Exception:
            pass
        self.ser = None

    def send(self, key):
        if key not in self.KEYS or self.ser is None:
            return False
        try:
            self.ser.write(key.encode("ascii"))
        except (self.serial_mod.SerialException, OSError):
            return False
        return True

    def snapshot(self, n_log=40):
        with self.lock:
            last = copy.deepcopy(self.last)
            log = list(self.log)[-n_log:]
        return dict(enabled=True, port=self.port, connected=self.ser is not None, error=self.error,
                    last=last, log=log)


class Source:
    """캡처보드(또는 저장한 프레임)를 계속 읽어 가장 최근 프레임과 fps 를 보관한다."""

    def __init__(self, args):
        self.args, self.lock = args, threading.Lock()
        self.frame, self.fps, self.error = None, 0.0, None
        self.still, self.cap = None, None
        if args.image:
            self.kind = "image"
            self.still = cv2.imread(args.image, cv2.IMREAD_COLOR)
            if self.still is None:
                raise SystemExit(f"이미지를 읽을 수 없습니다: {args.image}")
            self.name = f"image {Path(args.image).name}"
        elif args.device is not None:
            self.kind, self.name = "device", f"device {args.device}"
            # macOS 는 카메라 권한 요청을 메인 스레드에서만 띄울 수 있어서 여기서 연다
            self.cap = self._open()
        else:
            self.kind, self.name = "browser", "브라우저 캡처"
            self.t_prev = time.perf_counter()
            return
        threading.Thread(target=self._run, daemon=True).start()

    def push(self, frame):
        """브라우저가 보낸 프레임을 최근 프레임으로 둔다."""
        now = time.perf_counter()
        with self.lock:
            self.fps = 0.9 * self.fps + 0.1 / max(now - self.t_prev, 1e-6)
            self.frame, self.error = frame, None
        self.t_prev = now

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
        self.board = Board(args, board_result) if args.uart else None

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
    T = ct.TILE
    row_w = aruco_c.NUM_CELLS * T + (aruco_c.NUM_CELLS - 1) * TILE_GAP   # 안내 문구도 칸 줄과 같은 크기로
    if ar is None:
        return message_tile("ArUco 계산 대기 중", row_w, T)
    if ar["cnn"] is None:
        return message_tile(f"crop 실패: {aruco_c.ERR_NAME[ar['rc']]}  (마커 {len(ar['markers'])}/6)", row_w, T)
    idx = [s[0] for s in ARUCO_STAGES].index(stage)
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


def board_result(o):
    """보드 tx_capture 한 줄 -> 화면에 보일 캡처 결과."""
    cells = [make_cell(c.get("cell"), c.get("pl"), c.get("cpu"), c.get("pl_us"))
             for c in o.get("cells", [])]
    rc = o.get("rc")
    return dict(seq=o.get("seq"), ok=bool(o.get("ok")), stage=o.get("stage"), rc=rc,
                rc_name=aruco_c.ERR_NAME.get(rc) if o.get("stage") == "aruco" else None,
                cell=o.get("cell"), cells=cells, e2e_us=o.get("e2e_us"),
                at=time.strftime("%H:%M:%S"))


def make_cell(k, pl, cpu, pl_us):
    """칸 하나의 보드 결과. cpu/pl_us 는 recognition 줄만 있으면 None."""
    return dict(cell=k, pl=pl, pl_char=letter(pl), cpu=cpu, cpu_char=None if cpu is None else letter(cpu),
                pl_us=pl_us)


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
                    source=state.src.name, kind=state.src.kind, error=state.src.error, fps=round(fps, 1), mode=state.mode,
                    frame=None if raw is None else [raw.shape[1], raw.shape[0]], aruco=aruco,
                    stages={m: [dict(id=i, label=l, desc=d) for i, l, d in s] for m, s in STAGES.items()},
                    cells=aruco_c.NUM_CELLS))
            if path == "/api/frame.jpg":
                raw, fps, res = state.snapshot()
                if raw is None:
                    wait = "캡처보드 켜기를 누르세요" if state.src.kind == "browser" else "프레임 대기 중"
                    return self.send_bytes(encode(message_tile(state.src.error or wait), ".jpg"), "image/jpeg")
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
            if path == "/api/board":
                return self.send_json(state.board.snapshot() if state.board else dict(enabled=False))
            if path == "/api/cnn.txt":
                cell = min(max(int(q.get("cell", ["0"])[0]), 0), aruco_c.NUM_CELLS - 1)
                return self.send_bytes(cnn_text(state, cell).encode("utf-8"), "text/plain; charset=utf-8")
            self.send_bytes(b"not found", "text/plain", 404)

        def do_POST(self):
            u = urlparse(self.path)
            n = int(self.headers.get("Content-Length", 0))
            data = self.rfile.read(n)
            if u.path == "/api/frame" and state.src.kind == "browser":
                frame = cv2.imdecode(np.frombuffer(data, np.uint8), cv2.IMREAD_COLOR)
                if frame is None:
                    return self.send_bytes(b"bad frame", "text/plain", 400)
                state.src.push(frame)
                return self.send_bytes(b"ok", "text/plain")
            body = json.loads(data or b"{}")
            if u.path == "/api/mode" and body.get("mode") in STAGES:
                state.mode = body["mode"]
                return self.send_json(dict(mode=state.mode))
            if u.path == "/api/capture":
                files = save_capture(state)
                return self.send_json(dict(files=files, dir=str(ct.SAVE_DIR)), 200 if files else 503)
            if u.path == "/api/board/key":
                ok = state.board is not None and state.board.send(body.get("key", ""))
                return self.send_json(dict(ok=ok), 200 if ok else 503)
            self.send_bytes(b"not found", "text/plain", 404)

    return Handler


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--device", type=int, help="서버가 직접 여는 캡처보드 번호. 생략하면 브라우저가 캡처보드를 연다")
    ap.add_argument("--width", type=int, default=ct.IN_W)
    ap.add_argument("--height", type=int, default=ct.IN_H)
    ap.add_argument("--fps", type=int, default=60)
    ap.add_argument("--no-mjpg", action="store_true", help="MJPG 요청을 끈다 (화면이 안 나오면 시도)")
    ap.add_argument("--image", help="캡처보드 대신 저장한 프레임(PNG 등)을 계속 보여 준다")
    ap.add_argument("--port", type=int, default=8766)
    ap.add_argument("--uart", help="보드 UART COM 포트 (예: COM5). 주면 보드 인식 결과를 읽는다 (pyserial 필요)")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--list", action="store_true", help="0~4번 영상 장치와 COM 포트를 보여 주고 종료")
    args = ap.parse_args()
    if args.list:
        list_devices()
        return
    state = State(args)
    print(f"LightLetter 송신 UI: http://localhost:{args.port}  ({state.src.name})", flush=True)
    print(f"보드 UART: {args.uart} {args.baud} 8N1" if args.uart else "보드 UART: 사용 안 함 (--uart COMx)", flush=True)
    ThreadingHTTPServer(("127.0.0.1", args.port), make_handler(state)).serve_forever()


if __name__ == "__main__":
    main()
