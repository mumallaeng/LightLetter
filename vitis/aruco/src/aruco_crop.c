#include <math.h>
#include <string.h>
#include "aruco_config.h"
#include "aruco_crop.h"
#include "aruco_detect.h"
#include "aruco_geom.h"

/*
 * Reference: tb/aruco_crop/aruco_crop.ipynb (Python golden model)
 */

/* bilinear sample of the raw frame, coordinates clamped to the image (border replicate) */
static void sample(const aruco_frame_t *f, double x, double y, uint8_t out[3])
{
    int x0, y0, x1, y1, c;
    double fx, fy;

    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x > f->width - 1)  x = f->width - 1;
    if (y > f->height - 1) y = f->height - 1;
    x0 = (int)x; y0 = (int)y;
    x1 = x0 + 1 < f->width  ? x0 + 1 : x0;
    y1 = y0 + 1 < f->height ? y0 + 1 : y0;
    fx = x - x0; fy = y - y0;
    for (c = 0; c < 3; c++) {
        double a = f->rgb[y0 * f->stride + x0 * 3 + c] * (1 - fx) + f->rgb[y0 * f->stride + x1 * 3 + c] * fx;
        double b = f->rgb[y1 * f->stride + x0 * 3 + c] * (1 - fx) + f->rgb[y1 * f->stride + x1 * 3 + c] * fx;
        out[c] = (uint8_t)(a * (1 - fy) + b * fy + 0.5);
    }
}

/* board rect (x0,y0)-(x1,y1) -> w x h pixels written at column xoff of a 112-wide cell */
static void warp_region(const aruco_frame_t *f, const double H[9],
                        double x0, double y0, double x1, double y1,
                        int w, int h, int xoff, uint8_t cell[ARUCO_CELL_H][ARUCO_CELL_W][3])
{
    int u, v;
    for (v = 0; v < h; v++)
        for (u = 0; u < w; u++) {
            double b[2], p[2], q[2];
            b[0] = x0 + u * (x1 - x0) / w;
            b[1] = y0 + v * (y1 - y0) / h;
            perspective_transform(H, b, p);
            distort_point(p, q);
            sample(f, q[0], q[1], cell[v][xoff + u]);
        }
}

static void cell_rect(int k, double *x0, double *y0, double *x1, double *y1)
{
    *x0 = aruco_cell_x[k][0] + ARUCO_MARGIN_X;
    *x1 = aruco_cell_x[k][1] - ARUCO_MARGIN_X;
    *y0 = ARUCO_INSET;
    *y1 = 1 - ARUCO_INSET;
}

/* marker centers -> 5 cells. steps: undistort_points -> find_homography (fill missing
 * markers) -> get_perspective_transform (two pieces) -> per pixel board->H->distort->bilinear */
