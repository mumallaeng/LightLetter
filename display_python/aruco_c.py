"""vitis/tx_fpga/tx_fpga/src/aruco/ 의 C 코드를 numpy 로 옮긴 것.

보드가 버튼 캡처 때 하는 일을 PC 에서 그대로 재현한다.
    detect_markers  (aruco_detect.c) : 적응 threshold -> 4연결 blob -> 사각형 후보
                                       -> 6x6 비트 -> DICT_4X4_50 id 0..5 -> 보드 기하로 선택
    crop_from_centers (aruco_crop.c) : 마커 중심 -> 왜곡 보정 -> homography
                                       -> 칸 5개를 112x112 로 bilinear 샘플

상수와 판정 기준은 C 와 같다. C 와 다른 점은 blob 라벨링뿐이다.
C 는 union-find 로 라벨을 붙이고 MAX_LABELS(40000)를 넘으면 픽셀을 버리지만,
여기서는 cv2.connectedComponentsWithStats(4연결)를 쓰고, 꼭짓점(극점)이 같은 점수로
여러 개일 때는 래스터 순서상 첫 픽셀을 고른다. 그래서 대각선 변이 있는 blob 은
꼭짓점이 1px 다를 수 있다.

픽셀의 채널 순서는 상관없다. gray 는 세 채널 평균이고, 크롭은 채널별로 따로
샘플하므로 BGR 프레임을 넣으면 BGR 칸이 나온다.
"""
import cv2
import numpy as np

# ---- aruco_config.h ----
FRAME_W, FRAME_H = 1280, 720

FX, FY = 976.402954, 977.507958
CX, CY = 642.357291, 247.207812
K1, K2 = -0.40054574, 0.14264536

NUM_CELLS = 5
CELL_N = 112
INSET = 0.075
MARGIN_X = 0.045
SEAM_X = 2.5

BOARD = np.array([
    [0.236500, -0.162800],
    [2.500000, -0.162800],
    [4.763500, -0.162800],
    [0.236500, 1.139300],
    [2.500000, 1.139300],
    [4.763500, 1.139300],
])

CELL_X = [
    (0.095000, 0.956000),
    (1.046000, 1.987000),
    (2.072000, 2.970000),
    (3.073000, 3.946000),
    (4.056000, 4.915000),
]

# ---- aruco_dict.h (DICT_4X4_50 중 id 0..5 만 쓴다) ----
DICT = [0xB532, 0x0F9A, 0x332D, 0x9946, 0x549E, 0x79CD]
NUM_MARKERS = 6

# ---- aruco_detect.c ----
THRESH_WINS = (23, 13)
THRESH_C = 7
MIN_AREA = 100
MIN_SIDE = 10
MAX_SIDE = 300
MAX_CAND = 256
MAX_BORDER_ERR = 7
MAX_HAMMING = 1
MIN_CONTRAST = 30
MAX_POOL = 32
MAX_PER_ID = 3
DUP_DIST = 3.0
FIT_RMS_MAX = 4.0

UNDIST_ITERS = 20

# aruco_crop.h 반환 코드
OK, ERR_ARG, ERR_MARKERS, ERR_GEOM = 0, -1, -2, -3
ERR_NAME = {OK: "OK", ERR_ARG: "ERR_ARG", ERR_MARKERS: "ERR_MARKERS", ERR_GEOM: "ERR_GEOM"}


# ======================================================================
# aruco_geom.c
# ======================================================================

def solve_linear(A, b):
    """부분 피벗 가우스 소거. 피벗이 1e-12 보다 작으면 None."""
    A = np.array(A, dtype=np.float64)
    b = np.array(b, dtype=np.float64)
    n = len(b)
    for i in range(n):
        p = i + int(np.argmax(np.abs(A[i:, i])))
        if abs(A[p, i]) < 1e-12:
            return None
        if p != i:
            A[[i, p]] = A[[p, i]]
            b[[i, p]] = b[[p, i]]
        f = A[i + 1:, i] / A[i, i]
        A[i + 1:, i:] -= f[:, None] * A[i, i:]
        b[i + 1:] -= f * b[i]
    x = np.zeros(n)
    for i in range(n - 1, -1, -1):
        x[i] = (b[i] - A[i, i + 1:] @ x[i + 1:]) / A[i, i]
    return x


