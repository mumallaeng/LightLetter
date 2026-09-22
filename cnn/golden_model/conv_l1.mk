# conv_l1 (Layer 1 convolution) golden model test
#   make -f conv_l1.mk          build the test
#   make -f conv_l1.mk test     build and run (6 input scenarios x 4 handshake conditions)
#   make -f conv_l1.mk log      clock-by-clock log: logs/conv_l1_<SC>_<HS>.log (readable) and .csv
#                               default SC=real HS=A; e.g. make -f conv_l1.mk log SC=impulse HS=C
#                               (SC: real pattern impulse random rounding extreme all, HS: A B C D all)
#   make -f conv_l1.mk vectors  regenerate vectors/conv_l1.txt, conv_l2.txt from the Python dump
#   make -f conv_l1.mk clean

CC      ?= cc
CFLAGS  ?= -std=c99 -Wall -Wextra -O1
PYTHON  ?= python

BUILD   := build
TARGET  := $(BUILD)/test_conv_l1
VECTORS := vectors/conv_l1.txt
SC      ?= real
HS      ?= A

SRCS    := test_conv_l1.c conv_l1.c \
           total_ctrl_fsm_l1.c weight_addr_ctrl_l1.c weight_rom.c \
           output_buffer.c partial_sum.c buffer_ctrl.c bias_rom.c \
           relu_quant.c lane_packer.c out_reorder.c
HDRS    := common.h conv_l1.h total_ctrl_fsm_l1.h weight_addr_ctrl_l1.h weight_rom.h \
           line_buffer.h mac_unit.h mac_array.h \
           ob_common.h output_buffer.h partial_sum.h buffer_ctrl.h bias_rom.h \
           relu_quant.h lane_packer.h out_reorder.h

.PHONY: all test log vectors clean

all: $(TARGET)

$(TARGET): $(SRCS) $(HDRS)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(SRCS) -o $@

test: $(TARGET)
	./$(TARGET) $(VECTORS)

log: $(TARGET)
	./$(TARGET) $(VECTORS) -l -s $(SC) -h $(HS)

vectors:
	$(PYTHON) export_conv_vectors.py

clean:
	rm -f $(TARGET) logs/conv_l1_*
