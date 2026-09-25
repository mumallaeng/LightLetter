/*
 *  cam_ae.h
 *
 *  OV5640 자동노출(AE) 타겟 조절 — 하이라이트 보호용
 *
 *  ---------------------------------------------------------------------
 *  왜 이 모듈이 필요한가
 *
 *  Pcam 경로는 센서를 RAW 로 돌려 ISP 를 우회하지만, 노출(적분시간)과
 *  아날로그 게인은 ISP 앞단이라 그대로 살아 있습니다. 즉 AE 는 동작합니다.
 *
 *  그런데 Digilent 공식 코드와 우리 코드 모두 0x3A 대역에서 건드리는 것이
 *  이 셋뿐입니다.
 *
 *      {0x3a13, 0x43}   pre-gain
 *      {0x3a18, 0x00}   gain ceiling 상위
 *      {0x3a19, 0xf8}   gain ceiling = 248/16 = 15.5배
 *
 *  노출 타겟(0x3A0F / 0x3A10 / 0x3A1B / 0x3A1E / 0x3A11 / 0x3A1F)은
 *  한 번도 쓰이지 않아 센서 리셋 기본값 그대로 돕니다.
 *
 *      리셋 기본값   : 0x78 / 0x68 / 0x78 / 0x68 / 0xD0 / 0x40
 *      OmniVision 권장: 0x30 / 0x28 / 0x30 / 0x26 / 0x60 / 0x14
 *
 *  기본값이 권장값보다 두 배 넘게 밝습니다. AE 가 화면 평균을 이 목표에
 *  맞추려고 노출을 올리다 보니 밝은 영역이 센서에서 포화됩니다.
 *  한 번 포화된 신호는 뒤의 감마 커브로 되살릴 수 없습니다.
 *
 *  ---------------------------------------------------------------------
 *  레지스터 의미 (OV5640 표준 레지스터 맵)
 *
 *      0x3A0F  AEC CTRL0F   stable range 상한 (WPT)
 *      0x3A10  AEC CTRL10   stable range 하한 (BPT)
 *      0x3A1B  AEC CTRL1B   stable range 상한 (WPT2)
 *      0x3A1E  AEC CTRL1E   stable range 하한 (BPT2)
 *      0x3A11  AEC CTRL11   fast zone 상한
 *      0x3A1F  AEC CTRL1F   fast zone 하한
 *
 *  WPT/BPT 가 목표 밝기 창(히스테리시스)이고, fast zone 은 그 창을 크게
 *  벗어났을 때 빠르게 수렴시키는 바깥 경계입니다. 값을 내리면 목표 밝기가
 *  내려가고, 결과적으로 노출이 줄어 하이라이트가 살아납니다.
 *
 *  ⚠ 솔직한 한계 : 레벨 0(권장값)과 레벨 +2(리셋 기본값)만 출처가 분명한
 *     값입니다. 나머지 네 단계는 그 사이를 보간한 값입니다. 그래서 이 모듈은
 *     "한 번 정해서 박아넣는" 대신 UART 로 실시간으로 올렸다 내렸다 하도록
 *     만들었습니다. 화면을 보면서 맞추는 것이 확실합니다.
 *
 *  ---------------------------------------------------------------------
 *  쓰는 순서
 *
 *      cam_ae_init();          // 부팅 시 1회. 레벨 0 적용
 *      cam_ae_down();          // 어둡게 (하이라이트 보호)
 *      cam_ae_up();            // 밝게
 *      cam_ae_dump();          // 목표값 + 실제 노출/게인 확인
 *
 *  AE 가 수렴하는 데 몇 프레임 걸립니다. 바꾸고 나서 0.5초쯤 기다렸다가
 *  판단하세요.
 */

#ifndef CAM_AE_H
#define CAM_AE_H

#include "xil_types.h"

typedef enum {
    AE_LEVEL_M3 = 0,    /* 가장 어둡게 — 하이라이트 최대 보호 */
    AE_LEVEL_M2,
    AE_LEVEL_M1,
    AE_LEVEL_0,         /* OmniVision 권장값 (기본) */
    AE_LEVEL_P1,
    AE_LEVEL_P2,        /* 센서 리셋 기본값 = 이 모듈을 쓰기 전 상태 */
    AE_LEVEL_COUNT
} Ae_level;

/* 레벨 0(권장값)을 적용합니다. OV5640_SetMode720p() 뒤에 부르세요. */
void        cam_ae_init(void);

/* 지정한 레벨을 적용합니다. 범위를 벗어나면 아무것도 하지 않습니다. */
void        cam_ae_set(Ae_level lv);

/* 한 단계 어둡게 / 밝게. 끝에 닿으면 그대로 머무릅니다. */
void        cam_ae_down(void);
void        cam_ae_up(void);

Ae_level    cam_ae_get(void);
const char *cam_ae_name(Ae_level lv);

/*
 *  현재 AE 상태를 UART 로 출력합니다.
 *
 *    - 목표 레지스터 6개를 되읽어 실제로 써졌는지 확인
 *    - 지금 AE 가 잡고 있는 노출(라인 수)과 아날로그 게인(배율)
 *    - 게인 상한
 *
 *  하이라이트가 날아갈 때 게인이 1.0배 근처인데도 노출이 크면 조명이
 *  너무 밝은 것이고, 게인이 크면 AE 가 어두운 곳에 맞추느라 밝은 곳을
 *  포기하고 있는 것입니다.
 */
void        cam_ae_dump(void);

#endif /* CAM_AE_H */