def get_perspective_transform(src, dst):
    """점 4쌍에서 정확한 homography, H[2,2] = 1. 실패하면 None."""
    A = np.zeros((8, 8))
    b = np.zeros(8)
    for i in range(4):
        x, y = src[i]
        u, v = dst[i]
        A[i] = (x, y, 1, 0, 0, 0, -x * u, -y * u)
        A[i + 4] = (0, 0, 0, x, y, 1, -x * v, -y * v)
        b[i], b[i + 4] = u, v
    h = solve_linear(A, b)
    if h is None:
        return None
    return np.append(h, 1.0).reshape(3, 3)


def _normalize_points(p):
    c = p.mean(axis=0)
    d = np.sqrt(((p - c) ** 2).sum(axis=1)).mean()
    s = np.sqrt(2.0) / d if d > 0 else 1.0
    return c, s


def find_homography(src, dst):
    """최소제곱 homography (Hartley 정규화), 4점이면 정확해. 실패하면 None."""
    src = np.asarray(src, dtype=np.float64)
    dst = np.asarray(dst, dtype=np.float64)
    n = len(src)
    if n < 4:
        return None
    if n == 4:
        return get_perspective_transform(src, dst)

    (sx, sy), ss = _normalize_points(src)
    (dx, dy), ds = _normalize_points(dst)
    x = (src[:, 0] - sx) * ss
    y = (src[:, 1] - sy) * ss
    u = (dst[:, 0] - dx) * ds
    v = (dst[:, 1] - dy) * ds
    z, o = np.zeros(n), np.ones(n)
    rows = np.concatenate([
        np.stack([x, y, o, z, z, z, -x * u, -y * u, u], axis=1),
        np.stack([z, z, z, x, y, o, -x * v, -y * v, v], axis=1),
    ])
    A, r = rows[:, :8], rows[:, 8]
    h = solve_linear(A.T @ A, A.T @ r)
    if h is None:
        return None
    Hn = np.append(h, 1.0).reshape(3, 3)

    Ts = np.array([[ss, 0, -ss * sx], [0, ss, -ss * sy], [0, 0, 1]])
    Td_inv = np.array([[1 / ds, 0, dx], [0, 1 / ds, dy], [0, 0, 1]])
    H = Td_inv @ Hn @ Ts
    if abs(H[2, 2]) < 1e-12:
        return None
    return H / H[2, 2]


def perspective_transform(H, pts):
    """pts (..., 2) -> (..., 2). 분모가 0 이면 C 처럼 0 을 곱한다."""
    pts = np.asarray(pts, dtype=np.float64)
    x, y = pts[..., 0], pts[..., 1]
    w = H[2, 0] * x + H[2, 1] * y + H[2, 2]
    with np.errstate(divide="ignore"):
        w = np.where(w != 0, 1.0 / np.where(w != 0, w, 1.0), 0.0)
    return np.stack([(H[0, 0] * x + H[0, 1] * y + H[0, 2]) * w,
                     (H[1, 0] * x + H[1, 1] * y + H[1, 2]) * w], axis=-1)


def distort_points(pts):
    """이상 픽셀 -> 실제(왜곡된) 픽셀, 방사 왜곡 k1, k2."""
    pts = np.asarray(pts, dtype=np.float64)
    x = (pts[..., 0] - CX) / FX
    y = (pts[..., 1] - CY) / FY
    r2 = x * x + y * y
    f = 1 + K1 * r2 + K2 * r2 * r2
    return np.stack([x * f * FX + CX, y * f * FY + CY], axis=-1)


