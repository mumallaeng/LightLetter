#include "cpu_preprocess.h"

#include <string.h>

/* 임계값 처리된 112x112 (RTL 의 pixel_mem) */
static uint8_t proc[PRE_IN_W * PRE_IN_H];

void cpu_preprocess(const uint8_t *roi, int16_t out[PRE_OUT_W * PRE_OUT_H])
{
    int x, y, i;
    int min_x = PRE_IN_W - 1, max_x = 0;
    int min_y = PRE_IN_H - 1, max_y = 0;
    int found = 0;
    int bw, bh, wide, s_short, s_long, fit;
    int gw, gh, ox, oy;
    int xm[PRE_GLYPH_LIMIT];
    int ym[PRE_GLYPH_LIMIT];

    /* 입력단: luma -> 반전 -> threshold, 동시에 전경 bbox (ST_RECV) */
    for (y = 0; y < PRE_IN_H; y++) {
        for (x = 0; x < PRE_IN_W; x++) {
            const uint8_t *p = roi + (y * PRE_IN_W + x) * PRE_BPP;
            uint32_t g = p[0], b = p[1], r = p[2];
            uint32_t luma = (77u * r + 150u * g + 29u * b + 128u) >> 8;
            uint32_t inv  = 255u - luma;
            uint8_t  pix  = (inv < PRE_THRESHOLD) ? 0u : (uint8_t)inv;

            proc[y * PRE_IN_W + x] = pix;
            if (pix) {
                found = 1;
                if (x < min_x) min_x = x;
                if (x > max_x) max_x = x;
                if (y < min_y) min_y = y;
                if (y > max_y) max_y = y;
            }
        }
    }

    memset(out, 0, sizeof(int16_t) * PRE_OUT_W * PRE_OUT_H);
    if (!found) {
        return;
    }

    /* ST_PREP / ST_FIT: c * long <= short * 22 를 만족하는 가장 큰 c (1..22) */
    bw   = max_x - min_x + 1;
    bh   = max_y - min_y + 1;
    wide = (max_x - min_x) >= (max_y - min_y);
    s_short = wide ? bh : bw;
    s_long  = wide ? bw : bh;
    fit = (s_short * PRE_GLYPH_LIMIT) / s_long;
    if (fit < 1) fit = 1;
    if (fit > PRE_GLYPH_LIMIT) fit = PRE_GLYPH_LIMIT;

    /* ST_FIT_DONE: offset = (28 - size) >> 1 */
    if (wide) {
        gw = PRE_GLYPH_LIMIT;  gh = fit;
        ox = (PRE_OUT_W - PRE_GLYPH_LIMIT) >> 1;
        oy = (PRE_OUT_H - fit) >> 1;
    } else {
        gw = fit;  gh = PRE_GLYPH_LIMIT;
        ox = (PRE_OUT_W - fit) >> 1;
        oy = (PRE_OUT_H - PRE_GLYPH_LIMIT) >> 1;
    }

    /* ST_X/Y_STORE: map[i] = min + floor(i * bbox / glyph) */
    for (i = 0; i < gw; i++) xm[i] = min_x + (i * bw) / gw;
    for (i = 0; i < gh; i++) ym[i] = min_y + (i * bh) / gh;

    /* ST_OUT_*: 배치 + cnn 입력 스케일 */
    for (y = 0; y < gh; y++) {
        const uint8_t *row = proc + ym[y] * PRE_IN_W;
        int16_t *dst = out + (oy + y) * PRE_OUT_W + ox;

        for (x = 0; x < gw; x++) {
            uint32_t p = row[xm[x]];
            dst[x] = (int16_t)((p << 6) + (p >> 2));
        }
    }
}
