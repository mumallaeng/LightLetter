/*
 * cnn_top : conv_l1 -> pool_l1 -> conv_l2 -> pool_l2 -> fc (FC1 -> FC2 -> FC3) -> argmax  (28 x 28 x 1 -> class 0..35)
 *
 *   이전 단 --pixel_in / in_valid / ch_done--> conv_l1 --out_data0..2 / out_valid / out_ch_done--> pool_l1
 *   pool_l1 --pool_data0..2 / pool_valid / pool_ch_done--> conv_l2 --out_data / out_valid / out_ch_done--> pool_l2
 *   pool_l2 --pool_data / pool_valid--> fc_top (fc_in_data / fc_in_valid)
 *   fc_top  --logit_data / logit_valid--> argmax --cnn_result / cnn_done--> top 출력
 *   pool_l2 pool_ch_done 은 fc_top 에 포트가 없어 연결하지 않는다 (FC 는 개수 400 으로 프레임을 센다).
 *
 *   ready (뒤 -> 앞):
 *     argmax logit_ready (항상 1) -> fc_top logit_ready,  fc_top fc_in_ready -> pool_l2 pool_ready
 *     pool_l2 out_ready -> conv_l2 out_ready,   conv_l2 in_ready   -> pool_l1 pool_ready
 *     pool_l1 out_ready -> conv_l1 out_ready,   conv_l1 in_ready   -> 이전 단 (in_ready)
 *
 *   단계별 모양 (프레임당):
 *     conv_l1 출력 : group 0 (och0~2) 26x26 raster -> group 1 (och3~5), group 마다 마지막에 out_ch_done
 *     pool_l1 출력 : pass 0 13x13 -> pass 1 13x13, pass 마지막 (12,12) 에 pool_ch_done
 *     conv_l2 출력 : och0 11x11 -> ... -> och15, 채널마다 마지막에 out_ch_done
 *     pool_l2 출력 : och0 5x5 -> ... -> och15 = FC1 입력 400 개 (c*25 + y*5 + x, PyTorch flatten 순서)
 *     fc_top 출력  : FC1 120 -> FC2 84 -> FC3 logit 36 개 (signed, class 0 부터)
 *     argmax 출력  : 36 번째 logit 을 받은 다음 클럭에 cnn_done 1 클럭 펄스, 그때 cnn_result = class  = top 출력
 *
 *   조합 평가 순서 (cnn_top_comb). in_ready / out_valid / out_data 는 모두 레지스터에서 나오므로 루프가 없다
 *   (conv: FSM / Reorder Buffer, fc_top: staging full, argmax: 상수 1, pool: ready 는 다음 단 ready 를 조합으로 통과):
 *     ① conv_l2 (in_valid = 0, out_ready = 0) : in_ready, out_valid / out_data 만 읽는다
 *     ② conv_l1 (out_ready = 0)               : out_valid / out_data 만 읽는다
 *     ③ pool_l1 (pool_ready = conv_l2 in_ready)
 *     ④ conv_l1 (out_ready = pool_l1 out_ready)  : pop 확정
 *     ⑤ argmax  (logit_valid = 0)           : logit_ready 만 읽는다
 *     ⑥ fc_top  (fc_in_valid = 0, logit_ready = argmax logit_ready) : fc_in_ready 만 읽는다
 *     ⑦ pool_l2 (pool_ready = fc_top fc_in_ready)
 *     ⑧ conv_l2 (in_valid = pool_l1 pool_valid, out_ready = pool_l2 out_ready) : push / pop 확정
 *     ⑨ fc_top  (fc_in = pool_l2 출력, logit_ready = argmax logit_ready) : 확정
 *     ⑩ argmax  (logit = fc_top 출력) : 확정
 *
 *   conv_l1 / conv_l2 는 line_buffer.h 의 IMG_WIDTH 가 컴파일 상수라 cnn_chain_l1.c / cnn_chain_l2.c 안의
 *   static 인스턴스를 chain_l1_* / chain_l2_* 로 쓴다. 그래서 cnn_top 은 한 프로그램에 인스턴스 1 개만 둔다.
 *   fc 는 Output Buffer 를 120 뉴런까지 쓰므로 모든 파일을 -DOB_MAX_C_OUT=120 으로 빌드한다 (cnn_top.mk).
 *
 *   프레임 제약 (팀원 out_reorder): conv 의 Reorder Buffer 는 한 프레임만 담고 FSM 으로 가는 backpressure 가 없다.
 *   다음 이미지는 cnn_top_l1_idle() (conv_l1 이 앞 프레임을 다 내보냄) 뒤에 넣는다.
 */
#ifndef CNN_TOP_H
#define CNN_TOP_H

#include <stdint.h>
#include "cnn_chain.h"
#include "pool_l1.h"
#include "pool_l2.h"
#include "fc_top.h"
#include "argmax.h"

#define CNN_TOP_IN_H    28
#define CNN_TOP_IN_W    28
#define CNN_TOP_FC_IN   (POOL_L2_OUT_H * POOL_L2_OUT_W * 16)   /* 400 */
#define CNN_TOP_N_LOGIT 36

/* top input ports */
typedef struct
{
    uint8_t  in_valid;          /* 이전 단 */
    int16_t  pixel_in;
    uint8_t  ch_done;           /* 이미지 마지막 픽셀 */
} cnn_top_in_t;

/* top output ports */
typedef struct
{
    uint8_t  in_ready;          /* -> 이전 단 */
    uint8_t  cnn_result;        /* argmax class 0..35, cnn_done 클럭에 확정 */
    uint8_t  cnn_done;          /* 1 클럭 펄스 (36 번째 logit 다음 클럭) */
} cnn_top_out_t;

typedef struct
{
    /* module instances (conv_l1 / conv_l2 는 cnn_chain_l1.c / cnn_chain_l2.c 의 static 인스턴스) */
    pool_l1_t pool1;
    pool_l2_t pool2;
    fc_top_t  fc;
    argmax_t  am;

    /* module port wires (cnn_top_comb 에서 갱신, 모니터 / 로그용) */
    chain_l1_in_t  l1_i;
    chain_l1_out_t l1_o;
    pool_l1_in_t   p1_i;
    pool_l1_out_t  p1_o;
    chain_l2_in_t  l2_i;
    chain_l2_out_t l2_o;
    pool_l2_in_t   p2_i;
    pool_l2_out_t  p2_o;
    fc_top_in_t    fc_i;
    fc_top_out_t   fc_o;
    argmax_in_t    am_i;
    argmax_out_t   am_o;

    /* 이번 클럭에 전달이 일어나는 경계 (valid & ready) */
    uint8_t tx_in, tx_l1, tx_p1, tx_l2, tx_p2, tx_fc1, tx_fc2, tx_logit;
} cnn_top_t;

void cnn_top_init(cnn_top_t *m, const int16_t *w1, const int32_t *b1, uint8_t s1,
                  const int16_t *w2, const int32_t *b2, uint8_t s2,
                  const fc_param_t fc_p[3], const int16_t *const fc_rom[3], const int32_t *const fc_bias[3]);
void cnn_top_comb(cnn_top_t *m, const cnn_top_in_t *in, cnn_top_out_t *out);  /* always @(*) */
void cnn_top_seq(cnn_top_t *m);                                              /* posedge clk */

int  cnn_top_l1_idle(void);     /* conv_l1 이 비었는지 (다음 프레임 게이트) */
int  cnn_top_idle(void);        /* conv_l1, conv_l2 모두 비었는지 */

#endif
