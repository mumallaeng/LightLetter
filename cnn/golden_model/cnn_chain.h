/*
 * cnn_chain : conv_l1 -> pool_l1 -> conv_l2 연결
 *
 * conv_l1 / conv_l2 는 line_buffer.h 의 IMG_WIDTH (28 / 13) 가 컴파일 상수라 한 번역 단위에 같이 넣을 수 없다.
 * 그래서 각각을 별도 .c (cnn_chain_l1.c, cnn_chain_l2.c) 안의 static 인스턴스로 두고, 여기의 포트 구조체와
 * 함수로만 접근한다. 팀원 코드 (line_buffer.h 등) 는 수정하지 않는다.
 */
#ifndef CNN_CHAIN_H
#define CNN_CHAIN_H

#include <stdint.h>

/* ---------------- conv_l1 (28x28x1 -> 26x26x6, FIFO PACK 3) ---------------- */
typedef struct
{
    uint8_t  in_valid;
    int16_t  pixel_in;
    uint8_t  ch_done;
    uint8_t  out_ready;         /* FIFO rd_en */
} chain_l1_in_t;

typedef struct
{
    uint8_t  in_ready;
    uint16_t out_data[3];
    uint8_t  out_ch_done;
    uint8_t  out_valid;
} chain_l1_out_t;

typedef struct
{
    const char *fsm;
    uint8_t     win_valid, mac_valid, sum_valid;
    uint16_t    rb_fill, rb_peak;      /* Reorder Buffer: 이번 프레임에 쓴 entry 수 / 최대 */
    uint32_t    rb_overrun;            /* 앞 프레임을 다 읽기 전에 들어와 버려진 entry 수 */
} chain_probe_t;

void chain_l1_init(const int16_t *weight, const int32_t *bias, uint8_t scale_exp);
void chain_l1_comb(const chain_l1_in_t *in, chain_l1_out_t *out);
void chain_l1_seq(void);
int  chain_l1_idle(void);
void chain_l1_probe(chain_probe_t *p);

/* ---------------- conv_l2 (13x13x6 -> 11x11x16, FIFO PACK 1) ---------------- */
typedef struct
{
    uint8_t  in_valid;
    int16_t  pixel_in[3];
    uint8_t  ch_done;
    uint8_t  out_ready;
} chain_l2_in_t;

typedef struct
{
    uint8_t  in_ready;
    uint16_t out_data;
    uint8_t  out_ch_done;
    uint8_t  out_valid;
} chain_l2_out_t;

void chain_l2_init(const int16_t *weight, const int32_t *bias, uint8_t scale_exp);
void chain_l2_comb(const chain_l2_in_t *in, chain_l2_out_t *out);
void chain_l2_seq(void);
int  chain_l2_idle(void);
void chain_l2_probe(chain_probe_t *p);

#endif
