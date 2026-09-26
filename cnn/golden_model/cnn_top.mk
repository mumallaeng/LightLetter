# cnn_top : conv_l1 -> pool_l1 -> conv_l2 -> pool_l2 -> FC1 -> FC2 -> FC3 -> argmax end-to-end golden model test
#   make -f cnn_top.mk          build
#   make -f cnn_top.mk test     run handshake conditions A B C D (real image, 2 frames per run)
#                               weights: vectors/conv_l1.txt, conv_l2.txt, fc1..3.txt
#   make -f cnn_top.mk log      clock log logs/cnn_top_real_<HS>.log / .csv (default HS=A)
#   make -f cnn_top.mk clean
#
# conv_l1 / conv_l2 are wrapped in separate translation units (cnn_chain_l1.c: IMG_WIDTH 28,
# cnn_chain_l2.c: IMG_WIDTH 13) so both line buffer widths live in one binary.
# FC reuses the Output Buffer with up to 120 neurons, so every file is built with -DOB_MAX_C_OUT=120.

CC      ?= cc
CFLAGS  ?= -std=c99 -Wall -Wextra -O1 -DOB_MAX_C_OUT=120
HS      ?= A

BUILD   := build
TARGET  := $(BUILD)/test_cnn_top

SRCS    := test_cnn_top.c cnn_top.c cnn_chain_l1.c cnn_chain_l2.c \
           conv_l1.c conv_l2.c pool_l1.c pool_l1_ctrl.c pool_l1_datapath.c pool_l2.c \
           fc_top.c fc_layer.c fc_staging.c fc_ctrl.c fc_weight_rom.c fc_mac.c fc_quant_signed.c argmax.c \
           total_ctrl_fsm_l1.c weight_addr_ctrl_l1.c total_ctrl_fsm_l2.c weight_addr_ctrl_l2.c weight_rom.c \
           output_buffer.c partial_sum.c buffer_ctrl.c bias_rom.c relu_quant.c lane_packer.c out_reorder.c
HDRS    := cnn_top.h cnn_chain.h conv_l1.h conv_l2.h pool_l1.h pool_l1_ctrl.h pool_l1_datapath.h pool_l2.h \
           fc_common.h fc_top.h fc_layer.h fc_staging.h fc_ctrl.h fc_weight_rom.h fc_mac.h fc_quant_signed.h argmax.h \
           total_ctrl_fsm_l1.h weight_addr_ctrl_l1.h total_ctrl_fsm_l2.h weight_addr_ctrl_l2.h weight_rom.h common.h \
           line_buffer.h line_buffer_array.h mac_unit.h mac_array.h \
           ob_common.h output_buffer.h partial_sum.h buffer_ctrl.h bias_rom.h relu_quant.h lane_packer.h out_reorder.h

.PHONY: all test log clean

all: $(TARGET)

$(TARGET): $(SRCS) $(HDRS)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(SRCS) -lm -o $@

test: $(TARGET)
	./$(TARGET)

log: $(TARGET)
	./$(TARGET) -l -h $(HS)

clean:
	rm -f $(TARGET) logs/cnn_top_*
