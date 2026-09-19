/*
 * Layer 2 golden model testbench
 *
 * build : gcc -O2 -std=c99 -Wall -o l2_golden *.c
 * option: -DCFG_READY_BLOCK_ON_CLEAR=1
 *         -DCFG_WEIGHT_PIPE=1 -DCFG_MAC_PIPE=2
 *         -DSTIM_BUBBLE_PCT=30  (이전 레이어 out_valid 를 30% 확률로 끊음)
 *         -DNUM_FRAMES=2
 *
 * 출력 파일 (RTL testbench 비교용)
 *   golden_trace.csv    : 사이클별 제어 신호 + psum 출력
 *   golden_psum.txt     : psum_valid 가 뜬 사이클의 psum 스트림
 *   golden_input.hex    : 이전 레이어가 보내는 픽셀 스트림 (handshake 순서)
 *   golden_weight_rom.hex : ROM 초기값, 주소 = {och, grp}, 432bit (INT16 x 27)
 *   golden_ofmap.txt    : 최종 출력 (oc y x value)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "l2_top.h"

#ifndef STIM_BUBBLE_PCT
#define STIM_BUBBLE_PCT 0
#endif

#ifndef NUM_FRAMES
#define NUM_FRAMES 1
#endif

#define MAX_CYCLES          2000000
#define PIXELS_PER_PASS     (L2_IN_H * L2_IN_W)
#define PIXELS_PER_FRAME    (PIXELS_PER_PASS * L2_NUM_PASSES)

static act_t   g_ifmap[L2_IN_CH][L2_IN_H][L2_IN_W];
static wgt_t   g_weight[L2_OUT_CH][L2_IN_CH][L2_K][L2_K];
static int32_t g_bias[L2_OUT_CH];

static psum_t  g_ref_psum[L2_NUM_PASSES][L2_OUT_CH][L2_OUT_H][L2_OUT_W];
static psum_t  g_ref_ofmap[L2_OUT_CH][L2_OUT_H][L2_OUT_W];

/* ================================================================
 * Test data (INT16 전체 범위)
 *   - 일반 값: 결정적 hash 로 -32768 ~ 32767 전 범위
 *   - 코너   : (oy 0, ox 0) window 와 och 0 weight 를 전부 -32768
 *              -> 한 패스 psum 최대값 27 * 2^30 (누산기 bit 폭 검증)
 * ================================================================ */
