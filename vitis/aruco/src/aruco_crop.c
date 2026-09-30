#include <string.h>
#include "aruco_crop.h"
#include "aruco_detect.h"
#include "aruco_geom.h"

/*
 * Reference: tb/aruco_crop/aruco_crop.ipynb (Python golden model), pasted below.
 * Replace each block with the C version as it is implemented.
 */

/* frame -> 5 cells. steps: detect_markers -> centers -> undistort_points ->
 * find_homography (two pieces) -> per pixel board->H->distort->bilinear */
int aruco_crop_run(const aruco_frame_t *f, aruco_cells_t *out, aruco_result_t *res)
{
    if (!f || !f->rgb || !out)
        return ARUCO_ERR_ARG;
    memset(out, 0, sizeof(*out));
    if (res)
        memset(res, 0, sizeof(*res));
    /* TODO */
    return ARUCO_ERR_MARKERS;
}

// ---- tb/aruco_crop/aruco_crop.ipynb (Python) ----
// def board_to_image(found):
//     """마커 위치로 보드 -> 이미지 homography 두 개(왼쪽, 오른쪽 조각) 계산
//
//     return: HL, HR, centres, note
//     """
//     have = sorted(found)
//     if len(have) < 4:
//         return None, None, None, "마커 %d개 뿐 (최소 4개)" % len(have)
//
//     src = np.float32([BOARD[i] for i in have])
//     dst = np.float32([found[i]["c"] for i in have])
//     H, _ = cv2.findHomography(src, dst, 0)     # 0 = 최소제곱
//     if H is None:
//         return None, None, None, "homography 계산 실패"
//
//     # 빠진 마커는 위 변환으로 위치 추정
//     centres, filled = {}, []
//     for mid in BOARD:
//         if mid in found:
//             centres[mid] = found[mid]["c"]
//         else:
//             p = cv2.perspectiveTransform(np.float32([[BOARD[mid]]]), H)[0][0]
//             centres[mid] = p
//             filled.append(mid)
//
//     def quad(ids):
//         """마커 4개 대응으로 homography 하나 계산"""
//         return cv2.getPerspectiveTransform(
//             np.float32([BOARD[i] for i in ids]),
//             np.float32([centres[i] for i in ids]))
//
//     HL = quad((0, 1, 4, 3))
//     HR = quad((1, 2, 5, 4))
//     note = "마커 %d개" % len(have)
//     if filled:
//         note += ", id %s 는 추정" % ",".join(str(i) for i in filled)
//     return HL, HR, centres, note
//
//
// for path in IMAGES:
//     f = detect(to_gray(load_frame(path)))
//     _, _, _, note = board_to_image(f)
//     print("%-26s %s" % (os.path.basename(path).replace("aru_test_img_", ""), note))
//
// def cell_rect(k):
//     """칸 k의 보드 좌표 범위 (좌우는 CELL_X 안쪽에서 MARGIN_X만큼 물러남)
//
//     return: x0, y0, x1, y1
//     """
//     x0, x1 = CELL_X[k][0] + MARGIN_X, CELL_X[k][1] - MARGIN_X
//     y0, y1 = INSET, 1 - INSET
//     return x0, y0, x1, y1
//
//
// def warp_region(img, H, x0, y0, x1, y1, w, h):
//     """보드 좌표의 직사각형 영역 하나를 w x h 정면 이미지로 변환"""
//     src = np.float32([[x0, y0], [x1, y0], [x1, y1], [x0, y1]])
//     dst = np.float32([[0, 0], [w, 0], [w, h], [0, h]])
//     # 출력 -> 이미지 변환 = H · (보드 -> 출력 변환)의 역
//     M = H @ np.linalg.inv(cv2.getPerspectiveTransform(src, dst))
//     return cv2.warpPerspective(img, M, (w, h),
//                                flags=cv2.WARP_INVERSE_MAP | cv2.INTER_LINEAR,
//                                borderMode=cv2.BORDER_REPLICATE)
//
//
// def extract_cells(img, HL, HR, n=OUT_N):
//     """칸 5개를 n x n 컬러 이미지로 잘라냄
//
//     return: cells
//     """
//     cells = []
//     for k in range(NUM_CELLS):
//         x0, y0, x1, y1 = cell_rect(k)
//         if x1 <= SEAM_X:
//             cells.append(warp_region(img, HL, x0, y0, x1, y1, n, n))
//         elif x0 >= SEAM_X:
//             cells.append(warp_region(img, HR, x0, y0, x1, y1, n, n))
//         else:
//             # 이음매를 걸친 칸, 출력 폭을 비율대로 나눔
//             cut = int(round(n * (SEAM_X - x0) / (x1 - x0)))
//             left = warp_region(img, HL, x0, y0, SEAM_X, y1, cut, n)
//             right = warp_region(img, HR, SEAM_X, y0, x1, y1, n - cut, n)
//             cells.append(np.hstack([left, right]))
//     return cells
//
//
// def process(path):
//     """사진 한 장의 마커 검출부터 칸 자르기까지 실행
//
//     return: dict(bgr, gray, found, note, cells, centres, HL, HR)
//     """
//     bgr = load_frame(path)
//     gray = to_gray(bgr)
//     found = detect(gray)
//     HL, HR, centres, note = board_to_image(found)
//     if HL is None:
//         return dict(bgr=bgr, gray=gray, found=found, note=note, cells=None, centres=None)
//     return dict(bgr=bgr, gray=gray, found=found, note=note,
//                 cells=extract_cells(bgr, HL, HR), centres=centres,
//                 HL=HL, HR=HR)
//
//
// res = {os.path.basename(p).replace("aru_test_img_", "").replace(".png", ""): process(p)
//        for p in IMAGES}
// for k, v in res.items():
//     print("%-10s %s" % (k, v["note"]))