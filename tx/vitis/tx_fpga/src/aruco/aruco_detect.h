#ifndef ARUCO_DETECT_H
#define ARUCO_DETECT_H

#include <stdint.h>

#define ARUCO_NUM_MARKERS  6   /* ids 0..5 */

/* candidate corners, clockwise from the top-left in image coordinates */
typedef struct { float p[4][2]; } aruco_quad_t;

typedef struct {
    int    id;
    double corners[4][2];   /* marker's own order: corner 0 = top-left of the printed marker */
    double center[2];
} aruco_marker_t;

void adaptive_threshold(const uint8_t *gray, uint8_t *bin, int w, int h, int win, int c);

/* returns the number of markers written to out, ids 0..5 only */
int  detect_markers(const uint8_t *rgb, int w, int h, int stride,
                    aruco_marker_t *out, int max_out);

#endif
