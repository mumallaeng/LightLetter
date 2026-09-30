#include <math.h>
#include <string.h>
#include "aruco_detect.h"
#include "aruco_config.h"
#include "aruco_dict.h"
#include "aruco_geom.h"

/*
 * Reimplemented from OpenCV 5.0.0 (Apache-2.0): adaptiveThreshold, ArucoDetector
 * (_threshold, _findMarkerContours, detectCandidates, _extractCellPixelRatio,
 * _identifyOneCandidate) and Dictionary::identify. See THIRD_PARTY_NOTICES.md.
 *
 * Differences from OpenCV: threshold windows 23 and 13 (OpenCV sweeps 3..23 step 10),
 * connected components with diagonal extreme points instead of findContours + approxPolyDP,
 * only ids 0..5 are searched.
 */

#define MAX_LABELS     40000
#define MAX_CAND       256

#define THRESH_C       7
#define MIN_AREA       100
#define MIN_SIDE       10
#define MAX_SIDE       300
#define MAX_BORDER_ERR 7      /* maxErroneousBitsInBorderRate 0.35 of 20 border bits */
#define MAX_HAMMING    1
#define MIN_CONTRAST   30
#define MAX_POOL       32
#define MAX_PER_ID     3
#define DUP_DIST       3.0    /* same id closer than this (px) is the same marker seen in another window */
#define FIT_RMS_MAX    4.0    /* board -> image homography residual (px) of an accepted marker set */

static uint8_t  gray_buf[ARUCO_FRAME_H * ARUCO_FRAME_W];
static uint8_t  bin_buf[ARUCO_FRAME_H * ARUCO_FRAME_W];
static uint32_t integral[(ARUCO_FRAME_H + 1) * (ARUCO_FRAME_W + 1)];

/* mean threshold, THRESH_BINARY_INV: bin = 255 where gray <= mean - c */
void adaptive_threshold(const uint8_t *gray, uint8_t *bin, int w, int h, int win, int c)
{
    int W1 = w + 1, half = win / 2, x, y;

    memset(integral, 0, W1 * sizeof(uint32_t));
    for (y = 0; y < h; y++) {
        uint32_t rowsum = 0;
        integral[(y + 1) * W1] = 0;
        for (x = 0; x < w; x++) {
            rowsum += gray[y * w + x];
            integral[(y + 1) * W1 + x + 1] = integral[y * W1 + x + 1] + rowsum;
        }
    }
    for (y = 0; y < h; y++) {
        int y0 = y - half < 0 ? 0 : y - half;
        int y1 = y + half >= h ? h - 1 : y + half;
        for (x = 0; x < w; x++) {
            int x0 = x - half < 0 ? 0 : x - half;
            int x1 = x + half >= w ? w - 1 : x + half;
            uint32_t sum = integral[(y1 + 1) * W1 + x1 + 1] - integral[y0 * W1 + x1 + 1]
                         - integral[(y1 + 1) * W1 + x0] + integral[y0 * W1 + x0];
            int n = (x1 - x0 + 1) * (y1 - y0 + 1);
            int mean = (int)((sum + n / 2) / n);
            bin[y * w + x] = (gray[y * w + x] - mean <= -c) ? 255 : 0;
        }
    }
}

/* window sizes in search order, a marker merged with dark neighbours in one often separates in the other */
static const int thresh_wins[] = { 23, 13 };

static void threshold_frame(const uint8_t *gray, uint8_t *bin, int w, int h, int win)
{
    adaptive_threshold(gray, bin, w, h, win, THRESH_C);
}

/* ---- connected components with per-blob extreme points ---- */

typedef struct {
    int32_t area;
    int16_t x0, x1, y0, y1;
    int16_t e[4][2];     /* extremes: min(x+y), max(x-y), max(x+y), min(x-y) = TL, TR, BR, BL */
} blob_t;

static blob_t   blob[MAX_LABELS];
static uint16_t parent[MAX_LABELS];
static uint16_t row_a[ARUCO_FRAME_W], row_b[ARUCO_FRAME_W];

