#ifndef ARUCO_GEOM_H
#define ARUCO_GEOM_H

/* geometry subset mirroring OpenCV (double precision, planar points only) */

/* libm is not linked in the Vitis app, so the few math calls are done here */
double aruco_sqrt(double x);
static inline double aruco_fabs(double x) { return x < 0 ? -x : x; }

int  get_perspective_transform(const double src[4][2], const double dst[4][2], double H[9]);
int  find_homography(const double (*src)[2], const double (*dst)[2], int n, double H[9]);
void perspective_transform(const double H[9], const double src[2], double dst[2]);
void undistort_points(const double (*src)[2], double (*dst)[2], int n);
void distort_point(const double src[2], double dst[2]);

#endif
