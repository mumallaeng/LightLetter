#include "aruco_config.h"
#include "aruco_geom.h"

/*
 * Reimplemented from OpenCV 5.0.0 (Apache-2.0): getPerspectiveTransform,
 * findHomography, perspectiveTransform, undistortPoints. See THIRD_PARTY_NOTICES.md.
 */

#define UNDIST_ITERS 20

/* Newton iteration, libm is not linked in the Vitis app */
double aruco_sqrt(double x)
{
    double r;
    int i;
    if (x <= 0)
        return 0;
    r = x > 1 ? x : 1;
    for (i = 0; i < 30; i++)
        r = 0.5 * (r + x / r);
    return r;
}

/* Gauss elimination with partial pivoting. A is n x n row-major, destroyed */
static int solve_linear(int n, double *A, double *b, double *x)
{
    int i, j, k;
    for (i = 0; i < n; i++) {
        int p = i;
        double t;
        for (j = i + 1; j < n; j++)
            if (aruco_fabs(A[j * n + i]) > aruco_fabs(A[p * n + i]))
                p = j;
        if (aruco_fabs(A[p * n + i]) < 1e-12)
            return -1;
        if (p != i) {
            for (k = 0; k < n; k++) {
                t = A[i * n + k]; A[i * n + k] = A[p * n + k]; A[p * n + k] = t;
            }
            t = b[i]; b[i] = b[p]; b[p] = t;
        }
        for (j = i + 1; j < n; j++) {
            double f = A[j * n + i] / A[i * n + i];
            for (k = i; k < n; k++)
                A[j * n + k] -= f * A[i * n + k];
            b[j] -= f * b[i];
        }
    }
    for (i = n - 1; i >= 0; i--) {
        double s = b[i];
        for (k = i + 1; k < n; k++)
            s -= A[i * n + k] * x[k];
        x[i] = s / A[i * n + i];
    }
    return 0;
}

/* Calculates coefficients of perspective transformation
 * which maps (xi,yi) to (ui,vi), (i=1,2,3,4):
 *
 *      c00*xi + c01*yi + c02
 * ui = ---------------------
 *      c20*xi + c21*yi + c22
 *
 *      c10*xi + c11*yi + c12
 * vi = ---------------------
 *      c20*xi + c21*yi + c22
 *
 * Coefficients are calculated by solving one of 2 linear systems:
 * / x0 y0  1  0  0  0 -x0*u0 -y0*u0 \ /c00\ /u0\
 * | x1 y1  1  0  0  0 -x1*u1 -y1*u1 | |c01| |u1|
 * | x2 y2  1  0  0  0 -x2*u2 -y2*u2 | |c02| |u2|
 * | x3 y3  1  0  0  0 -x3*u3 -y3*u3 |.|c10|=|u3|,
 * |  0  0  0 x0 y0  1 -x0*v0 -y0*v0 | |c11| |v0|
 * |  0  0  0 x1 y1  1 -x1*v1 -y1*v1 | |c12| |v1|
 * |  0  0  0 x2 y2  1 -x2*v2 -y2*v2 | |c20| |v2|
 * \  0  0  0 x3 y3  1 -x3*v3 -y3*v3 / \c21/ \v3/
 *
 * where:
 *   cij - matrix coefficients, c22 = 1
 *
 * or
 *
 * / x0 y0  1  0  0  0 -x0*u0 -y0*u0 -u0 \ /c00\ /0\
 * | x1 y1  1  0  0  0 -x1*u1 -y1*u1 -u1 | |c01| |0|
 * | x2 y2  1  0  0  0 -x2*u2 -y2*u2 -u2 | |c02| |0|
 * | x3 y3  1  0  0  0 -x3*u3 -y3*u3 -u3 |.|c10|=|0|,
 * |  0  0  0 x0 y0  1 -x0*v0 -y0*v0 -v0 | |c11| |0|
 * |  0  0  0 x1 y1  1 -x1*v1 -y1*v1 -v1 | |c12| |0|
 * |  0  0  0 x2 y2  1 -x2*v2 -y2*v2 -v2 | |c20| |0|
 * \  0  0  0 x3 y3  1 -x3*v3 -y3*v3 -v3 / |c21| \0/
 *                                         \c22/
 *
 * where:
 *   cij - matrix coefficients, c00^2 + c01^2 + c02^2 + c10^2 + c11^2 + c12^2 + c20^2 + c21^2 + c22^2 = 1
 */
/* exact homography from 4 point pairs, H[8] = 1 (first system, "try c22 = 1") */
int get_perspective_transform(const double src[4][2], const double dst[4][2], double H[9])
{
    double A[64], b[8], h[8];
    int i;
    for (i = 0; i < 4; i++) {
        double x = src[i][0], y = src[i][1], u = dst[i][0], v = dst[i][1];
        double *r0 = A + i * 8, *r1 = A + (i + 4) * 8;
        r0[0] = x; r0[1] = y; r0[2] = 1; r0[3] = 0; r0[4] = 0; r0[5] = 0;
        r0[6] = -x * u; r0[7] = -y * u;
        r1[0] = 0; r1[1] = 0; r1[2] = 0; r1[3] = x; r1[4] = y; r1[5] = 1;
        r1[6] = -x * v; r1[7] = -y * v;
        b[i] = u; b[i + 4] = v;
    }
    if (solve_linear(8, A, b, h) != 0)
        return -1;
    for (i = 0; i < 8; i++)
        H[i] = h[i];
    H[8] = 1.0;
    return 0;
}