static int score(int i, int x, int y)
{
    switch (i) {
    case 0:  return -(x + y);
    case 1:  return x - y;
    case 2:  return x + y;
    default: return -(x - y);
    }
}

static void blob_add(blob_t *b, int x, int y)
{
    int i;
    b->area++;
    if (x < b->x0) b->x0 = x;
    if (x > b->x1) b->x1 = x;
    if (y < b->y0) b->y0 = y;
    if (y > b->y1) b->y1 = y;
    for (i = 0; i < 4; i++)
        if (score(i, x, y) > score(i, b->e[i][0], b->e[i][1])) {
            b->e[i][0] = x;
            b->e[i][1] = y;
        }
}

static void blob_merge(blob_t *a, const blob_t *b)
{
    int i;
    a->area += b->area;
    if (b->x0 < a->x0) a->x0 = b->x0;
    if (b->x1 > a->x1) a->x1 = b->x1;
    if (b->y0 < a->y0) a->y0 = b->y0;
    if (b->y1 > a->y1) a->y1 = b->y1;
    for (i = 0; i < 4; i++)
        if (score(i, b->e[i][0], b->e[i][1]) > score(i, a->e[i][0], a->e[i][1])) {
            a->e[i][0] = b->e[i][0];
            a->e[i][1] = b->e[i][1];
        }
}

static int find_root(int l)
{
    while (parent[l] != l) {
        parent[l] = parent[parent[l]];
        l = parent[l];
    }
    return l;
}

static int is_convex(const aruco_quad_t *q)
{
    int i, pos = 0, neg = 0;
    for (i = 0; i < 4; i++) {
        const float *a = q->p[i], *b = q->p[(i + 1) % 4], *c = q->p[(i + 2) % 4];
        float cr = (b[0] - a[0]) * (c[1] - b[1]) - (b[1] - a[1]) * (c[0] - b[0]);
        if (cr > 0) pos++;
        else if (cr < 0) neg++;
    }
    return pos == 4 || neg == 4;
}

static float quad_area(const aruco_quad_t *q)
{
    float s = 0;
    int i;
    for (i = 0; i < 4; i++) {
        const float *a = q->p[i], *b = q->p[(i + 1) % 4];
        s += a[0] * b[1] - b[0] * a[1];
    }
    return (s < 0 ? -s : s) * 0.5f;
}

/* 4-connected dark blobs -> quad candidates (own: replaces findContours + approxPolyDP) */
static int find_marker_contours(const uint8_t *bin, int w, int h, aruco_quad_t *cand, int max_cand)
{
    uint16_t *prev = row_a, *cur = row_b, *t;
    int next = 1, x, y, l, n = 0;

    memset(row_a, 0, sizeof(row_a));
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            int left, up, lab;
            if (!bin[y * w + x]) {
                cur[x] = 0;
                continue;
            }
            left = x > 0 ? cur[x - 1] : 0;
            up = prev[x];
            if (!left && !up) {
                if (next >= MAX_LABELS) {       /* too many blobs, drop the pixel */
                    cur[x] = 0;
                    continue;
                }
                lab = next++;
                parent[lab] = lab;
                memset(&blob[lab], 0, sizeof(blob_t));
                blob[lab].x0 = blob[lab].x1 = x;
                blob[lab].y0 = blob[lab].y1 = y;
                blob[lab].e[0][0] = blob[lab].e[1][0] = blob[lab].e[2][0] = blob[lab].e[3][0] = x;
                blob[lab].e[0][1] = blob[lab].e[1][1] = blob[lab].e[2][1] = blob[lab].e[3][1] = y;
                blob[lab].area = 1;
            } else {
                lab = find_root(left ? left : up);
                if (left && up) {
                    int rb = find_root(up);
                    if (rb != lab) {
                        parent[rb] = lab;
                        blob_merge(&blob[lab], &blob[rb]);
                    }
                }
                blob_add(&blob[lab], x, y);
            }
            cur[x] = lab;
        }
        t = prev; prev = cur; cur = t;
    }

    for (l = 1; l < next && n < max_cand; l++) {
        const blob_t *b;
        aruco_quad_t q;
        int bw, bh, i;
        if (parent[l] != l)
            continue;
        b = &blob[l];
        bw = b->x1 - b->x0 + 1;
        bh = b->y1 - b->y0 + 1;
        if (b->area < MIN_AREA || bw < MIN_SIDE || bh < MIN_SIDE || bw > MAX_SIDE || bh > MAX_SIDE)
            continue;
        if (bw * 5 < bh * 2 || bh * 5 < bw * 2)
            continue;
        for (i = 0; i < 4; i++) {
            q.p[i][0] = b->e[i][0];
            q.p[i][1] = b->e[i][1];
        }
        if (!is_convex(&q) || quad_area(&q) < 0.5f * bw * bh)
            continue;
        cand[n++] = q;
    }
    return n;
}

