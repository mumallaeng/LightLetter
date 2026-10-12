"""하드웨어 없이 UI 를 시험하기 위한 가짜 캡처 프레임 (1280x720 PNG).

    python synth_frame.py                # captures/synth_frame.png
    python synth_frame.py out.png --letters ABCDE

ArUco 마커 6개와 칸 5개의 손글씨를 보드 좌표(aruco_c.BOARD, CELL_X)에 그리고, aruco_c 의
왜곡 모델로 실제 픽셀을 만든다. 실제 보드 영상이 아니므로 값 비교용이 아니라 화면 확인용이다.
"""
import argparse
from pathlib import Path

import cv2
import numpy as np

import aruco_c as ac

PX_PER_UNIT = 230.0
ORIGIN = (65.0, 130.0)               # 보드 (0, 0) 의 이상 픽셀 위치
MARKER_SIDE = 0.32                   # 보드 단위
SCALE = 300                          # 보드 캔버스 해상도 (픽셀/단위)
X_RANGE, Y_RANGE = (-0.3, 5.3), (-0.55, 1.55)


def board_canvas(letters):
    w = round((X_RANGE[1] - X_RANGE[0]) * SCALE)
    h = round((Y_RANGE[1] - Y_RANGE[0]) * SCALE)
    img = np.full((h, w, 3), 255, np.uint8)

    def to_c(bx, by):
        return round((bx - X_RANGE[0]) * SCALE), round((by - Y_RANGE[0]) * SCALE)

    d = cv2.aruco.getPredefinedDictionary(cv2.aruco.DICT_4X4_50)
    side = round(MARKER_SIDE * SCALE)
    for mid, (bx, by) in enumerate(ac.BOARD):
        m = cv2.cvtColor(cv2.aruco.generateImageMarker(d, mid, side), cv2.COLOR_GRAY2BGR)
        cx, cy = to_c(bx, by)
        img[cy - side // 2:cy - side // 2 + side, cx - side // 2:cx - side // 2 + side] = m

    for k, ch in enumerate(letters[:ac.NUM_CELLS]):
        x0, y0, x1, y1 = ac.cell_rect(k)
        (ax, ay), (bx, by) = to_c(x0, y0), to_c(x1, y1)
        cell = np.full((by - ay, bx - ax, 3), 255, np.uint8)
        scale = (by - ay) / 55.0
        (tw, th), _ = cv2.getTextSize(ch, cv2.FONT_HERSHEY_SIMPLEX, scale, round(scale * 4))
        cv2.putText(cell, ch, ((bx - ax - tw) // 2, (by - ay + th) // 2),
                    cv2.FONT_HERSHEY_SIMPLEX, scale, (20, 20, 20), round(scale * 4))
        img[ay:by, ax:bx] = cell
        cv2.rectangle(img, (ax, ay), (bx - 1, by - 1), (200, 200, 200), 1)
    return img


def render(letters="HELLO"):
    board = board_canvas(letters)
    gy, gx = np.mgrid[0:ac.FRAME_H, 0:ac.FRAME_W]
    ideal = ac.undistort_points(np.stack([gx, gy], axis=-1).astype(np.float64))
    bx = (ideal[..., 0] - ORIGIN[0]) / PX_PER_UNIT
    by = (ideal[..., 1] - ORIGIN[1]) / PX_PER_UNIT
    mapx = ((bx - X_RANGE[0]) * SCALE).astype(np.float32)
    mapy = ((by - Y_RANGE[0]) * SCALE).astype(np.float32)
    frame = cv2.remap(board, mapx, mapy, cv2.INTER_LINEAR, borderValue=(235, 235, 235))
    return cv2.GaussianBlur(frame, (3, 3), 0.8)


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("out", nargs="?", default=str(Path(__file__).resolve().parent / "captures" / "synth_frame.png"))
    ap.add_argument("--letters", default="HELLO")
    a = ap.parse_args()
    Path(a.out).parent.mkdir(parents=True, exist_ok=True)
    cv2.imwrite(a.out, render(a.letters))
    print("wrote", a.out)
