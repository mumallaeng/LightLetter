#ifndef VDMA_API_H_
#define VDMA_API_H_

/*
 *  vdma_api.h
 *
 *  AXI VDMA 를 프레임버퍼로 쓰기 위한 얇은 래퍼. 본체는 Xilinx 예제
 *  vdma_api.c 를 거의 그대로 쓴 것입니다.
 *
 *  ---------------------------------------------------------------------
 *  인코딩에 대한 기록
 *
 *  이 파일은 원래 GB2312(중국어) 로 인코딩되어 있었습니다. 아래 세 개의
 *  enum 주석이 중국어였고, 그래서 편집기를 UTF-8 로 맞춰도 이 파일만
 *  깨져 보였습니다. 다른 소스는 전부 UTF-8 이라 멀쩡했는데 이 하나가
 *  섞여 있어서 "한글 주석이 깨진다" 로 보였던 것입니다.
 *
 *  원문과 뜻은 그대로 두고 한국어로 옮긴 뒤 UTF-8 로 다시 저장했습니다.
 *  코드는 한 글자도 바꾸지 않았습니다.
 *
 *      원문 //VDMA只开启读通道          -> VDMA 읽기 채널만 연다
 *      원문 //VDMA只开启写通道          -> VDMA 쓰기 채널만 연다
 *      원문 //同时开启VDMA写通道和读通道 -> 쓰기와 읽기를 동시에 연다
 */

/* ------------------------------------------------------------ */
/*              Include File Definitions                        */
/* ------------------------------------------------------------ */

#include "xaxivdma.h"
#include "xparameters.h"
#include "xil_exception.h"

/* ------------------------------------------------------------ */
/*              General Type Declarations                       */
/* ------------------------------------------------------------ */

/*
 *  run_vdma_frame_buffer() 에 넘길 동작 모드.
 *
 *  카메라 -> DDR -> HDMI 처럼 양방향이 다 필요하면 BOTH 를 씁니다.
 *  한쪽만 열면 반대쪽 채널은 설정도 기동도 하지 않습니다.
 */
typedef enum
{
	ONLY_READ=1,    /* VDMA 읽기 채널만 연다 (DDR -> 화면) */
	ONLY_WRITE,     /* VDMA 쓰기 채널만 연다 (카메라 -> DDR) */
	BOTH            /* 쓰기와 읽기를 동시에 연다 */
}vdma_run_mode;

/* ------------------------------------------------------------ */
/*              Procedure Declarations                          */
/* ------------------------------------------------------------ */

/*
 *  VDMA 를 프레임버퍼 모드로 설정하고 기동합니다.
 *
 *  InstancePtr        : 호출자가 들고 있는 XAxiVdma 인스턴스
 *  DeviceId           : XPAR_AXIVDMA_0_DEVICE_ID
 *  hsize              : 가로 픽셀 수. 내부에서 스트림 폭(바이트)을 곱해
 *                       실제 stride 로 환산합니다
 *  vsize              : 세로 라인 수
 *  buf_base_addr      : 프레임버퍼 시작 주소
 *  number_frame_count : 프레임 카운터 인터럽트를 쓸 때의 프레임 수
 *  enable_frm_cnt_intr: 0 이면 에러 인터럽트만 켭니다
 *  mode               : 위 enum
 *
 *  반환 : XST_SUCCESS 이면 성공
 */
int run_vdma_frame_buffer(XAxiVdma* InstancePtr, int DeviceId, int hsize,
		int vsize, int buf_base_addr, int number_frame_count,
		int enable_frm_cnt_intr,vdma_run_mode mode);

/* ------------------------------------------------------------ */

/************************************************************************/

#endif /* VDMA_API_H_ */
