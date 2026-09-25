/*
 *  main.c
 *
 *  Zybo Z7-20 + Pcam 5C (OV5640, MIPI CSI-2) -> DDR -> HDMI, 720p60.
 *  초기화 전용 최소 버전.
 *
 *  ---------------------------------------------------------------------
 *  이 파일이 하는 일
 *
 *  전원이 들어온 뒤 "카메라 영상이 HDMI로 나오는 상태"까지만 만들고,
 *  그 다음에는 아무것도 하지 않습니다. 메뉴도, 키 입력 처리도, 필터도
 *  없습니다. 마지막 for(;;) 는 정말로 비어 있습니다.
 *
 *  하드웨어가 스스로 도는 구조라서 이게 가능합니다. 초기화가 끝나면
 *  센서 -> D-PHY -> CSI-2 -> 디모자이크 -> 감마 -> VDMA -> DDR -> VDMA
 *  -> VTC/axi4s_vid_out -> rgb2dvi 까지 전부 PL 안에서 자율적으로
 *  돌아갑니다. CPU 는 한 번 설정해 준 뒤로는 할 일이 없습니다.
 *
 *  기능을 붙일 때는 아래 두 군데 중 하나에 넣게 됩니다.
 *      - 초기화 순서에 한 줄 추가          (main 안, 8번 앞)
 *      - 주기적으로 해야 할 일             (8번 super-loop 안)
 *
 *  ---------------------------------------------------------------------
 *  THE ONE THING THAT IS GENUINELY DIFFERENT : THE BRING-UP ORDER
 *
 *  PZ7020 의 DVP 는 클럭과 데이터 선이 전부라, 캡처 RTL 이 늦게 시작하면
 *  한 프레임 놓치고 다음 프레임부터 받으면 그만입니다. 순서가 거의
 *  상관없습니다.
 *
 *  MIPI CSI-2 는 패킷 프로토콜입니다. 수신기가 패킷 헤더를 봐야 어디가
 *  시작인지 압니다. 스트림 중간에 합류하면 스스로 복구하지 못합니다.
 *  그래서 아래 순서는 취향이 아니라 규칙입니다.
 *
 *      리셋   : 소비자 -> 생산자     (VDMA, CSI-2, D-PHY, 센서)
 *      설정   : 전부. 아직 멈춰 있는 상태로
 *      기동   : 생산자 -> 소비자     (D-PHY, CSI-2, ... , 센서가 마지막)
 *
 *  센서를 깨우는 OV5640_SetMode720p() 가 가장 마지막입니다. 그 시점에는
 *  하류 단계가 전부 살아서 기다리고 있습니다. 이 호출을 앞으로 옮기면
 *  리셋 버튼으로는 안 고쳐지고 전원을 뽑아야 고쳐지는 검은 화면이 나옵니다.
 *
 *  Terminal : 115200 8N1.
 */

#include "xil_types.h"
#include "xparameters.h"
#include "xil_printf.h"

#include "xaxivdma.h"

#include "cam_gpio/cam_gpio.h"
#include "iic_sccb_cfg/iic_sccb_cfg.h"
#include "ov5640/OV5640.h"
#include "mipi_rx/mipi_rx.h"
#include "gamma/gamma.h"
#include "cam_ae/cam_ae.h"
#include "vdma_api/vdma_api.h"
#include "display_ctrl_hdmi/display_ctrl.h"

#include "xuartps_hw.h"
#include "xstatus.h"
#include "capture_ctrl/capture_ctrl.h"
/*===========================================================================
 *  Platform glue
 *===========================================================================*/
#define VDMA_ID         XPAR_AXIVDMA_0_DEVICE_ID
#define DISP_VTC_ID     XPAR_VTC_0_DEVICE_ID
#define DISP_DYNCLK_ID  XPAR_DYNCLK_0_DEVICE_ID

/*
 *  프레임버퍼는 DDR 시작 + 160MB.
 *
 *  Digilent 데모와 같은 위치입니다. .elf, 스택, 힙이 절대 닿지 않는
 *  높이라서 안전합니다. DDR 에는 파티션 같은 게 없고, 코드 영역과
 *  픽셀 영역을 갈라놓는 것은 오직 이 상수 하나뿐입니다.
 *
 *  720p RGB888 3장 = 3 x 1280 x 720 x 3 = 8.3MB. 보드의 1GB 안에서
 *  여유가 충분합니다.
 */
