#include <stdio.h>
#include "../src/aruco_crop.h"

static uint8_t frame[1280 * 720 * 3];
static aruco_cells_t cells;

/* host check. stage 0 test data (frame PPM + reference cells) will be loaded here */
int main(void)
{
    aruco_frame_t f = { frame, 1280, 720, 3840 };
    aruco_result_t res;
    int rc = aruco_crop_run(&f, &cells, &res);
    printf("aruco_crop_run rc=%d\n", rc);
    return 0;
}
