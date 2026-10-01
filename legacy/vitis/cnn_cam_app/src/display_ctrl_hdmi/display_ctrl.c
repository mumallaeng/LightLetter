/*
 *  display_ctrl.c
 *
 *  픽셀 클럭(axi_dynclk) 생성 + 비디오 타이밍(v_tc) 생성.
 *  무엇을 왜 하는지는 display_ctrl.h 의 주석을 먼저 읽으세요.
 */

#include <string.h>

#include "display_ctrl.h"
#include "xdebug.h"
#include "xil_io.h"
#include "xil_printf.h"

/*
 *  영상 출력을 멈춥니다.
 *
 *  제너레이터만 끄면 충분합니다. VTC 는 프레임 중간에서 멈추지 못하므로
 *  진행 중인 라인을 끝내고 타이밍 신호를 놓습니다. 그러면 소비자가
 *  사라지므로 VDMA 읽기 채널은 알아서 멈춥니다. 그래서 여기에 VDMA 호출이
 *  없습니다.
 *
 *  클럭은 일부러 끄지 않습니다. axi_dynclk 를 끄면 LOCKED_O 가 0 이 되어
 *  HDMI_FPGA_ML 이 리셋에 걸리고, 모니터가 아예 신호를 잃습니다. 모드를
 *  바꾸는 중에 모니터가 입력을 놓쳤다가 다시 잡느라 몇 초씩 까맣게 되는
 *  것을 피하려는 것입니다. 클럭 재설정은 DisplayStart() 가 담당합니다.
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
 *  픽셀 클럭을 만들고, VTC 를 현재 VideoMode 로 프로그래밍한 뒤 시작합니다.
 *
 *  아래 타이밍 산술은 VideoMode 의 "누적" 표현(hps/hpe/hmax 는 라인 시작
 *  부터의 누적값)을 VTC 가 원하는 포치/싱크 폭으로 바꾸는 것입니다.
 *  하나가 1 어긋나면 화면이 한 픽셀 밀리는 정도라 눈에 안 보이지만,
 *  크게 틀리면 화면이 찢어지거나 모니터가 신호를 거부합니다.
 */
int DisplayStart(DisplayCtrl *dispPtr)
{
    int               Status;
    XVtc_Timing       vtcTiming;
    XVtc_SourceSelect SourceSelect;

    if (dispPtr->state == DISPLAY_RUNNING) {
        return XST_SUCCESS;
    }

    /*-------------------------------------------------------------------
     *  1. 픽셀 클럭
     *
     *  VideoMode.freq 는 MHz 단위 double 이므로 Hz 정수로 바꿔 넘깁니다.
     *  (720p 는 74.25 MHz -> 74250000)
     *
     *  DDynClk_SetRate() 는 내부에서 disable -> DRP 재설정 -> enable 을
     *  모두 수행하고, MMCM 이 lock 될 때까지 STATUS 레지스터를 폴링합니다.
     *
     *  ※ 그 폴링에는 타임아웃이 없습니다(BSP 드라이버 구현). 아래 안내
     *     문구가 찍힌 뒤 프로그램이 멈춰 있다면 MMCM 이 lock 되지 않은
     *     것이고, 원인은 대부분 둘 중 하나입니다.
     *       - 요청한 주파수가 이 기준 클럭에서 만들 수 없는 값
     *       - REF_CLK_I 에 실제로 클럭이 안 들어오고 있음
     *-------------------------------------------------------------------*/
    dispPtr->pxlFreqHz = (u32)(dispPtr->vMode.freq * 1000000.0);

    /* xil_printf 는 축소판 printf 입니다. 확실히 지원되는 %d 로만 씁니다. */
    xil_printf("display : requesting pixel clock %d Hz (ref %d Hz) ...\r\n",
               (int)dispPtr->pxlFreqHz,
               (int)dispPtr->dynClk.RefClkFreqHz);

    Status = DDynClk_SetRate(&dispPtr->dynClk, dispPtr->pxlFreqHz);
    if (Status != XST_SUCCESS) {
        xil_printf("display : DDynClk_SetRate failed\r\n");
        return XST_FAILURE;
    }
    xil_printf("display : pixel clock locked\r\n");

    /*-------------------------------------------------------------------
     *  2. 비디오 타이밍
     *-------------------------------------------------------------------*/
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

    /* 모든 필드를 검출기(detector)가 아니라 제너레이터 레지스터에서
     * 가져오게 합니다. 이 설계의 v_tc 는 enable_detection = false 라
     * 검출부가 아예 없으므로 아래 17 개가 전부 1 이어야 합니다. */
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

    /* 제너레이터를 켜는 순간 VDMA 읽기 채널의 백프레셔가 풀립니다.
     * 이 줄이 실행되기 전에는 DDR 에서 픽셀이 한 개도 나가지 않습니다. */
    XVtc_EnableGenerator(&dispPtr->vtc);

    dispPtr->state = DISPLAY_RUNNING;

    return XST_SUCCESS;
}

