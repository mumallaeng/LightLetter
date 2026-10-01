#ifndef SRC_CPU_CNN_CPU_PREPROCESS_H_
#define SRC_CPU_CNN_CPU_PREPROCESS_H_

/*
 * rtl/cnn_cam/img_preprocess.v 를 CPU 에서 그대로 하는 bit-exact 버전.
 *
 *   112x112 RGB (roi_dma 의 roi_frame, PL 로 DMA 되는 것과 같은 버퍼)
 *   -> grayscale (77R + 150G + 29B + 128) >> 8 -> 반전 -> THRESHOLD 미만 0
 *   -> 전경 bbox -> 가로세로비 유지하며 긴 변을 22 에 맞춤
 *   -> 28x28 가운데 배치 (MARGIN 3) -> cnn 입력 스케일 (p << 6) + (p >> 2)
 *
 * 픽셀은 3 byte, DMA 스트림 tdata 의 byte 순서 그대로
 *   byte 0 = tdata[7:0] = G, byte 1 = tdata[15:8] = B, byte 2 = tdata[23:16] = R
 *
 * display_python/capture_test.py 의 rtl_threshold / rtl_fit / cnn_scale 과
 * 같은 알고리즘이고, 그 Python 모델은 xsim 에서 RTL 과 bit-exact 로 확인됨.
 */

#include <stdint.h>

#define PRE_IN_W        112
#define PRE_IN_H        112
#define PRE_BPP         3
#define PRE_OUT_W       28
#define PRE_OUT_H       28
#define PRE_MARGIN      3
#define PRE_THRESHOLD   160     /* img_preprocess.v 기본값 = 블록디자인 값 */
#define PRE_GLYPH_LIMIT (PRE_OUT_W - 2 * PRE_MARGIN)

/* roi : 112x112 G,B,R 프레임, out : cnn 입력 784 픽셀 (pixel_in) */
void cpu_preprocess(const uint8_t *roi, int16_t out[PRE_OUT_W * PRE_OUT_H]);

#endif /* SRC_CPU_CNN_CPU_PREPROCESS_H_ */
