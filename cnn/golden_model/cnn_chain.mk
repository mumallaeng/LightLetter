# cnn_chain : conv_l1 -> pool_l1 -> conv_l2 end-to-end golden model test
#   make -f cnn_chain.mk          build
#   make -f cnn_chain.mk test     run handshake conditions A B C D (real image, 2 frames per run, frame gate on conv_l1)
#   make -f cnn_chain.mk log      clock log logs/cnn_chain_real_<HS>.log / .csv (default HS=A)
#   make -f cnn_chain.mk out      log + final conv_l2 output maps logs/cnn_chain_real_<HS>_f0_out.txt
#   make -f cnn_chain.mk clean
#
# conv_l1 / conv_l2 are wrapped in separate translation units (cnn_chain_l1.c: IMG_WIDTH 28,
# cnn_chain_l2.c: IMG_WIDTH 13) so both line buffer widths live in one binary.

CC      ?= cc
CFLAGS  ?= -std=c99 -Wall -Wextra -O1
PYTHON  ?= python
HS      ?= A

BUILD   := build
TARGET  := $(BUILD)/test_cnn_chain

SRCS    := test_cnn_chain.c cnn_chain_l1.c cnn_chain_l2.c \
           conv_l1.c conv_l2.c pool_l1.c pool_l1_ctrl.c pool_l1_datapath.c \
           total_ctrl_fsm_l1.c weight_addr_ctrl_l1.c total_ctrl_fsm_l2.c weight_addr_ctrl_l2.c weight_rom.c \
           output_buffer.c partial_sum.c buffer_ctrl.c bias_rom.c relu_quant.c lane_packer.c out_reorder.c
HDRS    := cnn_chain.h conv_l1.h conv_l2.h pool_l1.h pool_l1_ctrl.h pool_l1_datapath.h \
           total_ctrl_fsm_l1.h weight_addr_ctrl_l1.h total_ctrl_fsm_l2.h weight_addr_ctrl_l2.h weight_rom.h common.h \
           line_buffer.h line_buffer_array.h mac_unit.h mac_array.h \
           ob_common.h output_buffer.h partial_sum.h buffer_ctrl.h bias_rom.h relu_quant.h lane_packer.h out_reorder.h

.PHONY: all test log out clean

all: $(TARGET)

$(TARGET): $(SRCS) $(HDRS)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(SRCS) -o $@

test: $(TARGET)
	./$(TARGET)

log: $(TARGET)
	./$(TARGET) -l -h $(HS)

out: log
	$(PYTHON) extract_frame_out.py logs/cnn_chain_real_$(HS).csv

clean:
	rm -f $(TARGET) logs/cnn_chain_*
