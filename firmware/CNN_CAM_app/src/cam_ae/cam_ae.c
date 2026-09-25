/*
 *  cam_ae.c
 *
 *  OV5640 AE 타겟 조절. 무엇을 왜 하는지는 cam_ae.h 를 먼저 읽으세요.
 */

#include "cam_ae.h"
#include "xil_printf.h"
#include "../ov5640/OV5640.h"

/*===========================================================================
 *  레지스터
 *===========================================================================*/

/* AE 목표 밝기 */
#define REG_AEC_CTRL0F      0x3A0F      /* stable range 상한 (WPT)  */
#define REG_AEC_CTRL10      0x3A10      /* stable range 하한 (BPT)  */
#define REG_AEC_CTRL1B      0x3A1B      /* stable range 상한 (WPT2) */
#define REG_AEC_CTRL1E      0x3A1E      /* stable range 하한 (BPT2) */
#define REG_AEC_CTRL11      0x3A11      /* fast zone 상한           */
#define REG_AEC_CTRL1F      0x3A1F      /* fast zone 하한           */

/* AE 가 현재 잡고 있는 값 (읽기 전용으로 씁니다) */
#define REG_AEC_PK_EXP_H    0x3500      /* [3:0] = 노출[19:16]      */
#define REG_AEC_PK_EXP_M    0x3501      /*         노출[15:8]       */
#define REG_AEC_PK_EXP_L    0x3502      /*         노출[7:0]        */
#define REG_AEC_PK_GAIN_H   0x350A      /* [1:0] = 게인[9:8]        */
#define REG_AEC_PK_GAIN_L   0x350B      /*         게인[7:0]        */

/* 게인 상한 */
#define REG_AEC_GAIN_CEIL_H 0x3A18
#define REG_AEC_GAIN_CEIL_L 0x3A19

/*===========================================================================
 *  레벨 표
 *
 *  레벨 0  : OmniVision 권장 초기값. 출처가 분명한 값입니다.
 *  레벨 +2 : 센서 리셋 기본값. 이 모듈을 쓰기 전 우리 시스템이 있던 자리.
 *  나머지  : 그 사이를 보간한 값. 실측으로 다듬으세요.
 *
 *  각 행에서 상한(WPT) > 하한(BPT) 관계와
 *  fast zone 하한 < BPT < WPT < fast zone 상한 관계가 유지되어야 합니다.
 *===========================================================================*/
typedef struct {
    u8          wpt;        /* 0x3A0F */
    u8          bpt;        /* 0x3A10 */
    u8          wpt2;       /* 0x3A1B */
    u8          bpt2;       /* 0x3A1E */
    u8          vpt_hi;     /* 0x3A11 */
    u8          vpt_lo;     /* 0x3A1F */
    const char *name;
} Ae_target;

static const Ae_target ae_tab[AE_LEVEL_COUNT] = {
    /*  WPT   BPT   WPT2  BPT2  VPThi VPTlo */
    {  0x18, 0x10, 0x18, 0x0E, 0x30, 0x08, "-3  darkest (max highlight protection)" },
    {  0x20, 0x18, 0x20, 0x16, 0x40, 0x0C, "-2  darker"                             },
    {  0x28, 0x20, 0x28, 0x1E, 0x50, 0x10, "-1  slightly darker"                        },
    {  0x30, 0x28, 0x30, 0x26, 0x60, 0x14, " 0  OmniVision recommended"                  },
    {  0x48, 0x40, 0x48, 0x3E, 0x90, 0x20, "+1  brighter"                               },
    {  0x78, 0x68, 0x78, 0x68, 0xD0, 0x40, "+2  sensor reset default (before tuning)"    },
};

static Ae_level cur = AE_LEVEL_0;

/*===========================================================================
 *  적용
 *===========================================================================*/

void cam_ae_set(Ae_level lv)
{
    const Ae_target *t;

    if (lv >= AE_LEVEL_COUNT) {
        return;
    }
    t = &ae_tab[lv];

    /*
     * 그룹 라이트로 묶습니다. 여섯 개를 따로 쓰면 그 사이에 프레임이
     * 넘어가면서 상한만 내려가고 하한은 아직 높은 어중간한 조합이 한 프레임
     * 적용될 수 있습니다. 눈에 띄는 깜빡임이 됩니다.
     */
    OV5640_GroupBegin();
    OV5640_WriteSCCB(REG_AEC_CTRL0F, t->wpt);
    OV5640_WriteSCCB(REG_AEC_CTRL10, t->bpt);
    OV5640_WriteSCCB(REG_AEC_CTRL1B, t->wpt2);
    OV5640_WriteSCCB(REG_AEC_CTRL1E, t->bpt2);
    OV5640_WriteSCCB(REG_AEC_CTRL11, t->vpt_hi);
    OV5640_WriteSCCB(REG_AEC_CTRL1F, t->vpt_lo);
    OV5640_GroupCommit();

    cur = lv;

    xil_printf("AE target : %s\r\n", t->name);
    xil_printf("  (takes a few frames to settle - wait before judging)\r\n");
}

void cam_ae_init(void)
{
    cam_ae_set(AE_LEVEL_0);
}

void cam_ae_down(void)
{
    if (cur == AE_LEVEL_M3) {
        xil_printf("AE target : already at the darkest level (%s)\r\n", ae_tab[cur].name);
        xil_printf("  For more, dim the lighting or go to manual exposure.\r\n");
        return;
    }
    cam_ae_set((Ae_level)(cur - 1));
}