/*
 *  드라이버 초기화.
 *
 *  주의 : 여기서는 클럭을 만들지 않습니다. 실제 클럭 생성은
 *  DisplayStart() 에서 일어납니다. 초기화 단계에서 클럭을 켜 두면
 *  아직 VTC 타이밍이 없는 상태로 HDMI 출력단이 살아나면서 모니터가
 *  쓰레기 신호를 한 번 물었다 놓게 됩니다.
 */
int DisplayInitialize(DisplayCtrl *dispPtr, u16 vtcId, u16 dynClkId)
{
    int             Status;
    XVtc_Config    *vtcConfig;
    DDynClk_Config *dynClkConfig;

    dispPtr->state     = DISPLAY_STOPPED;
    dispPtr->vMode     = VMODE_1280x720;
    dispPtr->pxlFreqHz = 0;

    /*-------------------------------------------------------------------
     *  axi_dynclk
     *
     *  CfgInitialize 가 실패하는 경로는 사실상 하나뿐입니다 :
     *  IP 의 0x20 레지스터(기준 클럭 주파수)가 0 으로 읽히는 경우.
     *  AXI-Lite 자체가 안 붙었다는 뜻이므로, 주소 맵과 s_axi_lite_aclk /
     *  aresetn 연결을 확인해야 합니다.
     *-------------------------------------------------------------------*/
    dynClkConfig = DDynClk_LookupConfig(dynClkId);
    if (NULL == dynClkConfig) {
        xil_printf("display : dynclk LookupConfig failed (id %d)\r\n",
                   (int)dynClkId);
        return XST_FAILURE;
    }

    Status = DDynClk_CfgInitialize(&(dispPtr->dynClk), dynClkConfig,
                                   dynClkConfig->BaseAddress);
    if (Status != XST_SUCCESS) {
        xil_printf("display : dynclk CfgInitialize failed\r\n");
        xil_printf("          Reference clock register read back as 0.\r\n"
                   "          Check axi_dynclk AXI-Lite wiring and address map.\r\n");
        return XST_FAILURE;
    }

    /*-------------------------------------------------------------------
     *  v_tc
     *-------------------------------------------------------------------*/
    vtcConfig = XVtc_LookupConfig(vtcId);
    if (NULL == vtcConfig) {
        xil_printf("display : VTC LookupConfig failed (id %d)\r\n", (int)vtcId);
        return XST_FAILURE;
    }

    Status = XVtc_CfgInitialize(&(dispPtr->vtc), vtcConfig,
                                vtcConfig->BaseAddress);
    if (Status != XST_SUCCESS) {
        xil_printf("display : VTC CfgInitialize failed\r\n");
        return XST_FAILURE;
    }

    return XST_SUCCESS;
}

/*
 *  해상도 변경.
 *
 *  실제 반영은 DisplayStart() 에서 일어납니다. 이 설계는 클럭도 함께
 *  바뀌므로 다른 VideoMode 를 넣어도 정상 동작합니다.
 */
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