static int detect_candidates(const uint8_t *gray, int w, int h, int win, aruco_quad_t *cand, int max_cand)
{
    threshold_frame(gray, bin_buf, w, h, win);
    return find_marker_contours(bin_buf, w, h, cand, max_cand);
}

/* ---- bit extraction and dictionary lookup ---- */

static int otsu36(const uint8_t *v, int n)
{
    int hist[256] = {0}, i, t, best = 0;
    double sum = 0, sb = 0, wb = 0, maxv = -1;
    for (i = 0; i < n; i++) { hist[v[i]]++; sum += v[i]; }
    for (t = 0; t < 256; t++) {
        double wf, mb, mf, between;
        wb += hist[t];
        if (wb == 0) continue;
        wf = n - wb;
        if (wf == 0) break;
        sb += (double)t * hist[t];
        mb = sb / wb;
        mf = (sum - sb) / wf;
        between = wb * wf * (mb - mf) * (mb - mf);
        if (between > maxv) { maxv = between; best = t; }
    }
    return best;
}

/* 6x6 module grid -> 4x4 inner code, bit 15 = top-left, white = 1. returns 0 on failure */
static int extract_bits(const uint8_t *gray, int w, int h, const aruco_quad_t *q, uint16_t *code)
{
    static const double unit[4][2] = { {0, 0}, {6, 0}, {6, 6}, {0, 6} };
    static const double off[3] = { -0.25, 0, 0.25 };
    double dst[4][2], H[9];
    uint8_t cell[36];
    int i, j, a, b, lo = 255, hi = 0, thr, err = 0;

    for (i = 0; i < 4; i++) { dst[i][0] = q->p[i][0]; dst[i][1] = q->p[i][1]; }
    /* remove perspective */
    if (get_perspective_transform(unit, (const double (*)[2])dst, H) != 0)
        return 0;

    for (i = 0; i < 6; i++)
        for (j = 0; j < 6; j++) {
            int s = 0;
            /* Remove some border just to avoid border noise from perspective transformation */
            for (a = 0; a < 3; a++)
                for (b = 0; b < 3; b++) {
                    double p[2] = { j + 0.5 + off[b], i + 0.5 + off[a] }, r[2];
                    int px, py;
                    perspective_transform(H, p, r);
                    px = (int)(r[0] + 0.5);
                    py = (int)(r[1] + 0.5);
                    if (px < 0 || py < 0 || px >= w || py >= h)
                        return 0;
                    s += gray[py * w + px];
                }
            cell[i * 6 + j] = (uint8_t)(s / 9);
            if (cell[i * 6 + j] < lo) lo = cell[i * 6 + j];
            if (cell[i * 6 + j] > hi) hi = cell[i * 6 + j];
        }
    /* all pixels the same color -> nothing to decode */
    if (hi - lo < MIN_CONTRAST)
        return 0;

    /* now extract code, first threshold using Otsu */
    thr = otsu36(cell, 36);

    /* analyze border bits */
    for (i = 0; i < 6; i++)
        for (j = 0; j < 6; j++)
            if ((i == 0 || i == 5 || j == 0 || j == 5) && cell[i * 6 + j] > thr)
                err++;
    if (err > MAX_BORDER_ERR)
        return 0;

    /* take only inner bits */
    *code = 0;
    for (i = 0; i < 4; i++)
        for (j = 0; j < 4; j++)
            if (cell[(i + 1) * 6 + j + 1] > thr)
                *code |= (uint16_t)(1u << (15 - (4 * i + j)));
    return 1;
}