void cam_ae_up(void)
{
    if (cur == AE_LEVEL_P2) {
        xil_printf("AE target : already at the brightest level (%s)\r\n", ae_tab[cur].name);
        return;
    }
    cam_ae_set((Ae_level)(cur + 1));
}

Ae_level cam_ae_get(void)
{
    return cur;
}

const char *cam_ae_name(Ae_level lv)
{
    if (lv >= AE_LEVEL_COUNT) {
        return "?";
    }
    return ae_tab[lv].name;
}

/*===========================================================================
 *  상태 출력
 *===========================================================================*/

void cam_ae_dump(void)
{
    const Ae_target *t = &ae_tab[cur];
    u8  r0f, r10, r1b, r1e, r11, r1f;
    u32 exp_raw, exp_lines;
    u16 gain_raw, ceil_raw;
    int ok;

    xil_printf("\r\n--- AE status -----------------------------------\r\n");
    xil_printf("level : %s\r\n", t->name);

    /*-------------------------------------------------------------------
     *  목표 레지스터 되읽기
     *
     *  써 놓고 확인하지 않으면, SCCB 가 조용히 실패했을 때 "값을 바꿨는데
     *  화면이 그대로"라는 상황에서 원인을 못 찾습니다.
     *-------------------------------------------------------------------*/
    r0f = OV5640_ReadSCCB(REG_AEC_CTRL0F);
    r10 = OV5640_ReadSCCB(REG_AEC_CTRL10);
    r1b = OV5640_ReadSCCB(REG_AEC_CTRL1B);
    r1e = OV5640_ReadSCCB(REG_AEC_CTRL1E);
    r11 = OV5640_ReadSCCB(REG_AEC_CTRL11);
    r1f = OV5640_ReadSCCB(REG_AEC_CTRL1F);

    ok = (r0f == t->wpt) && (r10 == t->bpt) &&
         (r1b == t->wpt2) && (r1e == t->bpt2) &&
         (r11 == t->vpt_hi) && (r1f == t->vpt_lo);

    xil_printf("target registers (expected -> read back)\r\n");
    xil_printf("  0x3A0F WPT   0x%02X -> 0x%02X\r\n", t->wpt,    r0f);
    xil_printf("  0x3A10 BPT   0x%02X -> 0x%02X\r\n", t->bpt,    r10);
    xil_printf("  0x3A1B WPT2  0x%02X -> 0x%02X\r\n", t->wpt2,   r1b);
    xil_printf("  0x3A1E BPT2  0x%02X -> 0x%02X\r\n", t->bpt2,   r1e);
    xil_printf("  0x3A11 VPThi 0x%02X -> 0x%02X\r\n", t->vpt_hi, r11);
    xil_printf("  0x3A1F VPTlo 0x%02X -> 0x%02X\r\n", t->vpt_lo, r1f);
    xil_printf("  => %s\r\n", ok ? "match" : "!! MISMATCH - suspect SCCB");

    /*-------------------------------------------------------------------
     *  AE 가 지금 잡고 있는 값
     *
     *  노출은 20비트, 단위는 1/16 라인입니다.
     *  게인은 10비트, 실제 배율은 값/16 입니다.
     *-------------------------------------------------------------------*/
    exp_raw = ((u32)(OV5640_ReadSCCB(REG_AEC_PK_EXP_H) & 0x0F) << 16)
            | ((u32) OV5640_ReadSCCB(REG_AEC_PK_EXP_M)         <<  8)
            |  (u32) OV5640_ReadSCCB(REG_AEC_PK_EXP_L);
    exp_lines = exp_raw >> 4;

    gain_raw = ((u16)(OV5640_ReadSCCB(REG_AEC_PK_GAIN_H) & 0x03) << 8)
             |  (u16) OV5640_ReadSCCB(REG_AEC_PK_GAIN_L);

    ceil_raw = ((u16)OV5640_ReadSCCB(REG_AEC_GAIN_CEIL_H) << 8)
             |  (u16)OV5640_ReadSCCB(REG_AEC_GAIN_CEIL_L);

    xil_printf("current AE operating point\r\n");
    xil_printf("  exposure     : %d lines (raw %d, unit 1/16 line)\r\n",
               (int)exp_lines, (int)exp_raw);
    xil_printf("  gain         : %d.%02d x (raw %d)\r\n",
               (int)(gain_raw / 16), (int)((gain_raw % 16) * 100 / 16),
               (int)gain_raw);
    xil_printf("  gain ceiling : %d.%02d x (raw %d)\r\n",
               (int)(ceil_raw / 16), (int)((ceil_raw % 16) * 100 / 16),
               (int)ceil_raw);

    /*-------------------------------------------------------------------
     *  읽는 법
     *-------------------------------------------------------------------*/
    xil_printf("how to read this\r\n");
    if (gain_raw <= 20) {
        xil_printf("  Gain is near 1x, so the scene is bright enough.\r\n");
        xil_printf("  If highlights are still blown, lower the target ('a').\r\n");
    } else {
        xil_printf("  Gain is raised. AE is exposing for the dark areas and\r\n");
        xil_printf("  giving up the bright ones. Lower the target, or even\r\n");
        xil_printf("  out the lighting.\r\n");
    }
    xil_printf("-------------------------------------------------\r\n");
}