#define FRAME_BUFFER_ADDR   (XPAR_PS7_DDR_0_S_AXI_BASEADDR + 0x0A000000)

XAxiVdma     vdma;
DisplayCtrl  dispCtrl;
VideoMode    vd_mode;

/*===========================================================================
 *  menu
 *===========================================================================*/
static void menu_help()
{
	xil_printf("\r\n--- keys ---------------------\r\n");
	xil_printf(" BTN0 : capture next complete frame \r\n");
	xil_printf(" c    : request capture from UART \r\n");
	xil_printf(" ? : help \r\n");
}

static void menu_run()
{
	char c;

	if (!XUartPs_IsReceiveData(STDIN_BASEADDRESS)) {
		return;
	}

	c = (char)XUartPs_RecvByte(STDIN_BASEADDRESS);

	switch(c)
	{
	case 'c' : capture_ctrl_trigger(); break;
	case '?' : menu_help();
	default:	break;
	}
}


/*===========================================================================
 *  main
 *===========================================================================*/
int main(void)
{
    u16 w, h;

    xil_printf("\r\n\r\n");
    xil_printf("=================================================\r\n");
    xil_printf(" Zybo Z7-20 + Pcam 5C (MIPI CSI-2) + HDMI  720p60\r\n");
    xil_printf(" init only\r\n");
    xil_printf("=================================================\r\n");

    /*-------------------------------------------------------------------
     *  1. 카메라 전원
     *
     *  가장 먼저 해야 합니다. 이 보드의 센서는 파워다운에 걸린 채로
     *  올라오는데, 그 상태에서도 SCCB 는 대답을 합니다. 이 단계를
     *  건너뛰면 칩 ID 읽기는 성공하고 화면만 영원히 까맣습니다.
     *-------------------------------------------------------------------*/
    if (cam_gpio_init() != CAM_GPIO_OK) {
        xil_printf("EMIO GPIO did not come up. Stopping.\r\n");
        xil_printf("  check : PS block has EMIO GPIO enabled with Width=1,\r\n"
                   "          GPIO_0 made external, XDC pin G20 with PULLUP,\r\n"
                   "          XSA re-exported and the platform updated.\r\n");
        return 1;
    }

    /*-------------------------------------------------------------------
     *  2. MIPI 수신기를 리셋에 붙잡아 둡니다
     *
     *  센서가 입을 열 가능성이 생기기 전에 해 둡니다.
     *-------------------------------------------------------------------*/
    mipi_rx_reset();
    mipi_rx_print_version();

    /*-------------------------------------------------------------------
     *  3. SCCB 버스
     *
     *  하드웨어 I2C 는 실패를 실제로 보고할 수 있습니다. 비트뱅잉 계층은
     *  언제나 성공했다고 말하기 때문에, 이걸 쓰는 이유의 절반이 여기에
     *  있습니다.
     *-------------------------------------------------------------------*/
    if (OV5640_Init() != IIC_SCCB_OK) {
        xil_printf("SCCB bus did not come up. Stopping.\r\n");
        xil_printf("  check : I2C 0 enabled in the PS block and routed to\r\n"
                   "          EMIO, IIC_0 made external, XDC pins F20/F19,\r\n"
                   "          XSA re-exported and the platform updated.\r\n");
        return 1;
    }

    /*-------------------------------------------------------------------
     *  4. 센서 전원 사이클 후 공통 초기화
     *
     *  OV5640_InitSensor() 는 일부러 센서를 파워다운 상태로 남겨 둡니다.
     *  깨우는 것은 6번의 SetMode720p() 입니다.
     *-------------------------------------------------------------------*/
    OV5640_PowerCycle();

    if (OV5640_InitSensor() != 0) {
        xil_printf("OV5640 detected failed!\r\n");
        xil_printf("  check : Pcam flat cable fully latched (press the\r\n"
                   "          connector down with two fingers), 5V external\r\n"
                   "          supply with JP6 on WALL, and the F20/F19 pins\r\n");
        return 1;
    }
    xil_printf("OV5640 detected successful!\r\n");

    if (iic_sccb_error_count()) {
        xil_printf("WARNING : %d SCCB error(s) during init sequence\r\n",
                   (int)iic_sccb_error_count());
        iic_sccb_clear_errors();
    }

    /*-------------------------------------------------------------------
     *  5. 영상 경로 : VDMA 를 먼저 띄웁니다
     *
     *  센서가 프레임을 뱉기 시작하는 순간 DDR 이 이미 받을 준비가
     *  되어 있어야 합니다.
     *-------------------------------------------------------------------*/
    vd_mode = VMODE_1280x720;

    run_vdma_frame_buffer(&vdma, VDMA_ID, vd_mode.width, vd_mode.height,
                          FRAME_BUFFER_ADDR, 0, 0, BOTH);

    /*-------------------------------------------------------------------
     *  6. MIPI 수신기를 풀고, 그 다음에 센서를 깨웁니다
     *
     *  이 파일 맨 위의 순서 설명이 존재하는 이유가 바로 이 부분입니다.
     *-------------------------------------------------------------------*/
    mipi_rx_enable();

    /* 감마 IP 는 10비트 -> 8비트 변환을 담당하므로 경로에서 뺄 수 없습니다.
     * 리셋 직후 값이 1.0(선형)이라 그대로도 정상 화면이 나오지만,
     * 여기서 Digilent 가 쓰는 커브를 적용합니다. */
    gamma_init();

    OV5640_SetMode720p();
    OV5640_SetAWB(AWB_ADVANCED);

    /* 노출 타겟을 OmniVision 권장값으로 내립니다.
     *
     * 이 호출이 없으면 센서 리셋 기본값(0x78/0x68)으로 도는데, 권장값보다
     * 두 배 넘게 밝아서 밝은 영역이 센서 단에서 포화됩니다. Digilent 공식
     * 코드도 이 레지스터들을 건드리지 않습니다. cam_ae.h 참고. */
    cam_ae_init();

    /*-------------------------------------------------------------------
     *  7. HDMI 출력
     *
     *  이 설계에서는 여기가 픽셀 클럭을 "만드는" 지점입니다.
     *
     *  axi_dynclk 는 소프트웨어가 설정하기 전까지 목표 주파수를 모르고,
     *  VTC 도 타이밍을 내보내지 않습니다. 따라서 DisplayStart() 가
     *  성공하기 전에는 모니터에 "신호 없음" 이 뜨는 게 정상입니다.
     *
     *  반대로 화면에 노이즈가 뜬다면 이 단계는 통과한 것이고, DDR 에
     *  아직 영상이 안 채워졌다는 뜻입니다. 카메라 쪽을 보세요.
     *-------------------------------------------------------------------*/
    if (DisplayInitialize(&dispCtrl, DISP_VTC_ID, DISP_DYNCLK_ID)
            != XST_SUCCESS) {
        xil_printf("HDMI output init failed. Stopping.\r\n");
        return 1;
    }
    DisplaySetMode(&dispCtrl, &vd_mode);
    if (DisplayStart(&dispCtrl) != XST_SUCCESS) {
        xil_printf("Pixel clock generation failed. Stopping.\r\n");
        return 1;
    }

    OV5640_GetImageInfo(&w, &h);
    xil_printf("camera  : %dx%d RAW10, 2 lane MIPI\r\n", w, h);
    xil_printf("display : %dx%d @ %d Hz (from axi_dynclk)\r\n",
               vd_mode.width, vd_mode.height, (int)dispCtrl.pxlFreqHz);
    xil_printf("frame buffer at 0x%08X\r\n", (unsigned)FRAME_BUFFER_ADDR);
    xil_printf("init done. nothing else to do.\r\n");

    if (capture_ctrl_init() != XST_SUCCESS) {
        xil_printf("Capture GPIO initialization failed. Stopping.\r\n");
        return 1;
    }

    /*-------------------------------------------------------------------
     *  8. Super-loop
     *
     *  비어 있습니다. 초기화가 끝난 뒤로 CPU 가 할 일이 없기 때문입니다.
     *  영상은 PL 이 알아서 흘려보냅니다.
     *
     *  여기에 무언가를 넣을 때의 규칙 하나 : 각 작업은 짧고 블로킹하지
     *  않아야 합니다. 여기서 기다리는 호출 하나가 루프 안의 다른 모든
     *  작업의 타이밍을 망칩니다.
     *-------------------------------------------------------------------*/
    menu_help();

    for (;;) {
        capture_ctrl_poll();
        menu_run();
    }

    /* not reached */
}