/* rotate the 4x4 bit grid 90 degrees clockwise */
static uint16_t rotate_code(uint16_t c)
{
    uint16_t r = 0;
    int i, j;
    for (i = 0; i < 4; i++)
        for (j = 0; j < 4; j++)
            if (c & (1u << (15 - (4 * (3 - j) + i))))
                r |= (uint16_t)(1u << (15 - (4 * i + j)));
    return r;
}

static int popcount16(uint16_t v)
{
    int n = 0;
    while (v) { n += v & 1; v >>= 1; }
    return n;
}

/* distance from code to marker id over all rotations, rotation of the best match */
static int get_distance_to_id(uint16_t code, int id, int *rot)
{
    uint16_t c = ac_dict[id];
    int r, best = 17;
    for (r = 0; r < 4; r++) {
        int d = popcount16(code ^ c);
        if (d < best) { best = d; *rot = r; }
        c = rotate_code(c);
    }
    return best;
}

/* try to identify the marker */
static int dictionary_identify(uint16_t code, int *id, int *rotation)
{
    int i, best = MAX_HAMMING + 1;
    for (i = 0; i < ARUCO_NUM_MARKERS; i++) {
        int rot, d = get_distance_to_id(code, i, &rot);
        if (d < best) {
            best = d;
            *id = i;
            *rotation = rot;
        }
    }
    return best <= MAX_HAMMING;
}

/* identifies the candidate and reorders its corners to the marker's own order */
static int identify_one_candidate(const uint8_t *gray, int w, int h, aruco_quad_t *q, int *id)
{
    uint16_t code;
    aruco_quad_t o;
    int rot, k;
    if (!extract_bits(gray, w, h, q, &code))
        return 0;
    if (!dictionary_identify(code, id, &rot))
        return 0;
    for (k = 0; k < 4; k++) {
        o.p[k][0] = q->p[(k + rot) % 4][0];
        o.p[k][1] = q->p[(k + rot) % 4][1];
    }
    *q = o;
    return 1;
}

static aruco_marker_t pool[MAX_POOL];
static int            npool;

static void pool_add(int id, const aruco_quad_t *q)
{
    aruco_marker_t *m;
    int i, k, same = 0;

    if (npool >= MAX_POOL)
        return;
    m = &pool[npool];
    m->id = id;
    m->center[0] = m->center[1] = 0;
    for (k = 0; k < 4; k++) {
        m->corners[k][0] = q->p[k][0];
        m->corners[k][1] = q->p[k][1];
        m->center[0] += q->p[k][0] / 4.0;
        m->center[1] += q->p[k][1] / 4.0;
    }
    for (i = 0; i < npool; i++) {
        if (pool[i].id != id)
            continue;
        if (fabs(pool[i].center[0] - m->center[0]) < DUP_DIST
                && fabs(pool[i].center[1] - m->center[1]) < DUP_DIST)
            return;
        same++;
    }
    if (same < MAX_PER_ID)
        npool++;
}

/* RMS residual (px) of the board -> image homography over the chosen markers, -1 if it fails */
static double fit_rms(const aruco_marker_t *const *m, int n)
{
    double bsrc[ARUCO_NUM_MARKERS][2], raw[ARUCO_NUM_MARKERS][2], und[ARUCO_NUM_MARKERS][2], H[9], sq = 0;
    int i;
    for (i = 0; i < n; i++) {
        bsrc[i][0] = aruco_board[m[i]->id][0];
        bsrc[i][1] = aruco_board[m[i]->id][1];
        raw[i][0] = m[i]->center[0];
        raw[i][1] = m[i]->center[1];
    }
    undistort_points((const double (*)[2])raw, und, n);
    if (find_homography((const double (*)[2])bsrc, (const double (*)[2])und, n, H) != 0)
        return -1;
    for (i = 0; i < n; i++) {
        double d[2];
        perspective_transform(H, bsrc[i], d);
        sq += (d[0] - und[i][0]) * (d[0] - und[i][0]) + (d[1] - und[i][1]) * (d[1] - und[i][1]);
    }
    return sqrt(sq / n);
}

