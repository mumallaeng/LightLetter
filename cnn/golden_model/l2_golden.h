/*
 * LightLetter CNN Layer 2 (6ch -> 16ch, 3x3) cycle-accurate golden model
 *
 * RTL 블록 하나당 struct 하나(= 레지스터 묶음)로 표현한다.
 *   - l2_core_comb() : 현재 레지스터 + 입력으로 조합 신호 계산 (assign 문)
 *   - l2_core_step() : posedge 1회. 모든 next 값을 "현재" 값으로만 계산한 뒤
 *                      한 번에 commit (non-blocking assignment와 동일)
 *
 * 블록 구성 (설계 그림 기준)
 *   Total Control FSM : IDLE -> CH02_IMG_IN <-> WAIT_MAC_02 -> WAIT_LB_RST
 *                       -> CH35_IMG_IN <-> WAIT_MAC_35 -> STOP -> IDLE
 *   Weight Addr Ctrl  : IDLE -> WEIGHT_CAL(out_ch_sel 0..N-1) -> mac_done pulse
 *   Linebuffer Array  : 3 lane 병렬, pixel_valid 시 push, win_valid 1clk pulse
 *   Weight ROM        : OUT_CH별 ROM(주소 = ch3_5_en, sync read) + out_ch_sel MUX
 *   MAC Array         : 27 MAC 병렬 + adder tree (CFG_MAC_PIPE 단)
 *
 * 부분합 버퍼는 별도 담당자가 설계하므로 core 밖에 행동 모델로만 둔다.
 */
#ifndef L2_GOLDEN_H
#define L2_GOLDEN_H

#include <stdint.h>

/* ---------------- Layer geometry ---------------- */
#define L2_IN_H         26
#define L2_IN_W         26
#define L2_IN_CH        6
#define L2_OUT_CH       16
#define L2_LANES        3                       /* line buffer 개수 */
#define L2_NUM_PASSES   (L2_IN_CH / L2_LANES)   /* 2 */
#define L2_K            3
#define L2_OUT_H        (L2_IN_H - L2_K + 1)
#define L2_OUT_W        (L2_IN_W - L2_K + 1)
#define L2_NUM_WIN      (L2_OUT_H * L2_OUT_W)
#define L2_TAPS         (L2_LANES * L2_K * L2_K) /* 27 = 144bit x 3 */

/* ---------------- Quantization: INT16 ---------------- */
typedef int16_t act_t;      /* activation (line buffer, pixel_in) */
typedef int16_t wgt_t;      /* weight (ROM) */
typedef int64_t psum_t;     /* MAC 출력: 16x16=32bit 곱 27개 합 -> 최대 36bit */

#define ACT_BITS        16
#define WGT_BITS        16

/* ---------------- Configuration (-D 로 변경) ---------------- */

/* 0: 그림 그대로 - 외부 ch3_5_en 을 Weight ROM 주소로 직결
 * 1: pixel_valid 시점에 ch3_5_en 을 latch 해서 사용                        */
#ifndef CFG_CH35_EN_LATCH
#define CFG_CH35_EN_LATCH   0
#endif

/* 0: 그림 그대로 - out_ready = (IDLE|CH02_IMG_IN|CH35_IMG_IN) & ~win_valid
 * 1: out_ready 에 & ~phase_clear 추가 (STOP 직후 IDLE 에서 첫 픽셀 유실 방지) */
#ifndef CFG_READY_BLOCK_ON_CLEAR
#define CFG_READY_BLOCK_ON_CLEAR    0
#endif

/* Weight MUX 출력과 MAC 입력 사이 레지스터 단수 (그림 기준 0) */
#ifndef CFG_WEIGHT_PIPE
#define CFG_WEIGHT_PIPE     0
#endif

/* MAC Array 내부 단수: 곱셈 레지스터 + adder tree 레지스터 (>= 1) */
#ifndef CFG_MAC_PIPE
#define CFG_MAC_PIPE        2
#endif

