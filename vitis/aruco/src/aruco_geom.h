#ifndef ARUCO_GEOM_H
#define ARUCO_GEOM_H

/* geometry subset mirroring OpenCV (double precision, planar points only) */

int  find_homography(const double (*src)[2], const double (*dst)[2], int n, double H[9]);
void perspective_transform(const double H[9], const double src[2], double dst[2]);
void undistort_points(const double *src, double *dst, int n);
void distort_point(const double src[2], double dst[2]);

#endif
