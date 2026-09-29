"""FPGA HDMI 출력 -> 캡처보드 -> PC 화면 표시.

    python capture_test.py               # 0번 장치, 1280x720
    python capture_test.py --device 1    # 다른 장치
    python capture_test.py --list        # 0~4번 장치 탐색만 하고 종료

키
    q / ESC : 종료
    s       : 현재 프레임을 captures/ 에 PNG 로 저장
    g       : CNN crop 가이드 + 28x28 전처리 미리보기 on/off
"""
import argparse
import time
from pathlib import Path

import cv2
import numpy as np

# img_preprocess.v 와 같은 값
IN_W, IN_H = 1280, 720
OUT_W, OUT_H = 28, 28
THRESHOLD = 160
CROP = min(IN_W, IN_H)
CROP_X = (IN_W - CROP) // 2
CROP_Y = (IN_H - CROP) // 2

SAVE_DIR = Path(__file__).resolve().parent / "captures"


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


def cnn_preview(frame):
    """img_preprocess.v 를 흉내 낸 28x28: 중앙 crop -> nearest 샘플링 -> luma -> 반전 -> threshold."""
    h, w = frame.shape[:2]
    if (w, h) != (IN_W, IN_H):
        frame = cv2.resize(frame, (IN_W, IN_H), interpolation=cv2.INTER_NEAREST)
    o = np.arange(OUT_W)
    xs = CROP_X + ((2 * o + 1) * CROP) // (2 * OUT_W)
    ys = CROP_Y + ((2 * np.arange(OUT_H) + 1) * CROP) // (2 * OUT_H)
    px = frame[ys[:, None], xs[None, :]].astype(np.int32)   # BGR
    b, g, r = px[..., 0], px[..., 1], px[..., 2]
    luma = (77 * r + 150 * g + 29 * b + 128) >> 8
    inv = 255 - luma
    return np.where(inv < THRESHOLD, 0, inv).astype(np.uint8)


def draw_overlay(frame, fps, guide):
    h, w = frame.shape[:2]
    if guide:
        sx, sy = w / IN_W, h / IN_H
        p0 = (int(CROP_X * sx), int(CROP_Y * sy))
        p1 = (int((CROP_X + CROP) * sx) - 1, int((CROP_Y + CROP) * sy) - 1)
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
    cv2.namedWindow("FPGA Capture", cv2.WINDOW_NORMAL)

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
        cv2.imshow("FPGA Capture", frame)

        if guide:
            small = cnn_preview(raw)
            cv2.imshow("CNN input 28x28 (x10)",
                       cv2.resize(small, (OUT_W * 10, OUT_H * 10), interpolation=cv2.INTER_NEAREST))

        key = cv2.waitKey(1) & 0xFF
        if key in (ord("q"), 27):
            break
        if key == ord("s"):
            SAVE_DIR.mkdir(exist_ok=True)
            path = SAVE_DIR / time.strftime("capture_%Y%m%d_%H%M%S.png")
            cv2.imwrite(str(path), raw)
            print("saved", path)
        if key == ord("g"):
            guide = not guide
            if not guide:
                cv2.destroyWindow("CNN input 28x28 (x10)")

    cap.release()
    cv2.destroyAllWindows()


if __name__ == "__main__":
    main()
