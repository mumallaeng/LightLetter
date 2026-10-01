/*
 *  filter_sw.c
 *
 *  소프트웨어 필터. 구조와 주의사항은 filter_sw.h 를 먼저 읽으세요.
 */

#include <string.h>

#include "filter_sw.h"
#include "xil_printf.h"
#include "xil_cache.h"
#include "xtime_l.h"
#include "xaxivdma_hw.h"

/*===========================================================================
 *  ★ 메모리 안의 채널 위치
 *
 *  스트림이 R-B-G 이고 AXI 는 최하위 바이트가 낮은 주소로 가므로,
 *  픽셀 하나가 메모리에 [G][B][R] 로 놓인다고 보고 있습니다.
 *
 *  확인 방법 : 'n' 으로 FILT_CH_R 을 고르고 'f' 를 누릅니다.
 *      빨간 계열만 남으면   -> 아래 세 줄이 맞습니다. 그대로 두세요.
 *      초록만 남으면        -> OFF_R 과 OFF_G 를 맞바꾸세요.
 *      파랑만 남으면        -> OFF_R 과 OFF_B 를 맞바꾸세요.
 *
 *  이 세 줄만 맞으면 아래 모든 필터가 함께 맞습니다.
 *===========================================================================*/
#define OFF_G   0
#define OFF_B   1
#define OFF_R   2

#define BYTES_PER_PIXEL 3

/*===========================================================================
 *  상태
 *===========================================================================*/
static XAxiVdma *s_vdma;
static UINTPTR   s_cap_base;
static UINTPTR   s_disp[2];
static u16       s_w, s_h;
static u32       s_stride;          /* 한 줄의 바이트 수 */
static u32       s_frame_bytes;
static int       s_ready;
static int       s_disp_idx;        /* 다음에 CPU 가 쓸 표시 버퍼 */
static int       s_live;            /* 1 = 실시간 영상 상태 */
static Filt_kind s_kind = FILT_GRAY;
static int       s_thresh = 128;

static const char *s_names[FILT_COUNT] = {
    "copy   (baseline, no processing)",
    "R only (byte order check)",
    "G only (byte order check)",
    "B only (byte order check)",
    "gray",
    "binary",
    "sobel 3x3",
};

/*===========================================================================
 *  헬퍼
 *===========================================================================*/

/* 한 픽셀의 밝기. 77/150/29 는 8비트로 정규화한 Rec.601 계수입니다. */
static u8 gray_of(const u8 *p)
{
    return (u8)(((u32)p[OFF_R] * 77u +
                 (u32)p[OFF_G] * 150u +
                 (u32)p[OFF_B] * 29u) >> 8);
}

static void put_gray(u8 *p, u8 v)
{
    p[OFF_R] = v;
    p[OFF_G] = v;
    p[OFF_B] = v;
}

/*
 *  S2MM 이 방금 다 채운 캡처 버퍼의 인덱스.
 *
 *  PARKPTR[28:24] 가 "지금 쓰고 있는" 인덱스이므로, 그 직전 버퍼가 완성된
 *  프레임입니다. 이 계산을 빼먹고 현재 인덱스를 그대로 쓰면 절반만 채워진
 *  프레임을 필터링하게 됩니다.
 */
static int newest_capture(void)
{
    u32 pp  = XAxiVdma_ReadReg(s_vdma->BaseAddr, XAXIVDMA_PARKPTR_OFFSET);
    int cur = (int)((pp & XAXIVDMA_PARKPTR_WRTSTR_MASK) >> XAXIVDMA_WRTSTR_SHIFT);
    int n   = s_vdma->MaxNumFrames;

    if (cur < 0 || cur >= n) {
        cur = 0;
    }
    return (cur + n - 1) % n;
}