#if CFG_MAC_PIPE < 1
#error "CFG_MAC_PIPE must be >= 1"
#endif

/* ---------------- State encodings ---------------- */
typedef enum
{
    T_IDLE = 0,
    T_CH02_IMG_IN,
    T_WAIT_MAC_02,
    T_WAIT_LB_RST,
    T_CH35_IMG_IN,
    T_WAIT_MAC_35,
    T_STOP
} total_state_t;

typedef enum
{
    W_IDLE = 0,
    W_WEIGHT_CAL
} wac_state_t;

/* ---------------- Registers per RTL block ---------------- */
typedef struct
{
    total_state_t state;
    uint8_t       mac_start;    /* 1clk pulse */
    uint8_t       phase_clear;  /* 1clk pulse */
    uint8_t       ch_count;     /* ch_count += ch_done & pixel_valid */
} total_fsm_t;

typedef struct
{
    wac_state_t state;
    uint8_t     out_ch_sel;
    uint8_t     mac_done;       /* 1clk pulse */
} weight_addr_ctrl_t;

typedef struct
{
    act_t    line[L2_LANES][2][L2_IN_W]; /* [0]: row-1, [1]: row-2 */
    act_t    win[L2_LANES][L2_K][L2_K];    /* window shift register */
    uint16_t col;
    uint16_t row;
    uint8_t  win_valid;                  /* 1clk pulse */
} line_buffer_t;

typedef struct
{
    uint8_t grp_q;              /* sync ROM 의 주소 레지스터 (ch3_5_en) */
} weight_rom_t;

typedef struct
{
    uint8_t valid;
    uint8_t och;
    uint8_t pass;
    wgt_t   weight[L2_TAPS];
} weight_bus_t;

typedef struct
{
    uint8_t valid;
    uint8_t och;
    uint8_t pass;
    psum_t  data;
} psum_bus_t;

typedef struct
{
    total_fsm_t        tfsm;
    weight_addr_ctrl_t wac;
    line_buffer_t      lb;
    weight_rom_t       rom;
    uint8_t            ch3_5_en_q;                      /* CFG_CH35_EN_LATCH=1 용 */
    weight_bus_t       wpipe[CFG_WEIGHT_PIPE + 1];      /* [0..CFG_WEIGHT_PIPE-1] 사용 */
    psum_bus_t         macp[CFG_MAC_PIPE];

    /* ROM 내용: [och][grp][lane*9 + ky*3 + kx] (합성 시 상수) */
    wgt_t              rom_data[L2_OUT_CH][L2_NUM_PASSES][L2_TAPS];
} l2_core_t;

/* ---------------- Ports ---------------- */
typedef struct
{
    uint8_t out_valid;              /* 이전 레이어 valid */
    act_t   pixel_in[L2_LANES];     /* 3 lane 병렬 입력 */
    uint8_t ch_done;                /* 패스 마지막 픽셀과 같은 사이클 */
    uint8_t ch3_5_en;               /* 현재 픽셀이 ch3~5 */
} l2_in_t;

typedef struct
{
    /* 외부 포트 */
    uint8_t    out_ready;
    psum_bus_t psum;                /* 부분합 버퍼로 가는 출력 */

    /* 내부 조합 신호 (trace 용) */
    uint8_t    pixel_valid;
    uint8_t    rom_grp_in;
    weight_bus_t mac_in;
} l2_out_t;

void l2_core_reset(l2_core_t *c,
                   const wgt_t weight[L2_OUT_CH][L2_IN_CH][L2_K][L2_K]);
void l2_core_comb(const l2_core_t *c, const l2_in_t *in, l2_out_t *out);
void l2_core_step(l2_core_t *c, const l2_in_t *in);

const char *total_state_name(total_state_t s);
const char *wac_state_name(wac_state_t s);

#endif