/* centroid shift and scale so the mean distance is sqrt(2) */
static void normalize_points(const double (*p)[2], int n, double *cx, double *cy, double *s)
{
    double d = 0;
    int i;
    *cx = *cy = 0;
    for (i = 0; i < n; i++) { *cx += p[i][0]; *cy += p[i][1]; }
    *cx /= n; *cy /= n;
    for (i = 0; i < n; i++)
        d += aruco_sqrt((p[i][0] - *cx) * (p[i][0] - *cx) + (p[i][1] - *cy) * (p[i][1] - *cy));
    d /= n;
    *s = d > 0 ? 1.41421356237 / d : 1.0;
}

/* least squares homography (method 0), Hartley normalization, H[8] = 1 */
int find_homography(const double (*src)[2], const double (*dst)[2], int n, double H[9])
{
    double AtA[64] = {0}, Atb[8] = {0}, h[8];
    double sx, sy, ss, dx, dy, ds;
    double Hn[9], t[9];
    int i, j, k, r;

    if (n < 4)
        return -1;
    if (n == 4)
        return get_perspective_transform((const double (*)[2])src, (const double (*)[2])dst, H);

    normalize_points(src, n, &sx, &sy, &ss);
    normalize_points(dst, n, &dx, &dy, &ds);
    for (i = 0; i < n; i++) {
        double x = (src[i][0] - sx) * ss, y = (src[i][1] - sy) * ss;
        double u = (dst[i][0] - dx) * ds, v = (dst[i][1] - dy) * ds;
        double row[2][9] = {
            { x, y, 1, 0, 0, 0, -x * u, -y * u, u },
            { 0, 0, 0, x, y, 1, -x * v, -y * v, v },
        };
        for (r = 0; r < 2; r++)
            for (j = 0; j < 8; j++) {
                for (k = 0; k < 8; k++)
                    AtA[j * 8 + k] += row[r][j] * row[r][k];
                Atb[j] += row[r][j] * row[r][8];
            }
    }
    if (solve_linear(8, AtA, Atb, h) != 0)
        return -1;
    for (i = 0; i < 8; i++)
        Hn[i] = h[i];
    Hn[8] = 1.0;

    /* H = Td^-1 * Hn * Ts */
    for (j = 0; j < 3; j++) {
        t[j]     = Hn[j]     * ss;          /* Hn * Ts */
        t[3 + j] = Hn[3 + j] * ss;
        t[6 + j] = Hn[6 + j] * ss;
    }
    for (i = 0; i < 3; i++) {
        t[i * 3 + 2] = Hn[i * 3 + 2] - ss * (Hn[i * 3] * sx + Hn[i * 3 + 1] * sy);
    }
    for (j = 0; j < 3; j++) {
        H[j]     = t[j] / ds + dx * t[6 + j];
        H[3 + j] = t[3 + j] / ds + dy * t[6 + j];
        H[6 + j] = t[6 + j];
    }
    if (aruco_fabs(H[8]) < 1e-12)
        return -1;
    for (i = 0; i < 9; i++)
        H[i] /= H[8];
    return 0;
}

void perspective_transform(const double H[9], const double src[2], double dst[2])
{
    double w = H[6] * src[0] + H[7] * src[1] + H[8];
    w = w != 0 ? 1.0 / w : 0.0;
    dst[0] = (H[0] * src[0] + H[1] * src[1] + H[2]) * w;
    dst[1] = (H[3] * src[0] + H[4] * src[1] + H[5]) * w;
}

/* ideal pixel -> raw pixel (radial k1, k2) */
void distort_point(const double src[2], double dst[2])
{
    double x = (src[0] - ARUCO_CX) / ARUCO_FX;
    double y = (src[1] - ARUCO_CY) / ARUCO_FY;
    double r2 = x * x + y * y;
    double f = 1 + ARUCO_K1 * r2 + ARUCO_K2 * r2 * r2;
    dst[0] = x * f * ARUCO_FX + ARUCO_CX;
    dst[1] = y * f * ARUCO_FY + ARUCO_CY;
}

/* raw pixel -> ideal pixel, fixed-point inversion of distort_point */
void undistort_points(const double (*src)[2], double (*dst)[2], int n)
{
    int i, it;
    for (i = 0; i < n; i++) {
        /* compensate distortion iteratively using fixed-point iteration */
        double x0 = (src[i][0] - ARUCO_CX) / ARUCO_FX;
        double y0 = (src[i][1] - ARUCO_CY) / ARUCO_FY;
        double x = x0, y = y0;
        for (it = 0; it < UNDIST_ITERS; it++) {
            double r2 = x * x + y * y;
            double f = 1 + ARUCO_K1 * r2 + ARUCO_K2 * r2 * r2;
            /* [x', y']^T = [(x'' - deltaX) * icdist, (y'' - deltaY) * icdist]^T
             * icdist = 1 / (1 + k1 * r^2 + k2 * r^4), tangential terms are zero here */
            x = x0 / f;
            y = y0 / f;
        }
        dst[i][0] = x * ARUCO_FX + ARUCO_CX;
        dst[i][1] = y * ARUCO_FY + ARUCO_CY;
    }
}
