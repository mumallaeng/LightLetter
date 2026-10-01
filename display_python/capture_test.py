"""FPGA HDMI 출력 -> 캡처보드 -> PC 화면 표시 + CNN 입력 확인.

    python capture_test.py               # 0번 장치, 1280x720
    python capture_test.py --device 1    # 다른 장치
    python capture_test.py --list        # 0~4번 장치 탐색만 하고 종료

키
    q / ESC : 종료
    s       : 현재 프레임 PNG + CNN 입력값(txt/npy)을 captures/ 에 저장
    p       : 현재 CNN 입력 28x28 값을 콘솔에 출력
    g       : ROI 가이드 + 전처리 미리보기 on/off

미리보기는 보드의 버튼 캡처 경로를 그대로 흉내 낸다.
    PS  (roi_dma.c)          : 정중앙 224x224 -> 112x112 nearest 다운스케일
    PL  (img_preprocess.v)   : luma -> 반전 -> threshold -> bbox -> 22x22 맞춤
                               -> 28x28 가운데 배치 -> x64.25 (2^-14 스케일)
캡처보드가 MJPG 로 압축한 영상에서 계산하므로 보드 안의 값과 몇 LSB 차이가
날 수 있다. 형태(bbox, 맞춤, 배치)는 같다.
"""
import argparse
import time
from pathlib import Path

import cv2
import numpy as np

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
    args = ap.parse_args()

    if args.list:
        list_devices()
        return

    cap = open_device(args.device, args.width, args.height, args.fps, not args.no_mjpg)
    if cap is None:
        print(f"캡처보드(device {args.device})를 열 수 없습니다. --list 로 번호를 확인하세요.")
        return

    print(f"device {args.device}: "
          f"{int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))}x{int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))}")
    cv2.namedWindow(WIN_CAP, cv2.WINDOW_NORMAL)

    guide = True
    fps, t_prev, fails = 0.0, time.perf_counter(), 0
    while True:
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
        draw_overlay(frame, fps, guide)
        cv2.imshow(WIN_CAP, frame)

        roi, proc, out8, cnn, info = cnn_pipeline(raw)
        if guide:
            show_preview(roi, proc, out8, info)

        key = cv2.waitKey(1) & 0xFF
        if key in (ord("q"), 27):
            break
        if key == ord("p"):
            print(format_cnn(cnn, info))
        if key == ord("s"):
            SAVE_DIR.mkdir(exist_ok=True)
            stem = SAVE_DIR / time.strftime("capture_%Y%m%d_%H%M%S")
            cv2.imwrite(f"{stem}.png", raw)
            cv2.imwrite(f"{stem}_roi112.png", roi)
            np.save(f"{stem}_cnn28.npy", cnn)
            Path(f"{stem}_cnn28.txt").write_text(format_cnn(cnn, info) + "\n",
                                                 encoding="utf-8")
            print("saved", f"{stem}.png", "+ roi112.png / cnn28.npy / cnn28.txt")
        if key == ord("g"):
            guide = not guide
            if not guide:
                cv2.destroyWindow(WIN_ROI)
                cv2.destroyWindow(WIN_CNN)

    cap.release()
    cv2.destroyAllWindows()


if __name__ == "__main__":
    main()