/*
 * Picks at most one candidate per id so that the board geometry agrees. Tries every
 * choice (candidate k of an id, or the id absent), keeps the largest set whose
 * homography residual is within FIT_RMS_MAX, ties go to the smaller residual.
 * Four markers always fit exactly, so such a set is only accepted when it is the whole
 * pool (nothing left that could disagree). Fewer than 4 candidates cannot be checked and
 * are returned as they are (first per id).
 */
static int select_markers(aruco_marker_t *out, int max_out)
{
    int idx[ARUCO_NUM_MARKERS][MAX_PER_ID], cnt[ARUCO_NUM_MARKERS] = {0};
    int choice[ARUCO_NUM_MARKERS], best[ARUCO_NUM_MARKERS], best_n = 0, n = 0, i, id;
    double best_rms = 1e30;

    for (i = 0; i < npool; i++)
        idx[pool[i].id][cnt[pool[i].id]++] = i;

    if (npool < 4) {
        for (id = 0; id < ARUCO_NUM_MARKERS && n < max_out; id++)
            if (cnt[id])
                out[n++] = pool[idx[id][0]];
        return n;
    }

    for (id = 0; id < ARUCO_NUM_MARKERS; id++)
        choice[id] = -1;
    for (;;) {
        const aruco_marker_t *m[ARUCO_NUM_MARKERS];
        int k = 0;
        for (id = 0; id < ARUCO_NUM_MARKERS; id++)
            if (choice[id] >= 0)
                m[k++] = &pool[idx[id][choice[id]]];
        if (k >= 4 && k >= best_n) {
            double r = fit_rms(m, k);
            if (r >= 0 && r <= FIT_RMS_MAX && (k > best_n || r < best_rms)) {
                best_n = k;
                best_rms = r;
                memcpy(best, choice, sizeof(best));
            }
        }
        /* odometer over choice[id] in -1 .. cnt[id]-1 */
        for (id = 0; id < ARUCO_NUM_MARKERS; id++) {
            if (++choice[id] < cnt[id])
                break;
            choice[id] = -1;
        }
        if (id == ARUCO_NUM_MARKERS)
            break;
    }

    if (best_n == 4 && npool > 4)
        best_n = 0;
    for (id = 0; id < ARUCO_NUM_MARKERS && n < max_out; id++)
        if (best_n && best[id] >= 0)
            out[n++] = pool[idx[id][best[id]]];
    return n;
}

int detect_markers(const uint8_t *rgb, int w, int h, int stride, aruco_marker_t *out, int max_out)
{
    static aruco_quad_t cand[MAX_CAND];
    int x, y, i, wi, nc, n = 0;

    if (!rgb || !out || w > ARUCO_FRAME_W || h > ARUCO_FRAME_H)
        return 0;
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            const uint8_t *p = rgb + y * stride + x * 3;
            gray_buf[y * w + x] = (uint8_t)(((p[0] + p[1] + p[2]) * 21846) >> 16);
        }

    npool = 0;
    for (wi = 0; wi < (int)(sizeof(thresh_wins) / sizeof(thresh_wins[0])); wi++) {
        nc = detect_candidates(gray_buf, w, h, thresh_wins[wi], cand, MAX_CAND);
        for (i = 0; i < nc; i++) {
            int id;
            if (identify_one_candidate(gray_buf, w, h, &cand[i], &id))
                pool_add(id, &cand[i]);
        }
        n = select_markers(out, max_out);
        if (n == ARUCO_NUM_MARKERS)     /* all six found and consistent, skip the other windows */
            break;
    }
    return n;
}
