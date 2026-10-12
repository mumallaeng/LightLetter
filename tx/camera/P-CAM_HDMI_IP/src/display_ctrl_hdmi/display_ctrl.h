/*
 *  display_ctrl.h
 *
 *  Video Timing Controller setup for the HDMI output path.
 *
 *  ---------------------------------------------------------------------
 *  WHAT WAS REMOVED FROM THE PZ7020 VERSION
 *
 *  The PZ7020 copy of this file also owned the pixel clock. It carried a
 *  dynClkAddr, called ClkFindParams() / ClkFindReg() / ClkWriteReg() /
 *  ClkStart(), and reprogrammed an axi_dynclk MMCM every time the mode
 *  changed. The whole dynclk/ directory existed to serve it.
 *
 *  In this design the pixel clock is a fixed 74.25 MHz from a plain Clocking
 *  Wizard, and rgb2dvi generates its own 5x serial clock internally. Nothing
 *  in software touches a clock. So :
 *
 *      - dynClkAddr and pxlFreq are gone from DisplayCtrl
 *      - DisplayInitialize() lost its dynClkAddr argument
 *      - the entire dynclk/ module is absent from this project
 *
 *  What is left is the VTC, which is genuinely all this file was ever about.
 *
 *  THE COST OF THAT SIMPLIFICATION : this build is 720p only. Passing a
 *  different VideoMode to DisplaySetMode() will program the VTC for it, but
 *  the pixel clock stays at 74.25 MHz, so the timing no longer matches the
 *  mode and the monitor loses sync. That is a useful thing to demonstrate
 *  once - it makes visible what axi_dynclk was actually buying.
 */

#ifndef DISPLAY_CTRL_H_
#define DISPLAY_CTRL_H_

#include "xil_types.h"
#include "xvtc.h"
#include "lcd_modes.h"

#define DISPLAY_NUM_FRAMES 1

typedef enum {
    DISPLAY_STOPPED = 0,
    DISPLAY_RUNNING = 1
} DisplayState;

typedef struct {
    XVtc         vtc;       /* VTC driver struct                            */
    VideoMode    vMode;     /* current video mode                           */
    DisplayState state;     /* whether the generator is running             */
} DisplayCtrl;

int DisplayInitialize(DisplayCtrl *dispPtr, u16 vtcId);
int DisplaySetMode(DisplayCtrl *dispPtr, const VideoMode *newMode);
int DisplayStart(DisplayCtrl *dispPtr);
int DisplayStop(DisplayCtrl *dispPtr);

#endif /* DISPLAY_CTRL_H_ */
