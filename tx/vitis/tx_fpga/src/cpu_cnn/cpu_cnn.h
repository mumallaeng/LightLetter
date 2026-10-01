#ifndef SRC_CPU_CNN_CPU_CNN_H_
#define SRC_CPU_CNN_CPU_CNN_H_

/*
 * cnn_top 과 같은 연산을 CPU 에서 하는 bit-exact 정수 모델.
 *
 *   conv1 (1->6, 3x3) -> pool 2x2 -> conv2 (6->16, 3x3) -> pool 2x2
 *   -> fc1 (400->120) -> fc2 (120->84) -> fc3 (84->26) -> argmax
 *
 * tb/cnn/cnn_top/gen_img_vectors.py 의 Model.run() 을 그대로 옮긴 것이고,
 * 가중치는 export_cpu_model.py 가 만든 cpu_cnn_weights.h 에서 읽습니다.
 * 입력 img 는 cnn_ip 가 AXIS 로 받는 것과 같은 pixel_in (round(p / 255 * 2^14)).
 *
 * 보드 (Vitis, ARM) 와 PC (tb/cnn/cnn_top/cpu_bench.c) 가 같은 파일을 씁니다.
 */

#include <stdint.h>

#define CPU_CNN_IMG_W       28
#define CPU_CNN_IMG_PIX     (CPU_CNN_IMG_W * CPU_CNN_IMG_W)
#define CPU_CNN_NUM_CLASS   26
/* conv1 26*26*6*9 + conv2 11*11*16*6*9 + fc 400*120 + 120*84 + 84*26 */
#define CPU_CNN_TOTAL_MAC   201312u

/* returns the class (0 = 'A'); logit may be NULL */
int cpu_cnn_run(const int16_t img[CPU_CNN_IMG_PIX], int16_t logit[CPU_CNN_NUM_CLASS]);

#endif /* SRC_CPU_CNN_CPU_CNN_H_ */