/*
 *  ★ 읽기 채널의 시작주소를 바꾸고 "반영"까지 시키는 함수.
 *
 *  여기서 한 번 크게 헤맸습니다. XAxiVdma_DmaSetBufferAddr() 는 이름과 달리
 *  시작주소 레지스터에 값을 써 넣기만 합니다. VDMA 는 그 값을 곧바로 쓰지
 *  않습니다.
 *
 *      xaxivdma_channel.c : XAxiVdma_ChannelSetBufferAddr()
 *          -> XAxiVdma_WriteReg(StartAddrBase, START_ADDR_OFFSET + i*4, addr)
 *             ... 그리고 그대로 return. 커밋 동작이 없습니다.
 *
 *  이 설계는 SG 를 쓰지 않으므로(INCLUDE_SG = 0) direct register 모드이고,
 *  이 모드에서 설정을 커밋하는 것은 VSIZE 쓰기입니다.
 *
 *      xaxivdma_channel.c : XAxiVdma_ChannelStart()
 *          else {
 *              // Direct register mode: Update vsize to start the channel
 *              XAxiVdma_WriteReg(StartAddrBase, VSIZE_OFFSET, Channel->Vsize);
 *          }
 *
 *  그래서 주소를 쓴 뒤 XAxiVdma_DmaStart() 를 한 번 더 불러 VSIZE 를 다시
 *  쓰게 합니다. 이미 돌고 있는 채널에 대해서도 안전합니다. ChannelStart 는
 *  "이미 running 이면 VSIZE 만 다시 쓰고 XST_SUCCESS 로 끝나는" 경로를
 *  가지고 있고, XST_DEVICE_BUSY 는 SG 모드에서만 나옵니다.
 *
 *  이걸 빼면 증상이 아주 조용합니다. 함수는 XST_SUCCESS 를 돌려주고
 *  로그도 정상으로 찍히는데 화면만 그대로입니다.
 */
static int commit_read_addrs(UINTPTR *set)
{
    int st = XAxiVdma_DmaSetBufferAddr(s_vdma, XAXIVDMA_READ, set);

    if (st != XST_SUCCESS) {
        return st;
    }
    return XAxiVdma_DmaStart(s_vdma, XAXIVDMA_READ);   /* VSIZE 재기록 = 커밋 */
}

/*
 *  MM2S 의 프레임스토어를 전부 같은 주소로 맞춥니다.
 *
 *  셋을 같은 곳으로 두면 VDMA 가 어느 인덱스를 고르든 같은 버퍼를 읽습니다.
 *  파킹도, genlock 인덱스 계산도 신경 쓸 필요가 없어집니다.
 */
static int point_display_at(UINTPTR addr)
{
    UINTPTR set[XAXIVDMA_MAX_FRAMESTORE];
    int i;

    for (i = 0; i < s_vdma->MaxNumFrames; i++) {
        set[i] = addr;
    }
    return commit_read_addrs(set);
}

/*===========================================================================
 *  필터 본체
 *
 *  전부 src -> dst 한 방향입니다. 제자리 처리를 하지 않으므로 이웃 연산에서
 *  이미 처리된 픽셀을 다시 읽는 사고가 나지 않습니다.
 *===========================================================================*/

static void filt_copy(const u8 *src, u8 *dst)
{
    memcpy(dst, src, s_frame_bytes);
}

/* keep 위치의 바이트만 남기고 나머지 두 채널을 0 으로 만듭니다. */
static void filt_channel(const u8 *src, u8 *dst, int keep)
{
    u32 n = (u32)s_w * s_h;
    u32 i;

    for (i = 0; i < n; i++) {
        const u8 *sp = src + i * BYTES_PER_PIXEL;
        u8       *dp = dst + i * BYTES_PER_PIXEL;

        dp[0] = 0;
        dp[1] = 0;
        dp[2] = 0;
        dp[keep] = sp[keep];
    }
}

static void filt_gray(const u8 *src, u8 *dst)
{
    u32 n = (u32)s_w * s_h;
    u32 i;

    for (i = 0; i < n; i++) {
        put_gray(dst + i * BYTES_PER_PIXEL, gray_of(src + i * BYTES_PER_PIXEL));
    }
}

static void filt_binary(const u8 *src, u8 *dst)
{
    u32 n = (u32)s_w * s_h;
    u32 i;

    for (i = 0; i < n; i++) {
        u8 g = gray_of(src + i * BYTES_PER_PIXEL);
        put_gray(dst + i * BYTES_PER_PIXEL, (g >= (u8)s_thresh) ? 255 : 0);
    }
}