static uint32_t hash32(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

static void make_test_data(void)
{
    uint32_t seed = 1;

    for (int ic = 0; ic < L2_IN_CH; ic++)
        for (int y = 0; y < L2_IN_H; y++)
            for (int x = 0; x < L2_IN_W; x++)
                g_ifmap[ic][y][x] = (act_t)(hash32(seed++) & 0xFFFFu);

    for (int oc = 0; oc < L2_OUT_CH; oc++)
    {
        for (int ic = 0; ic < L2_IN_CH; ic++)
            for (int ky = 0; ky < L2_K; ky++)
                for (int kx = 0; kx < L2_K; kx++)
                    g_weight[oc][ic][ky][kx] = (wgt_t)(hash32(seed++) & 0xFFFFu);

        g_bias[oc] = (int32_t)hash32(seed++) >> 4;
    }

    for (int ic = 0; ic < L2_IN_CH; ic++)
        for (int ky = 0; ky < L2_K; ky++)
            for (int kx = 0; kx < L2_K; kx++)
            {
                g_ifmap[ic][ky][kx]     = INT16_MIN;
                g_weight[0][ic][ky][kx] = INT16_MIN;
            }
}

/* 알고리즘 레퍼런스: 패스별 부분합(bias 제외)과 최종 출력 */
static void make_reference(void)
{
    for (int pass = 0; pass < L2_NUM_PASSES; pass++)
        for (int oc = 0; oc < L2_OUT_CH; oc++)
            for (int oy = 0; oy < L2_OUT_H; oy++)
                for (int ox = 0; ox < L2_OUT_W; ox++)
                {
                    psum_t acc = 0;
                    for (int lane = 0; lane < L2_LANES; lane++)
                    {
                        int ic = pass * L2_LANES + lane;
                        for (int ky = 0; ky < L2_K; ky++)
                            for (int kx = 0; kx < L2_K; kx++)
                                acc += (psum_t)g_ifmap[ic][oy + ky][ox + kx] *
                                       (psum_t)g_weight[oc][ic][ky][kx];
                    }
                    g_ref_psum[pass][oc][oy][ox] = acc;
                }

    for (int oc = 0; oc < L2_OUT_CH; oc++)
        for (int oy = 0; oy < L2_OUT_H; oy++)
            for (int ox = 0; ox < L2_OUT_W; ox++)
                g_ref_ofmap[oc][oy][ox] = (psum_t)g_bias[oc] +
                                          g_ref_psum[0][oc][oy][ox] +
                                          g_ref_psum[1][oc][oy][ox];
}

/* ================================================================
 * 이전 레이어 모델 (valid/ready source)
 *   - 한 프레임 = ch0~2 패스(raster) -> ch3~5 패스(raster)
 *   - ch_done 은 각 패스 마지막 픽셀과 같은 사이클
 *   - out_valid=0 인 동안 data 는 대기 중인 픽셀 값을 유지
 *   - is_ch35 는 보내지 않는다 (FSM 이 state 에서 직접 생성)
 * ================================================================ */
typedef struct
{
    int      idx;       /* 다음에 보낼 픽셀 (전체 스트림 기준) */
    int      total;
    uint32_t lfsr;
} source_t;

static int source_bubble(source_t *s)
{
    /* 32bit Galois LFSR */
    s->lfsr = (s->lfsr >> 1) ^ (-(int32_t)(s->lfsr & 1u) & 0xA3000000u);
    return (int)(s->lfsr % 100u) < STIM_BUBBLE_PCT;
}

static void source_drive(const source_t *s, int bubble, l2_in_t *in)
{
    int i    = s->idx < s->total ? s->idx : s->total - 1;
    int p    = i % PIXELS_PER_FRAME;
    int pass = p / PIXELS_PER_PASS;
    int pix  = p % PIXELS_PER_PASS;
    int y    = pix / L2_IN_W;
    int x    = pix % L2_IN_W;

    in->out_valid = (s->idx < s->total) && !bubble;
    in->ch_done   = in->out_valid && (pix == PIXELS_PER_PASS - 1);

    for (int lane = 0; lane < L2_LANES; lane++)
        in->pixel_in[lane] = g_ifmap[pass * L2_LANES + lane][y][x];
}

/* ================================================================
 * 부분합 버퍼 행동 모델 (실제 RTL 은 별도 담당자 설계)
 *   pass 0 : buf = bias + psum
 *   pass 1 : ofmap = buf + psum
 *   윈도우 위치는 och == N-1 인 psum 개수로 센다.
 * ================================================================ */
typedef struct
{
    int     win_idx[L2_NUM_PASSES];
    psum_t  buf[L2_OUT_CH][L2_NUM_WIN];
    psum_t  ofmap[L2_OUT_CH][L2_NUM_WIN];
    int     ofmap_written[L2_OUT_CH][L2_NUM_WIN];
} psum_buffer_t;

static void psum_buffer_accept(psum_buffer_t *pb, const psum_bus_t *ps)
{
    int pass = ps->pass;
    int pos  = pb->win_idx[pass] % L2_NUM_WIN;

    if (pass == 0)
        pb->buf[ps->och][pos] = (psum_t)g_bias[ps->och] + ps->data;
    else
    {
        pb->ofmap[ps->och][pos] = pb->buf[ps->och][pos] + ps->data;
        pb->ofmap_written[ps->och][pos] = 1;
    }

    if (ps->och == L2_OUT_CH - 1)
        pb->win_idx[pass]++;
}

/* ================================================================
 * psum stream checker
 *   pass 태그와 무관하게 "n 번째 psum 은 (pass, 위치, och) 여야 한다"로 비교.
 *   틀린 값이 다른 패스 weight 로 계산된 값과 같으면 함께 표시한다.
 * ================================================================ */
typedef struct
{
    int count;
    int mismatch;
    int shown;
    int max_bits;       /* 관측된 psum 의 signed bit 폭 최대값 */
} checker_t;

/* v 를 2's complement 로 표현하는 데 필요한 최소 bit 수 */
static int signed_bits(psum_t v)
{
    int bits = 1;

    while (v < -((psum_t)1 << (bits - 1)) || v > ((psum_t)1 << (bits - 1)) - 1)
        bits++;
    return bits;
}

/* win_pass 채널 window 에 w_pass 채널 weight 를 곱한 값 (진단용) */
static psum_t cross_psum(int win_pass, int w_pass, int och, int oy, int ox)
{
    psum_t acc = 0;

    for (int lane = 0; lane < L2_LANES; lane++)
        for (int ky = 0; ky < L2_K; ky++)
            for (int kx = 0; kx < L2_K; kx++)
                acc += (psum_t)g_ifmap[win_pass * L2_LANES + lane][oy + ky][ox + kx] *
                       (psum_t)g_weight[och][w_pass * L2_LANES + lane][ky][kx];
    return acc;
}

static void check_psum(checker_t *ck, const psum_bus_t *ps, long cycle)
{
    int n    = ck->count++;
    int och  = n % L2_OUT_CH;
    int win  = (n / L2_OUT_CH) % (L2_NUM_WIN * L2_NUM_PASSES);
    int pass = win / L2_NUM_WIN;
    int pos  = win % L2_NUM_WIN;
    int oy   = pos / L2_OUT_W;
    int ox   = pos % L2_OUT_W;

    psum_t expect = g_ref_psum[pass][och][oy][ox];

    if (signed_bits(ps->data) > ck->max_bits)
        ck->max_bits = signed_bits(ps->data);

    if (ps->och == och && ps->pass == pass && ps->data == expect)
        return;

    ck->mismatch++;
    if (ck->shown >= 20)
        return;
    ck->shown++;

    printf("  [psum mismatch] cycle %6ld : expect pass %d (oy %2d, ox %2d) och %2d = %12lld"
           " | got pass tag %d och %2d = %12lld",
           cycle, pass, oy, ox, och, (long long)expect, ps->pass, ps->och,
           (long long)ps->data);
    for (int alt = 0; alt < L2_NUM_PASSES; alt++)
        if (alt != pass && ps->data == cross_psum(pass, alt, och, oy, ox))
            printf("  <- ch%d~%d window x ch%d~%d weight",
                   pass * 3, pass * 3 + 2, alt * 3, alt * 3 + 2);
    for (int d = 1; d <= 2; d++)
        if (pos + d < L2_NUM_WIN &&
            ps->data == g_ref_psum[pass][och][(pos + d) / L2_OUT_W][(pos + d) % L2_OUT_W])
        {
            printf("  <- %d 칸 뒤 window 값 (window 밀림)", d);
            break;
        }
    printf("\n");
}

/* ================================================================
 * File dump helpers
 * ================================================================ */
static void dump_weight_rom(const weight_rom_t *rom)
{
    FILE *f = fopen("golden_weight_rom.hex", "w");
    if (!f) return;

    /* 한 줄 = 432bit (INT16 x 27), MSB 가 tap 26 (lane2, ky2, kx2) */
    for (int oc = 0; oc < L2_OUT_CH; oc++)
        for (int grp = 0; grp < L2_NUM_PASSES; grp++)
        {
            for (int t = L2_TAPS - 1; t >= 0; t--)
                fprintf(f, "%04X", (uint16_t)rom->data[oc][grp][t]);
            fprintf(f, "\n");
        }
    fclose(f);
}

static void dump_input_stream(int total)
{
    FILE *f = fopen("golden_input.hex", "w");
    if (!f) return;

    /* {ch_done, lane2, lane1, lane0} */
    for (int i = 0; i < total; i++)
    {
        source_t s = { i, total, 0 };
        l2_in_t  in;
        source_drive(&s, 0, &in);
        fprintf(f, "%X %04X %04X %04X\n", in.ch_done,
                (uint16_t)in.pixel_in[2], (uint16_t)in.pixel_in[1],
                (uint16_t)in.pixel_in[0]);
    }
    fclose(f);
}

/* ================================================================
 * main
 * ================================================================ */
int main(void)
{
    static l2_top_t      top;
    static psum_buffer_t pb;
    checker_t ck = { 0, 0, 0, 0 };

    make_test_data();
    make_reference();
    l2_top_reset(&top, g_weight);

    source_t src = { 0, PIXELS_PER_FRAME * NUM_FRAMES, 0xACE1u };

    FILE *ftr = fopen("golden_trace.csv", "w");
    FILE *fps = fopen("golden_psum.txt", "w");
    if (!ftr || !fps)
    {
        printf("cannot open output files\n");
        return 1;
    }

    fprintf(ftr, "cycle,tfsm,wac,out_valid,out_ready,pixel_valid,ch_done,"
                 "is_ch35,ch_count,win_valid,phase_clear,mac_start,mac_done,"
                 "out_ch_sel,rom_grp,psum_valid,psum_och,psum_pass,psum_data\n");
    fprintf(fps, "# cycle och pass data\n");

    printf("LightLetter L2 golden model\n");
    printf("  INT%d activation x INT%d weight\n", ACT_BITS, WGT_BITS);
    printf("  CFG_READY_BLOCK_ON_CLEAR=%d CFG_WEIGHT_PIPE=%d "
           "CFG_MAC_PIPE=%d\n  STIM_BUBBLE_PCT=%d NUM_FRAMES=%d\n",
           CFG_READY_BLOCK_ON_CLEAR, CFG_WEIGHT_PIPE,
           CFG_MAC_PIPE, STIM_BUBBLE_PCT, NUM_FRAMES);

    int  frames_done = 0;
    long cycle;

    for (cycle = 0; cycle < MAX_CYCLES; cycle++)
    {
        l2_in_t in;

        /* 입력 인가 -> 조합 로직 평가 */
        source_drive(&src, source_bubble(&src), &in);
        l2_top_comb(&top, &in);

        const psum_bus_t *psum = &top.mac_o.psum;

        fprintf(ftr, "%ld,%s,%s,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%lld\n",
                cycle,
                total_state_name(top.fsm.state), wac_state_name(top.wac.state),
                in.out_valid, top.fsm_o.out_ready, top.fsm_o.pixel_valid, in.ch_done,
                top.fsm_o.is_ch35, top.fsm.ch_count, top.lb.win_valid,
                top.fsm.phase_clear, top.fsm.mac_start, top.wac.mac_done,
                top.wac.out_ch_sel, top.rom.grp_q,
                psum->valid, psum->och, psum->pass, (long long)psum->data);

        if (psum->valid)
        {
            fprintf(fps, "%ld %d %d %lld\n", cycle, psum->och, psum->pass,
                    (long long)psum->data);
            check_psum(&ck, psum, cycle);
            psum_buffer_accept(&pb, psum);
        }

        if (top.fsm.state == T_STOP)
            frames_done++;

        /* posedge clk */
        if (top.fsm_o.pixel_valid)
            src.idx++;
        l2_top_seq(&top);

        /* 모든 프레임 끝 + FSM 두 개 IDLE + MAC 파이프라인 비면 종료 */
        if (frames_done == NUM_FRAMES && top.fsm.state == T_IDLE &&
            top.wac.state == W_IDLE && !mac_array_busy(&top.mac))
            break;
    }

    fclose(ftr);
    fclose(fps);
    dump_weight_rom(&top.rom);
    dump_input_stream(src.total);

    /* ---------------- 최종 출력 비교 ---------------- */
    FILE *fof = fopen("golden_ofmap.txt", "w");
    int ofmap_mismatch = 0;
    int ofmap_missing  = 0;
    int ofmap_bits     = 0;

    for (int oc = 0; oc < L2_OUT_CH; oc++)
        for (int pos = 0; pos < L2_NUM_WIN; pos++)
        {
            int oy = pos / L2_OUT_W;
            int ox = pos % L2_OUT_W;

            if (fof)
                fprintf(fof, "%d %d %d %lld\n", oc, oy, ox,
                        (long long)pb.ofmap[oc][pos]);

            if (signed_bits(g_ref_ofmap[oc][oy][ox]) > ofmap_bits)
                ofmap_bits = signed_bits(g_ref_ofmap[oc][oy][ox]);

            if (!pb.ofmap_written[oc][pos])
                ofmap_missing++;
            else if (pb.ofmap[oc][pos] != g_ref_ofmap[oc][oy][ox])
                ofmap_mismatch++;
        }
    if (fof)
        fclose(fof);

    int expect_psum = L2_NUM_WIN * L2_OUT_CH * L2_NUM_PASSES * NUM_FRAMES;

    printf("\n  cycles         : %ld%s\n", cycle,
           cycle >= MAX_CYCLES ? "  (TIMEOUT - FSM hang)" : "");
    printf("  frames done    : %d / %d\n", frames_done, NUM_FRAMES);
    printf("  pixels sent    : %d / %d\n", src.idx, src.total);
    printf("  psum count     : %d / %d\n", ck.count, expect_psum);
    printf("  psum mismatch  : %d\n", ck.mismatch);
    printf("  ofmap mismatch : %d, missing %d (last frame)\n",
           ofmap_mismatch, ofmap_missing);
    printf("  bit width      : MAC psum %d bit, bias+2pass ofmap %d bit (관측 최대)\n",
           ck.max_bits, ofmap_bits);

    int pass = (cycle < MAX_CYCLES) && ck.count == expect_psum &&
               ck.mismatch == 0 && ofmap_mismatch == 0 && ofmap_missing == 0;

    printf("\n  RESULT: %s\n", pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}