def undistort_points(pts):
    """실제 픽셀 -> 이상 픽셀, distort 의 고정점 반복 역변환."""
    pts = np.asarray(pts, dtype=np.float64)
    x0 = (pts[..., 0] - CX) / FX
    y0 = (pts[..., 1] - CY) / FY
    x, y = x0, y0
    for _ in range(UNDIST_ITERS):
        r2 = x * x + y * y
        f = 1 + K1 * r2 + K2 * r2 * r2
        x, y = x0 / f, y0 / f
    return np.stack([x * FX + CX, y * FY + CY], axis=-1)


# ======================================================================
# aruco_detect.c
# ======================================================================

def to_gray(frame):
    """(p0 + p1 + p2) * 21846 >> 16, 채널 순서와 무관."""
    s = frame[..., 0].astype(np.uint32) + frame[..., 1] + frame[..., 2]
    return ((s * 21846) >> 16).astype(np.uint8)


def adaptive_threshold(gray, win, c=THRESH_C):
    """평균 threshold, INV: gray <= mean - c 이면 255. 창은 영상 안쪽으로 잘린다."""
    h, w = gray.shape
    half = win // 2
    I = np.zeros((h + 1, w + 1), np.int64)
    I[1:, 1:] = gray.astype(np.int64).cumsum(0).cumsum(1)

    ys, xs = np.arange(h), np.arange(w)
    y0, y1 = np.maximum(ys - half, 0), np.minimum(ys + half, h - 1)
    x0, x1 = np.maximum(xs - half, 0), np.minimum(xs + half, w - 1)
    s = (I[(y1 + 1)[:, None], (x1 + 1)[None, :]] - I[y0[:, None], (x1 + 1)[None, :]]
         - I[(y1 + 1)[:, None], x0[None, :]] + I[y0[:, None], x0[None, :]])
    n = (y1 - y0 + 1)[:, None] * (x1 - x0 + 1)[None, :]
    mean = (s + n // 2) // n
    return np.where(gray.astype(np.int64) - mean <= -c, 255, 0).astype(np.uint8)


def _is_convex(q):
    pos = neg = 0
    for i in range(4):
        a, b, c = q[i], q[(i + 1) % 4], q[(i + 2) % 4]
        cr = (b[0] - a[0]) * (c[1] - b[1]) - (b[1] - a[1]) * (c[0] - b[0])
        if cr > 0:
            pos += 1
        elif cr < 0:
            neg += 1
    return pos == 4 or neg == 4


def _quad_area(q):
    s = 0.0
    for i in range(4):
        a, b = q[i], q[(i + 1) % 4]
        s += a[0] * b[1] - b[0] * a[1]
    return abs(s) * 0.5


def _score(k, x, y):
    return (-(x + y), x - y, x + y, -(x - y))[k]


def _scan_extremes(mask, ox, oy):
    """blob 하나에 C 의 래스터 스캔 + union-find 를 그대로 돌려 극점 4개를 얻는다.

    극점 점수가 같은 픽셀이 여럿이면 C 는 병합 순서에 따라 하나를 고르므로,
    동점이 있는 blob 만 여기로 보낸다. 다른 blob 과는 연결되지 않으니 bbox 안의
    이 blob 픽셀만 보면 C 와 같은 결과가 나온다.
    """
    h, w = mask.shape
    parent, ext = [0], [None]
    prev = [0] * w

    def root(l):
        while parent[l] != l:
            parent[l] = parent[parent[l]]
            l = parent[l]
        return l

    for y in range(h):
        cur = [0] * w
        row = mask[y]
        for x in range(w):
            if not row[x]:
                continue
            left = cur[x - 1] if x > 0 else 0
            up = prev[x]
            if not left and not up:
                lab = len(parent)
                parent.append(lab)
                ext.append([(x, y)] * 4)
            else:
                lab = root(left or up)
                if left and up:
                    rb = root(up)
                    if rb != lab:
                        parent[rb] = lab
                        ea, eb = ext[lab], ext[rb]
                        for k in range(4):
                            if _score(k, *eb[k]) > _score(k, *ea[k]):
                                ea[k] = eb[k]
                e = ext[lab]
                for k in range(4):
                    if _score(k, x, y) > _score(k, *e[k]):
                        e[k] = (x, y)
            cur[x] = lab
        prev = cur

    e = ext[root(1)]
    return [(float(x + ox), float(y + oy)) for x, y in e]


def find_marker_contours(binimg):
    """4연결 blob -> 사각형 후보. 꼭짓점 = 대각 극점 TL, TR, BR, BL."""
    nlab, labels, stats, _ = cv2.connectedComponentsWithStats(binimg, connectivity=4,
                                                               ltype=cv2.CV_32S)
    if nlab <= 1:
        return []
    bw = stats[:, cv2.CC_STAT_WIDTH]
    bh = stats[:, cv2.CC_STAT_HEIGHT]
    area = stats[:, cv2.CC_STAT_AREA]
    good = ((area >= MIN_AREA) & (bw >= MIN_SIDE) & (bh >= MIN_SIDE)
            & (bw <= MAX_SIDE) & (bh <= MAX_SIDE)
            & (bw * 5 >= bh * 2) & (bh * 5 >= bw * 2))
    good[0] = False
    if not good.any():
        return []

    # 통과한 blob 의 픽셀만 모아 극점을 찾는다 (래스터 순서 = flatnonzero 순서)
    flat = labels.ravel()
    idx = np.flatnonzero(good[flat])
    lab = flat[idx]
    py, px = np.divmod(idx, binimg.shape[1])
    scores = (-(px + py), px - py, px + py, -(px - py))

    ext, tied = {}, set()
    for k, sc in enumerate(scores):
        # 라벨별 최대 점수 픽셀. 최대 점수가 여러 픽셀이면 C 의 선택이 병합 순서에 달려 있다
        order = np.lexsort((idx, -sc, lab))
        ls, ss = lab[order], sc[order]
        head = np.r_[True, ls[1:] != ls[:-1]]
        tie = np.zeros_like(head)
        tie[:-1] = head[:-1] & (ls[1:] == ls[:-1]) & (ss[1:] == ss[:-1])
        tied.update(int(l) for l in ls[tie])
        for f in order[head]:
            ext.setdefault(int(lab[f]), [None] * 4)[k] = (float(px[f]), float(py[f]))

    for l in tied:
        x, y, w, h = stats[l, :4]
        ext[l] = _scan_extremes(labels[y:y + h, x:x + w] == l, x, y)

    cand = []
    for l in sorted(ext):
        q = ext[l]
        if not _is_convex(q) or _quad_area(q) < 0.5 * bw[l] * bh[l]:
            continue
        cand.append(np.array(q, dtype=np.float32).astype(np.float64))
        if len(cand) >= MAX_CAND:
            break
    return cand


def _otsu36(v):
    hist = np.bincount(v, minlength=256).astype(np.float64)
    t = np.arange(256, dtype=np.float64)
    n = float(len(v))
    total = float(v.sum())
    wb = hist.cumsum()
    sb = (t * hist).cumsum()
    wf = n - wb
    ok = (wb > 0) & (wf > 0)
    if not ok.any():
        return 0
    with np.errstate(divide="ignore", invalid="ignore"):
        mb = sb / wb
        mf = (total - sb) / wf
        between = wb * wf * (mb - mf) ** 2
    between = np.where(ok, between, -np.inf)
    return int(np.argmax(between))


# 6x6 모듈 중심 근처 3x3 샘플 위치 (x = j + 0.5 + off[b], y = i + 0.5 + off[a])
_OFF = np.array([-0.25, 0.0, 0.25])
_II, _JJ, _AA, _BB = np.meshgrid(np.arange(6), np.arange(6), np.arange(3), np.arange(3),
                                 indexing="ij")
_SAMPLE = np.stack([_JJ + 0.5 + _OFF[_BB], _II + 0.5 + _OFF[_AA]], axis=-1)  # (6,6,3,3,2)
_UNIT = np.array([[0, 0], [6, 0], [6, 6], [0, 6]], dtype=np.float64)
_BORDER = np.zeros((6, 6), bool)
_BORDER[[0, 5], :] = True
_BORDER[:, [0, 5]] = True


def extract_bits(gray, q):
    """6x6 모듈 -> 안쪽 4x4 코드 (bit15 = 왼쪽 위, 흰색 = 1). 실패하면 None."""
    h, w = gray.shape
    H = get_perspective_transform(_UNIT, q)
    if H is None:
        return None
    r = perspective_transform(H, _SAMPLE)
    p = np.trunc(r + 0.5).astype(np.int64)        # C 의 (int)(r + 0.5)
    if (p < 0).any() or (p[..., 0] >= w).any() or (p[..., 1] >= h).any():
        return None
    cell = (gray[p[..., 1], p[..., 0]].astype(np.int64).sum(axis=(2, 3)) // 9).astype(np.uint8)
    if int(cell.max()) - int(cell.min()) < MIN_CONTRAST:
        return None
    thr = _otsu36(cell.ravel())
    if int((cell[_BORDER] > thr).sum()) > MAX_BORDER_ERR:
        return None
    bits = (cell[1:5, 1:5] > thr).ravel()
    return int(sum(1 << (15 - k) for k in range(16) if bits[k]))


def _rotate_code(c):
    """4x4 비트를 시계 방향 90도 회전."""
    r = 0
    for i in range(4):
        for j in range(4):
            if c & (1 << (15 - (4 * (3 - j) + i))):
                r |= 1 << (15 - (4 * i + j))
    return r


def _rotations(c):
    out = []
    for _ in range(4):
        out.append(c)
        c = _rotate_code(c)
    return out


_DICT_ROT = [_rotations(c) for c in DICT]


def dictionary_identify(code):
    """해밍 거리 MAX_HAMMING 이하인 id 0..5 와 회전. 없으면 None."""
    best, best_id, best_rot = MAX_HAMMING + 1, None, 0
    for mid, rots in enumerate(_DICT_ROT):
        d, rot = 17, 0
        for r, c in enumerate(rots):
            dd = bin(code ^ c).count("1")
            if dd < d:
                d, rot = dd, r
        if d < best:
            best, best_id, best_rot = d, mid, rot
    if best_id is None:
        return None
    return best_id, best_rot


def identify_one_candidate(gray, q):
    """(id, 마커 자신의 꼭짓점 순서로 정렬한 q) 또는 None."""
    code = extract_bits(gray, q)
    if code is None:
        return None
    hit = dictionary_identify(code)
    if hit is None:
        return None
    mid, rot = hit
    return mid, np.roll(q, -rot, axis=0)        # o[k] = q[(k + rot) % 4]


def _pool_add(pool, mid, q):
    if len(pool) >= MAX_POOL:
        return
    center = q.mean(axis=0)
    same = 0
    for m in pool:
        if m["id"] != mid:
            continue
        if abs(m["center"][0] - center[0]) < DUP_DIST and abs(m["center"][1] - center[1]) < DUP_DIST:
            return
        same += 1
    if same < MAX_PER_ID:
        pool.append(dict(id=mid, corners=q.copy(), center=center))


def _fit_rms(markers):
    bsrc = BOARD[[m["id"] for m in markers]]
    und = undistort_points(np.array([m["center"] for m in markers]))
    H = find_homography(bsrc, und)
    if H is None:
        return -1.0
    d = perspective_transform(H, bsrc) - und
    return float(np.sqrt((d ** 2).sum() / len(markers)))


def select_markers(pool):
    """id 마다 후보 하나씩, 보드 기하(homography 잔차)가 맞는 가장 큰 조합."""
    idx = [[i for i, m in enumerate(pool) if m["id"] == mid] for mid in range(NUM_MARKERS)]

    if len(pool) < 4:
        return [pool[ix[0]] for ix in idx if ix]

    choice = [-1] * NUM_MARKERS
    best, best_n, best_rms = None, 0, 1e30
    while True:
        m = [pool[idx[mid][choice[mid]]] for mid in range(NUM_MARKERS) if choice[mid] >= 0]
        k = len(m)
        if k >= 4 and k >= best_n:
            r = _fit_rms(m)
            if 0 <= r <= FIT_RMS_MAX and (k > best_n or r < best_rms):
                best_n, best_rms, best = k, r, list(choice)
        # choice[id] 를 -1 .. cnt-1 로 도는 주행계
        for mid in range(NUM_MARKERS):
            choice[mid] += 1
            if choice[mid] < len(idx[mid]):
                break
            choice[mid] = -1
        else:
            break

    if best_n == 4 and len(pool) > 4:
        best_n = 0
    if not best_n:
        return []
    return [pool[idx[mid][best[mid]]] for mid in range(NUM_MARKERS) if best[mid] >= 0]


def detect_markers(frame, debug=None):
    """1280x720 프레임 -> [{'id', 'corners'(4,2), 'center'(2,)}], id 오름차순.

    debug 에 dict 를 넘기면 창별 후보 수, 풀 크기, 사용한 창을 채운다.
    """
    gray = to_gray(frame)
    pool, out = [], []
    for win in THRESH_WINS:
        cand = find_marker_contours(adaptive_threshold(gray, win))
        nid = 0
        for q in cand:
            hit = identify_one_candidate(gray, q)
            if hit is not None:
                nid += 1
                _pool_add(pool, hit[0], hit[1])
        out = select_markers(pool)
        if debug is not None:
            debug.setdefault("windows", []).append(
                dict(win=win, candidates=len(cand), identified=nid, pool=len(pool)))
        if len(out) == NUM_MARKERS:
            break
    if debug is not None:
        debug["pool"] = pool
    return out


# ======================================================================
# aruco_crop.c
# ======================================================================

def _sample(frame, x, y):
    """bilinear, 좌표는 영상 안으로 고정 (가장자리 복제)."""
    h, w = frame.shape[:2]
    x = np.clip(x, 0, w - 1)
    y = np.clip(y, 0, h - 1)
    x0 = x.astype(np.int64)
    y0 = y.astype(np.int64)
    x1 = np.where(x0 + 1 < w, x0 + 1, x0)
    y1 = np.where(y0 + 1 < h, y0 + 1, y0)
    fx = (x - x0)[..., None]
    fy = (y - y0)[..., None]
    f = frame.astype(np.float64)
    a = f[y0, x0] * (1 - fx) + f[y0, x1] * fx
    b = f[y1, x0] * (1 - fx) + f[y1, x1] * fx
    return (a * (1 - fy) + b * fy + 0.5).astype(np.uint8)


def board_to_raw(H, pts):
    """보드 좌표 -> H -> 왜곡 -> 실제 프레임 픽셀."""
    return distort_points(perspective_transform(H, pts))


def _warp_region(frame, H, x0, y0, x1, y1, w, h):
    u = np.arange(w)
    v = np.arange(h)
    bx = x0 + u * (x1 - x0) / w
    by = y0 + v * (y1 - y0) / h
    b = np.stack(np.broadcast_arrays(bx[None, :], by[:, None]), axis=-1)
    q = board_to_raw(H, b)
    return _sample(frame, q[..., 0], q[..., 1])


def cell_rect(k):
    """칸 k 의 보드 좌표 사각형 (x0, y0, x1, y1)."""
    return (CELL_X[k][0] + MARGIN_X, INSET, CELL_X[k][1] - MARGIN_X, 1 - INSET)


def crop_from_centers(frame, centers, mask):
    """마커 중심 -> 칸 5개. 반환 (rc, cells (5,112,112,3) uint8, res dict)."""
    cells = np.zeros((NUM_CELLS, CELL_N, CELL_N, 3), np.uint8)
    res = dict(n_markers=0, marker_mask=0, fit_rms_px=0.0, center=None, HL=None, HR=None)
    if frame is None or frame.shape[:2] != (FRAME_H, FRAME_W):
        return ERR_ARG, cells, res

    ids = [i for i in range(NUM_MARKERS) if mask & (1 << i)]
    n = len(ids)
    res["n_markers"] = n
    if n < 4:
        return ERR_MARKERS, cells, res

    bsrc = BOARD[ids]
    und = undistort_points(np.array([centers[i] for i in ids]))
    Hall = find_homography(bsrc, und)
    if Hall is None:
        return ERR_GEOM, cells, res

    # 안 보인 마커는 전체 맞춤으로 투영해서 채운다
    c = perspective_transform(Hall, BOARD)
    c[ids] = und
    d = perspective_transform(Hall, bsrc) - und
    sq = float((d ** 2).sum())

    HL = get_perspective_transform(BOARD[[0, 1, 4, 3]], c[[0, 1, 4, 3]])
    HR = get_perspective_transform(BOARD[[1, 2, 5, 4]], c[[1, 2, 5, 4]])
    if HL is None or HR is None:
        return ERR_GEOM, cells, res

    for k in range(NUM_CELLS):
        x0, y0, x1, y1 = cell_rect(k)
        if x1 <= SEAM_X:
            cells[k] = _warp_region(frame, HL, x0, y0, x1, y1, CELL_N, CELL_N)
        elif x0 >= SEAM_X:
            cells[k] = _warp_region(frame, HR, x0, y0, x1, y1, CELL_N, CELL_N)
        else:
            # 칸이 좌우 경계에 걸치면 출력 폭을 비율대로 나눈다
            cut = int(CELL_N * (SEAM_X - x0) / (x1 - x0) + 0.5)
            cells[k, :, :cut] = _warp_region(frame, HL, x0, y0, SEAM_X, y1, cut, CELL_N)
            cells[k, :, cut:] = _warp_region(frame, HR, SEAM_X, y0, x1, y1, CELL_N - cut, CELL_N)

    res.update(marker_mask=mask, fit_rms_px=float(np.sqrt(sq / n)), center=c, HL=HL, HR=HR)
    return OK, cells, res


def crop_run(frame, debug=None):
    """aruco_crop_run: 프레임 -> (rc, cells, res, markers)."""
    markers = detect_markers(frame, debug)
    centers = np.zeros((NUM_MARKERS, 2))
    mask = 0
    for m in markers:
        centers[m["id"]] = m["center"]
        mask |= 1 << m["id"]
    rc, cells, res = crop_from_centers(frame, centers, mask)
    return rc, cells, res, markers


def cell_outline(res, k, steps=12):
    """칸 k 의 크롭 영역 테두리 (실제 프레임 픽셀 다각형). 경계에 걸친 칸은 두 조각을 잇는다."""
    x0, y0, x1, y1 = cell_rect(k)

    def edge(H, xa, xb):
        t = np.linspace(0, 1, steps)
        top = np.stack([xa + (xb - xa) * t, np.full(steps, y0)], axis=1)
        bot = np.stack([xb + (xa - xb) * t, np.full(steps, y1)], axis=1)
        return board_to_raw(H, top), board_to_raw(H, bot)

    if x1 <= SEAM_X or x0 >= SEAM_X:
        H = res["HL"] if x1 <= SEAM_X else res["HR"]
        top, bot = edge(H, x0, x1)
        return np.concatenate([top, bot])
    lt, lb = edge(res["HL"], x0, SEAM_X)
    rt, rb = edge(res["HR"], SEAM_X, x1)
    return np.concatenate([lt, rt, rb, lb])