int aruco_crop_from_centers(const aruco_frame_t *f, const double centers[6][2], int mask,
                            aruco_cells_t *out, aruco_result_t *res)
{
    double bsrc[ARUCO_NUM_MARKERS][2], raw[ARUCO_NUM_MARKERS][2], und[ARUCO_NUM_MARKERS][2];
    double c[ARUCO_NUM_MARKERS][2], Hall[9], HL[9], HR[9], sq = 0;
    static const int idL[4] = { 0, 1, 4, 3 }, idR[4] = { 1, 2, 5, 4 };
    double qs[4][2], qd[4][2];
    int i, k, n = 0, ids[ARUCO_NUM_MARKERS];

    if (!f || !f->rgb || !centers || !out || f->width != ARUCO_FRAME_W || f->height != ARUCO_FRAME_H)
        return ARUCO_ERR_ARG;
    memset(out, 0, sizeof(*out));
    if (res)
        memset(res, 0, sizeof(*res));

    for (i = 0; i < ARUCO_NUM_MARKERS; i++)
        if (mask & (1 << i)) {
            ids[n] = i;
            bsrc[n][0] = aruco_board[i][0]; bsrc[n][1] = aruco_board[i][1];
            raw[n][0] = centers[i][0]; raw[n][1] = centers[i][1];
            n++;
        }
    if (n < 4)
        return ARUCO_ERR_MARKERS;

    undistort_points((const double (*)[2])raw, und, n);
    if (find_homography((const double (*)[2])bsrc, (const double (*)[2])und, n, Hall) != 0)
        return ARUCO_ERR_GEOM;

    /* missing markers are projected with the all-marker fit */
    for (i = 0; i < ARUCO_NUM_MARKERS; i++)
        perspective_transform(Hall, aruco_board[i], c[i]);
    for (k = 0; k < n; k++) {
        double d[2];
        c[ids[k]][0] = und[k][0]; c[ids[k]][1] = und[k][1];
        perspective_transform(Hall, bsrc[k], d);
        sq += (d[0] - und[k][0]) * (d[0] - und[k][0]) + (d[1] - und[k][1]) * (d[1] - und[k][1]);
    }

    for (i = 0; i < 4; i++) {
        qs[i][0] = aruco_board[idL[i]][0]; qs[i][1] = aruco_board[idL[i]][1];
        qd[i][0] = c[idL[i]][0];           qd[i][1] = c[idL[i]][1];
    }
    if (get_perspective_transform(qs, qd, HL) != 0)
        return ARUCO_ERR_GEOM;
    for (i = 0; i < 4; i++) {
        qs[i][0] = aruco_board[idR[i]][0]; qs[i][1] = aruco_board[idR[i]][1];
        qd[i][0] = c[idR[i]][0];           qd[i][1] = c[idR[i]][1];
    }
    if (get_perspective_transform(qs, qd, HR) != 0)
        return ARUCO_ERR_GEOM;

    for (k = 0; k < ARUCO_CELL_COUNT; k++) {
        double x0, y0, x1, y1;
        cell_rect(k, &x0, &y0, &x1, &y1);
        if (x1 <= ARUCO_SEAM_X) {
            warp_region(f, HL, x0, y0, x1, y1, ARUCO_CELL_W, ARUCO_CELL_H, 0, out->pix[k]);
        } else if (x0 >= ARUCO_SEAM_X) {
            warp_region(f, HR, x0, y0, x1, y1, ARUCO_CELL_W, ARUCO_CELL_H, 0, out->pix[k]);
        } else {
            /* cell straddles the seam, split the output width proportionally */
            int cut = (int)floor(ARUCO_CELL_W * (ARUCO_SEAM_X - x0) / (x1 - x0) + 0.5);
            warp_region(f, HL, x0, y0, ARUCO_SEAM_X, y1, cut, ARUCO_CELL_H, 0, out->pix[k]);
            warp_region(f, HR, ARUCO_SEAM_X, y0, x1, y1, ARUCO_CELL_W - cut, ARUCO_CELL_H, cut, out->pix[k]);
        }
    }

    if (res) {
        res->n_markers = n;
        res->marker_mask = mask;
        for (k = 0; k < ARUCO_CELL_COUNT; k++)
            res->cell_ok[k] = 1;
        res->fit_rms_px = sqrt(sq / n);
        memcpy(res->center, c, sizeof(res->center));
    }
    return ARUCO_OK;
}

/* frame -> 5 cells. steps: detect_markers -> centers -> undistort_points ->
 * find_homography (two pieces) -> per pixel board->H->distort->bilinear */
int aruco_crop_run(const aruco_frame_t *f, aruco_cells_t *out, aruco_result_t *res)
{
    aruco_marker_t m[ARUCO_NUM_MARKERS];
    double centers[ARUCO_NUM_MARKERS][2] = {{0}};
    int i, n, mask = 0;

    if (!f || !f->rgb || !out)
        return ARUCO_ERR_ARG;
    n = detect_markers(f->rgb, f->width, f->height, f->stride, m, ARUCO_NUM_MARKERS);
    for (i = 0; i < n; i++)
        if (m[i].id >= 0 && m[i].id < ARUCO_NUM_MARKERS) {
            centers[m[i].id][0] = m[i].center[0];
            centers[m[i].id][1] = m[i].center[1];
            mask |= 1 << m[i].id;
        }
    return aruco_crop_from_centers(f, (const double (*)[2])centers, mask, out, res);
}