/*
 *  소벨 3x3.
 *
 *  일부러 단순하게 썼습니다. 픽셀마다 이웃 9개의 밝기를 그 자리에서 계산하니
 *  픽셀당 27바이트를 읽습니다. 느립니다. 그게 이 필터의 교육적 가치입니다 -
 *  같은 알고리즘을 PL 로 옮겼을 때의 차이가 여기서 나옵니다.
 *
 *  빠르게 만들고 싶다면 : 밝기를 미리 한 줄씩 만들어 두고 세 줄을 돌려쓰면
 *  읽기가 1/3 로 줄어듭니다. 최적화 실습 과제로 좋습니다.
 *
 *  테두리 한 픽셀은 이웃이 없으므로 0 으로 둡니다.
 */
static void filt_sobel(const u8 *src, u8 *dst)
{
    int x, y;

    memset(dst, 0, s_frame_bytes);

    for (y = 1; y < (int)s_h - 1; y++) {
        const u8 *r0 = src + (u32)(y - 1) * s_stride;
        const u8 *r1 = src + (u32) y      * s_stride;
        const u8 *r2 = src + (u32)(y + 1) * s_stride;
        u8       *dp = dst + (u32) y      * s_stride + BYTES_PER_PIXEL;

        for (x = 1; x < (int)s_w - 1; x++) {
            int xm = (x - 1) * BYTES_PER_PIXEL;
            int xc =  x      * BYTES_PER_PIXEL;
            int xp = (x + 1) * BYTES_PER_PIXEL;

            int p00 = gray_of(r0 + xm), p01 = gray_of(r0 + xc), p02 = gray_of(r0 + xp);
            int p10 = gray_of(r1 + xm),                         p12 = gray_of(r1 + xp);
            int p20 = gray_of(r2 + xm), p21 = gray_of(r2 + xc), p22 = gray_of(r2 + xp);

            int gx = (p02 + 2 * p12 + p22) - (p00 + 2 * p10 + p20);
            int gy = (p20 + 2 * p21 + p22) - (p00 + 2 * p01 + p02);
            int m;

            if (gx < 0) gx = -gx;
            if (gy < 0) gy = -gy;

            m = gx + gy;            /* |Gx|+|Gy| : 제곱근을 피한 근사 */
            if (m > 255) m = 255;

            put_gray(dp, (u8)m);
            dp += BYTES_PER_PIXEL;
        }
    }
}

/*===========================================================================
 *  공개 함수
 *===========================================================================*/

int filter_sw_init(XAxiVdma *vdma, UINTPTR cap_base, UINTPTR disp_base, u16 w, u16 h)
{
    if (vdma == NULL || w == 0 || h == 0) {
        return -1;
    }

    s_vdma        = vdma;
    s_cap_base    = cap_base;
    s_w           = w;
    s_h           = h;
    s_stride      = (u32)w * BYTES_PER_PIXEL;
    s_frame_bytes = s_stride * h;
    s_disp[0]     = disp_base;
    s_disp[1]     = disp_base + s_frame_bytes;
    s_disp_idx    = 0;
    s_live        = 1;
    s_ready       = 1;

    /*
     * 표시 버퍼를 검게 지워 둡니다. 지우지 않으면 처음 전환하는 순간
     * DDR 의 쓰레기 값이 한 프레임 보입니다.
     */
    memset((void *)s_disp[0], 0, s_frame_bytes);
    memset((void *)s_disp[1], 0, s_frame_bytes);
    Xil_DCacheFlushRange((INTPTR)s_disp[0], s_frame_bytes * 2);

    xil_printf("filter : ready. capture 0x%08X x3, display 0x%08X x2 "
               "(%d bytes each)\r\n",
               (unsigned)s_cap_base, (unsigned)s_disp[0],
               (int)s_frame_bytes);
    return 0;
}

void filter_sw_next(void)
{
    s_kind = (Filt_kind)((s_kind + 1) % FILT_COUNT);
    xil_printf("filter : %s   (press 'f' to apply)\r\n", s_names[s_kind]);
}

Filt_kind filter_sw_get(void)
{
    return s_kind;
}

const char *filter_sw_name(Filt_kind k)
{
    return (k < FILT_COUNT) ? s_names[k] : "?";
}

void filter_sw_thresh(int delta)
{
    s_thresh += delta;
    if (s_thresh < 0)   s_thresh = 0;
    if (s_thresh > 255) s_thresh = 255;
    xil_printf("filter : binary threshold = %d\r\n", s_thresh);
}

