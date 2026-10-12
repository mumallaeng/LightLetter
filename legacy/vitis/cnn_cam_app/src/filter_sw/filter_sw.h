/*
 *  filter_sw.h
 *
 *  CPU(PS)로 프레임버퍼를 직접 처리하는 소프트웨어 필터.
 *  하드웨어(블록디자인/비트스트림) 변경은 전혀 없습니다.
 *
 *  ---------------------------------------------------------------------
 *  왜 하드웨어를 안 바꿔도 되는가
 *
 *  AXI VDMA 는 읽기 채널과 쓰기 채널이 시작주소 레지스터를 따로 가집니다.
 *
 *      XAXIVDMA_MM2S_ADDR_OFFSET 0x50   읽기(HDMI) 채널 주소 블록
 *      XAXIVDMA_S2MM_ADDR_OFFSET 0xA0   쓰기(카메라) 채널 주소 블록
 *
 *  그래서 소프트웨어만으로 이렇게 갈라놓을 수 있습니다.
 *
 *      S2MM(카메라) -> [cap0][cap1][cap2]      그대로 순환
 *                             |
 *                          CPU 읽기 -> 필터 -> CPU 쓰기
 *                             v
 *      MM2S(HDMI)   -> [disp0] 또는 [disp1]    번갈아 표시
 *
 *  NUM_FSTORES 가 3 이라는 제약도 문제가 아닙니다. 프레임스토어 개수를
 *  늘리는 게 아니라, 이미 있는 3개가 가리키는 주소만 다르게 쓰는 것이니까요.
 *
 *  부수 효과로 genlock 걱정이 사라집니다. "손 흔들면 화면이 떨리던" 그 문제는
 *  읽기와 쓰기가 같은 버퍼를 공유해서 생긴 것인데, 이제 구조적으로 분리됩니다.
 *
 *  ---------------------------------------------------------------------
 *  더블버퍼링
 *
 *  CPU 는 항상 "지금 화면에 안 나가는" 쪽에 씁니다. 다 쓴 뒤에야 MM2S 를
 *  그쪽으로 옮깁니다. 그래서 반쯤 그려진 프레임이 화면에 나가지 않습니다.
 *
 *      쓰기 중 : MM2S -> disp0 (완성된 이전 결과)
 *                CPU  -> disp1 (작업 중)
 *      완료    : MM2S -> disp1 로 전환, 다음엔 disp0 에 쓴다
 *
 *  ---------------------------------------------------------------------
 *  반드시 지켜야 하는 것 : 캐시
 *
 *  VDMA 는 HP 포트로 DDR 을 직접 읽고 씁니다. CPU 캐시를 거치지 않습니다.
 *
 *      읽기 전 : Xil_DCacheInvalidateRange()  - 캐시의 낡은 사본을 버린다
 *      쓴 뒤   : Xil_DCacheFlushRange()       - 캐시에만 있는 결과를 내려보낸다
 *
 *  이걸 빠뜨리면 필터 로직이 완벽해도 화면이 안 바뀌거나 이전 프레임이
 *  섞여 나옵니다. 원인을 찾기 아주 어려운 종류의 고장입니다.
 *
 *  ---------------------------------------------------------------------
 *  메모리 안의 바이트 순서  ★ 먼저 확인할 것
 *
 *  이 파이프라인의 AXI4-Stream 은 R-B-G 배치입니다.
 *
 *      AXI_BayerToRGB.vhd:419
 *          m_axis_video_tdata <= "00" & Red & Blue & Green;
 *
 *  AXI 는 tdata 의 최하위 바이트가 낮은 주소로 가므로, 메모리에는
 *
 *      byte 0 = tdata[7:0]   = G
 *      byte 1 = tdata[15:8]  = B
 *      byte 2 = tdata[23:16] = R
 *
 *  즉 픽셀당 [G][B][R] 이 됩니다. 흔히 예상하는 RGB 도 BGR 도 아닙니다.
 *
 *  ⚠ 이건 추론입니다. 그래서 채널 분리 필터를 먼저 넣어두었습니다.
 *    FILT_CH_R 을 걸었을 때 화면이 빨간 계열로만 남으면 맞는 것이고,
 *    초록이나 파랑으로 나오면 filter_sw.c 위쪽의 OFF_R / OFF_G / OFF_B
 *    세 줄만 고치면 나머지 필터가 전부 따라서 맞습니다.
 *
 *  ---------------------------------------------------------------------
 *  성능에 대한 기대치
 *
 *  720p 한 장이 1280 x 720 x 3 = 2.76 MB 입니다. 읽고 쓰면 프레임당
 *  5.5 MB 가 DDR 을 오갑니다. Cortex-A9 단일 코어 666 MHz 로는
 *
 *      점 연산(그레이/이진화)  : 대략 10~20 fps
 *      3x3 컨볼루션(소벨)      : 대략 2~5 fps
 *
 *  이 모듈은 매번 처리 시간을 ms 로 찍어줍니다. 같은 알고리즘을 PL 에
 *  올렸을 때의 이득이 숫자로 남습니다. 이 대비가 이 모듈의 진짜 목적입니다.
 */

#ifndef FILTER_SW_H
#define FILTER_SW_H

#include "xil_types.h"
#include "xaxivdma.h"

typedef enum {
    FILT_COPY = 0,      /* 그대로 복사 - 메모리 대역폭만 측정하는 기준선 */
    FILT_CH_R,          /* 빨강만 남김 - 바이트 순서 확인용 */
    FILT_CH_G,          /* 초록만 남김 */
    FILT_CH_B,          /* 파랑만 남김 */
    FILT_GRAY,          /* 그레이스케일 */
    FILT_BINARY,        /* 이진화 (임계값 조절 가능) */
    FILT_SOBEL,         /* 소벨 3x3 엣지 */
    FILT_COUNT
} Filt_kind;

/*
 *  vdma       : 이미 run_vdma_frame_buffer() 로 기동된 인스턴스
 *  cap_base   : S2MM 이 쓰는 캡처 버퍼 3장의 시작 주소 (= FRAME_BUFFER_ADDR)
 *  disp_base  : 표시 버퍼 2장을 놓을 시작 주소. 캡처 3장과 겹치면 안 됩니다.
 *  w, h       : 해상도
 *
 *  반환 : 0 이면 성공
 */
int         filter_sw_init(XAxiVdma *vdma, UINTPTR cap_base, UINTPTR disp_base,
                           u16 w, u16 h);

/* 필터 선택을 순환합니다. 화면은 바뀌지 않습니다 - 'f' 를 눌러야 적용됩니다. */
void        filter_sw_next(void);
Filt_kind   filter_sw_get(void);
const char *filter_sw_name(Filt_kind k);

/*
 *  지금 이 순간의 프레임을 잡아 필터를 걸고 화면에 띄웁니다(정지화면).
 *  처리 시간을 UART 로 출력합니다.
 */
void        filter_sw_apply(void);

/* 필터를 끄고 원래의 실시간 카메라 영상으로 돌아갑니다. */
void        filter_sw_live(void);

/* 이진화 임계값 조절. delta 만큼 더합니다. 0~255 로 제한됩니다. */
void        filter_sw_thresh(int delta);

/* 현재 상태를 UART 로 출력합니다. */
void        filter_sw_dump(void);

#endif /* FILTER_SW_H */
