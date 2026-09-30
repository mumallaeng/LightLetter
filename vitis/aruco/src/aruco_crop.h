#ifndef ARUCO_CROP_H
#define ARUCO_CROP_H

#include <stdint.h>

#define ARUCO_CELL_COUNT   5
#define ARUCO_CELL_W       112
#define ARUCO_CELL_H       112
#define ARUCO_CELL_BYTES   (ARUCO_CELL_W * ARUCO_CELL_H * 3)   /* 37632 */

#define ARUCO_OK            0
#define ARUCO_ERR_ARG      -1
#define ARUCO_ERR_MARKERS  -2
#define ARUCO_ERR_GEOM     -3

typedef struct {
    const uint8_t *rgb;
    int width, height, stride;
} aruco_frame_t;

/* 5 contiguous 112x112x3 images, cell 0..4 left to right */
typedef struct {
    uint8_t pix[ARUCO_CELL_COUNT][ARUCO_CELL_H][ARUCO_CELL_W][3];
} aruco_cells_t;

typedef struct {
    int    n_markers;
    int    marker_mask;
    int    cell_ok[ARUCO_CELL_COUNT];
    double fit_rms_px;
    double center[6][2];
} aruco_result_t;

/* cells from marker centers (raw frame pixels, bit i of mask = marker i present) */
int aruco_crop_from_centers(const aruco_frame_t *f, const double centers[6][2], int mask,
                            aruco_cells_t *out, aruco_result_t *res);

int aruco_crop_run(const aruco_frame_t *f, aruco_cells_t *out, aruco_result_t *res);

#endif