void filter_sw_apply(void)
{
    int       cap;
    const u8 *src;
    u8       *dst;
    XTime     t0, t1;
    u32       us, ms;

    if (!s_ready) {
        xil_printf("filter : not initialised\r\n");
        return;
    }

    cap = newest_capture();
    src = (const u8 *)(s_cap_base + (UINTPTR)cap * s_frame_bytes);
    dst = (u8 *)s_disp[s_disp_idx];

    /*
     * VDMA 가 캐시를 거치지 않고 쓴 데이터를 읽어야 하므로, 캐시에 남은
     * 낡은 사본을 먼저 버립니다.
     */
    Xil_DCacheInvalidateRange((INTPTR)src, s_frame_bytes);

    XTime_GetTime(&t0);

    switch (s_kind) {
    case FILT_COPY:   filt_copy(src, dst);                break;
    case FILT_CH_R:   filt_channel(src, dst, OFF_R);      break;
    case FILT_CH_G:   filt_channel(src, dst, OFF_G);      break;
    case FILT_CH_B:   filt_channel(src, dst, OFF_B);      break;
    case FILT_GRAY:   filt_gray(src, dst);                break;
    case FILT_BINARY: filt_binary(src, dst);              break;
    case FILT_SOBEL:  filt_sobel(src, dst);               break;
    default:          filt_copy(src, dst);                break;
    }

    XTime_GetTime(&t1);

    /* 결과가 캐시에만 있으면 VDMA 는 옛날 내용을 읽습니다. 반드시 내려보냅니다. */
    Xil_DCacheFlushRange((INTPTR)dst, s_frame_bytes);

    /* 다 쓴 뒤에야 화면을 이쪽으로 옮깁니다. 이것이 더블버퍼링입니다. */
    if (point_display_at(s_disp[s_disp_idx]) != XST_SUCCESS) {
        xil_printf("filter : failed to repoint MM2S\r\n");
        return;
    }
    s_disp_idx ^= 1;
    s_live = 0;

    us = (u32)(((t1 - t0) * 1000000ULL) / COUNTS_PER_SECOND);
    ms = us / 1000u;

    xil_printf("filter : %s\r\n", s_names[s_kind]);
    xil_printf("  source capture buffer : %d\r\n", cap);
    xil_printf("  processing time       : %d.%03d ms\r\n",
               (int)ms, (int)(us % 1000u));
    if (us > 0) {
        xil_printf("  => %d fps if run continuously\r\n",
                   (int)(1000000u / us));
    }
    xil_printf("  ('b' to go back to live video)\r\n");
}

void filter_sw_live(void)
{
    UINTPTR set[XAXIVDMA_MAX_FRAMESTORE];
    int i;

    if (!s_ready) {
        return;
    }
    if (s_live) {
        xil_printf("filter : already showing live video\r\n");
        return;
    }

    /* MM2S 를 원래대로 캡처 버퍼 3장에 되돌립니다. */
    for (i = 0; i < s_vdma->MaxNumFrames; i++) {
        set[i] = s_cap_base + (UINTPTR)i * s_frame_bytes;
    }
    if (commit_read_addrs(set) != XST_SUCCESS) {
        xil_printf("filter : failed to restore MM2S\r\n");
        return;
    }

    s_live = 1;
    xil_printf("filter : back to live video\r\n");
}

void filter_sw_dump(void)
{
    xil_printf("\r\n--- software filter -----------------------------\r\n");
    xil_printf("state      : %s\r\n", s_live ? "live video" : "frozen (filtered)");
    xil_printf("filter     : %s\r\n", s_names[s_kind]);
    xil_printf("threshold  : %d  (binary only)\r\n", s_thresh);
    xil_printf("resolution : %dx%d, stride %d, frame %d bytes\r\n",
               (int)s_w, (int)s_h, (int)s_stride, (int)s_frame_bytes);
    xil_printf("capture    : 0x%08X (3 buffers)\r\n", (unsigned)s_cap_base);
    xil_printf("display    : 0x%08X / 0x%08X\r\n",
               (unsigned)s_disp[0], (unsigned)s_disp[1]);
    xil_printf("byte order : G=%d B=%d R=%d  (see filter_sw.c if colours look wrong)\r\n",
               OFF_G, OFF_B, OFF_R);
    xil_printf("-------------------------------------------------\r\n");
}
