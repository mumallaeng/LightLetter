/*
 *  display_ctrl.c
 *
 *  VTC only. See the note in display_ctrl.h for what was taken out of the
 *  PZ7020 version and why.
 */

#include <string.h>

#include "display_ctrl.h"
#include "xdebug.h"
#include "xil_io.h"

/*
 *  Halts video output.
 *
 *  Disabling the generator is enough : the VTC cannot stop mid-frame, so it
 *  finishes the line it is on and then stops asserting timing. VDMA's read
 *  channel stalls on its own once nothing is consuming, which is why there is
 *  no VDMA call here.
 */
int DisplayStop(DisplayCtrl *dispPtr)
{
    if (dispPtr->state == DISPLAY_STOPPED) {
        return XST_SUCCESS;
    }

    XVtc_DisableGenerator(&dispPtr->vtc);
    dispPtr->state = DISPLAY_STOPPED;

    return XST_SUCCESS;
}

/*
 *  Programs the VTC from the current VideoMode and starts the generator.
 *
 *  The arithmetic below converts the VideoMode's "cumulative" description
 *  (hps, hpe, hmax are running totals from the start of the line) into the
 *  VTC's porch/sync widths. Getting one of these off by one shifts the whole
 *  picture sideways by a pixel, which is invisible; getting it wrong by more
 *  makes the image tear or the monitor refuse the signal entirely.
 */
int DisplayStart(DisplayCtrl *dispPtr)
{
    XVtc_Timing       vtcTiming;
    XVtc_SourceSelect SourceSelect;

    if (dispPtr->state == DISPLAY_RUNNING) {
        return XST_SUCCESS;
    }

    vtcTiming.HActiveVideo  = dispPtr->vMode.width;
    vtcTiming.HFrontPorch   = dispPtr->vMode.hps  - dispPtr->vMode.width;
    vtcTiming.HSyncWidth    = dispPtr->vMode.hpe  - dispPtr->vMode.hps;
    vtcTiming.HBackPorch    = dispPtr->vMode.hmax - dispPtr->vMode.hpe + 1;
    vtcTiming.HSyncPolarity = dispPtr->vMode.hpol;

    vtcTiming.VActiveVideo  = dispPtr->vMode.height;
    vtcTiming.V0FrontPorch  = dispPtr->vMode.vps  - dispPtr->vMode.height;
    vtcTiming.V0SyncWidth   = dispPtr->vMode.vpe  - dispPtr->vMode.vps;
    vtcTiming.V0BackPorch   = dispPtr->vMode.vmax - dispPtr->vMode.vpe + 1;
    vtcTiming.V1FrontPorch  = dispPtr->vMode.vps  - dispPtr->vMode.height;
    vtcTiming.V1SyncWidth   = dispPtr->vMode.vpe  - dispPtr->vMode.vps;
    vtcTiming.V1BackPorch   = dispPtr->vMode.vmax - dispPtr->vMode.vpe + 1;
    vtcTiming.VSyncPolarity = dispPtr->vMode.vpol;

    vtcTiming.Interlaced    = 0;

    /* Every field takes its value from the generator registers rather than
     * from a detector. This design has no video input to the VTC - detection
     * is switched off in the IP - so all seventeen of these must be 1. */
    memset((void *)&SourceSelect, 0, sizeof(SourceSelect));
    SourceSelect.VBlankPolSrc       = 1;
    SourceSelect.VSyncPolSrc        = 1;
    SourceSelect.HBlankPolSrc       = 1;
    SourceSelect.HSyncPolSrc        = 1;
    SourceSelect.ActiveVideoPolSrc  = 1;
    SourceSelect.ActiveChromaPolSrc = 1;
    SourceSelect.VChromaSrc         = 1;
    SourceSelect.VActiveSrc         = 1;
    SourceSelect.VBackPorchSrc      = 1;
    SourceSelect.VSyncSrc           = 1;
    SourceSelect.VFrontPorchSrc     = 1;
    SourceSelect.VTotalSrc          = 1;
    SourceSelect.HActiveSrc         = 1;
    SourceSelect.HBackPorchSrc      = 1;
    SourceSelect.HSyncSrc           = 1;
    SourceSelect.HFrontPorchSrc     = 1;
    SourceSelect.HTotalSrc          = 1;

    XVtc_SelfTest(&(dispPtr->vtc));

    XVtc_RegUpdateEnable(&(dispPtr->vtc));
    XVtc_SetGeneratorTiming(&(dispPtr->vtc), &vtcTiming);
    XVtc_SetSource(&(dispPtr->vtc), &SourceSelect);

    /* Enabling the generator is what releases backpressure on VDMA's read
     * channel. Until this line runs, no pixel leaves DDR. */
    XVtc_EnableGenerator(&dispPtr->vtc);

    dispPtr->state = DISPLAY_RUNNING;

    return XST_SUCCESS;
}

int DisplayInitialize(DisplayCtrl *dispPtr, u16 vtcId)
{
    int          Status;
    XVtc_Config *vtcConfig;

    dispPtr->state = DISPLAY_STOPPED;
    dispPtr->vMode = VMODE_1280x720;

    vtcConfig = XVtc_LookupConfig(vtcId);
    if (NULL == vtcConfig) {
        return XST_FAILURE;
    }

    Status = XVtc_CfgInitialize(&(dispPtr->vtc), vtcConfig,
                                vtcConfig->BaseAddress);
    if (Status != XST_SUCCESS) {
        return XST_FAILURE;
    }

    return XST_SUCCESS;
}

int DisplaySetMode(DisplayCtrl *dispPtr, const VideoMode *newMode)
{
    int Status;

    if (dispPtr->state == DISPLAY_RUNNING) {
        Status = DisplayStop(dispPtr);
        if (Status != XST_SUCCESS) {
            xdbg_printf(XDBG_DEBUG_GENERAL,
                        "Cannot change mode, unable to stop display %d\r\n",
                        Status);
            return XST_FAILURE;
        }
    }

    dispPtr->vMode = *newMode;

    return XST_SUCCESS;
}
