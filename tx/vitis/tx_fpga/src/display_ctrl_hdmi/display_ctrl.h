/*
 *  display_ctrl.h
 *
 *  HDMI 출력 경로 제어 : 픽셀 클럭(axi_dynclk) + 타이밍 생성(v_tc)
 *
 *  ---------------------------------------------------------------------
 *  이 설계에서 소프트웨어가 클럭을 만들어야 하는 이유
 *
 *  블록 디자인을 보면 픽셀 클럭이 어디에서도 고정되어 있지 않습니다.
 *
 *      axi_dynclk_0/REF_CLK_I      <- FCLK_CLK0 (100 MHz)
 *      axi_dynclk_0/PXL_CLK_O      -> v_tc_0/clk
 *                                     v_axi4s_vid_out_0/vid_io_out_clk
 *                                     HDMI_FPGA_ML_0/PXLCLK_I
 *      axi_dynclk_0/PXL_CLK_5X_O   -> HDMI_FPGA_ML_0/PXLCLK_5X_I
 *      axi_dynclk_0/LOCKED_O       -> HDMI_FPGA_ML_0/LOCKED_I, RST_N
 *
 *  axi_dynclk 는 MMCM 을 DRP 로 재프로그래밍하는 IP 입니다. 리셋 직후에는
 *  아무 클럭도 내보내지 않고, LOCKED_O = 0 이므로 HDMI_FPGA_ML 도 리셋에
 *  걸려 있습니다. 즉 이 파일의 DisplayStart() 가 호출되기 전까지
 *  HDMI 출력단은 통째로 정지 상태입니다.
 *
 *  → 소프트웨어가 클럭을 안 만들면 화면은 무조건 안 나옵니다.
 *    (모니터가 "신호 없음"을 띄웁니다. 노이즈조차 안 나옵니다.)
 *
 *  이전 rgb2dvi 버전과의 차이가 바로 이 지점입니다. rgb2dvi 는 고정
 *  Clocking Wizard 에서 픽셀 클럭을 받아 5배 직렬 클럭을 IP 내부에서
 *  직접 만들었기 때문에 소프트웨어가 관여할 일이 없었습니다.
 *
 *  ---------------------------------------------------------------------
 *  드라이버 선택 : BSP 의 ddynclk 를 씁니다
 *
 *  PZ7020 프로젝트는 src/dynclk/ 폴더에 Digilent 의 구형 드라이버
 *  (ClkFindParams / ClkFindReg / ClkWriteReg / ClkStart) 를 직접 넣어
 *  썼습니다. 이 플랫폼의 BSP 에는 dynclk_v1_1 (ddynclk) 이 이미 들어 있어
 *  그쪽을 씁니다. 장점이 하나 더 있습니다 :
 *
 *      DDynClk_CfgInitialize() 가 기준 클럭 주파수를 IP 의 읽기 전용
 *      레지스터(0x20)에서 직접 읽어옵니다. axi_dynclk.vhd 의 generic
 *      kRefClkFreqHz 가 slv_reg8 에 그대로 실려 있기 때문입니다.
 *      따라서 100 MHz 를 코드에 박아 넣을 필요가 없고, 나중에 BD 에서
 *      REF_CLK_I 를 바꿔도 소프트웨어는 그대로 맞습니다.
 *
 *  ---------------------------------------------------------------------
 *  해상도를 바꿀 수 있습니다
 *
 *  클럭을 소프트웨어가 만들므로, lcd_modes.h 의 다른 VideoMode 를
 *  DisplaySetMode() 에 넘기고 DisplayStart() 를 다시 부르면 실제로 그
 *  해상도로 바뀝니다. 고정 클럭 버전에서는 VTC 만 바뀌고 클럭이 안 따라와
 *  모니터가 동기를 잃었는데, 이 설계에서는 정상 동작합니다.
 */

#ifndef DISPLAY_CTRL_H_
#define DISPLAY_CTRL_H_

#include "xil_types.h"
#include "xvtc.h"
#include "ddynclk.h"        /* BSP : dynclk_v1_1 */
#include "lcd_modes.h"

#define DISPLAY_NUM_FRAMES 1

typedef enum {
    DISPLAY_STOPPED = 0,
    DISPLAY_RUNNING = 1
} DisplayState;

typedef struct {
    XVtc         vtc;        /* VTC 드라이버 인스턴스                       */
    DDynClk      dynClk;     /* axi_dynclk 드라이버 인스턴스                */
    VideoMode    vMode;      /* 현재 비디오 모드                            */
    u32          pxlFreqHz;  /* 실제로 요청한 픽셀 클럭 (Hz)                */
    DisplayState state;      /* 제너레이터 동작 여부                        */
} DisplayCtrl;

/*
 *  vtcId     : XPAR_VTC_0_DEVICE_ID
 *  dynClkId  : XPAR_DYNCLK_0_DEVICE_ID
 */
int DisplayInitialize(DisplayCtrl *dispPtr, u16 vtcId, u16 dynClkId);
int DisplaySetMode(DisplayCtrl *dispPtr, const VideoMode *newMode);
int DisplayStart(DisplayCtrl *dispPtr);
int DisplayStop(DisplayCtrl *dispPtr);

#endif /* DISPLAY_CTRL_H_ */
