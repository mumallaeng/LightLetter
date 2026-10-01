/* constants from tx/aruco/aruco_crop.ipynb (section 1) and calib/camera.json */
#ifndef ARUCO_CONFIG_H
#define ARUCO_CONFIG_H

#define ARUCO_FRAME_W   1280
#define ARUCO_FRAME_H   720

#define ARUCO_FX  976.402954
#define ARUCO_FY  977.507958
#define ARUCO_CX  642.357291
#define ARUCO_CY  247.207812
#define ARUCO_K1  -0.40054574
#define ARUCO_K2  0.14264536

#define ARUCO_NUM_CELLS  5
#define ARUCO_OUT_N      112
#define ARUCO_INSET      0.075000
#define ARUCO_MARGIN_X   0.045000
#define ARUCO_SEAM_X     2.500000

/* marker center in board units, index = marker id */
static const double aruco_board[6][2] = {
    { 0.236500, -0.162800 },
    { 2.500000, -0.162800 },
    { 4.763500, -0.162800 },
    { 0.236500, 1.139300 },
    { 2.500000, 1.139300 },
    { 4.763500, 1.139300 },
};

/* cell inner range (x0, x1) in board units */
static const double aruco_cell_x[5][2] = {
    { 0.095000, 0.956000 },
    { 1.046000, 1.987000 },
    { 2.072000, 2.970000 },
    { 3.073000, 3.946000 },
    { 4.056000, 4.915000 },
};

#endif
