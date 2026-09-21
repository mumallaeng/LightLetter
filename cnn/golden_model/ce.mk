# CE (Convolution Engine) end-to-end golden model: conv1 -> MaxPool -> conv2
#   make -f ce.mk          build the test
#   make -f ce.mk test     build and run
#   make -f ce.mk trace    run and write ce_conv{1,2}_trace.csv (first run)
#   make -f ce.mk vectors  regenerate vectors/ce_lenet5.txt from the Python dump
#   make -f ce.mk clean

CC      ?= cc
CFLAGS  ?= -std=c99 -Wall -Wextra -O1
PYTHON  ?= python

BUILD   := build
TARGET  := $(BUILD)/test_ce
VECTORS := vectors/ce_lenet5.txt

SRCS    := test_ce.c ce_top.c ce_lb_l1.c ce_lb_l2.c \
           total_ctrl_fsm_l2.c weight_addr_ctrl_l2.c weight_rom.c \
           output_buffer.c partial_sum.c buffer_ctrl.c bias_rom.c \
           relu_quant.c lane_packer.c output_fifo.c
HDRS    := common.h ce_top.h total_ctrl_fsm_l2.h weight_addr_ctrl_l2.h weight_rom.h \
           line_buffer.h line_buffer_array.h mac_unit.h mac_array.h \
           ob_common.h output_buffer.h partial_sum.h buffer_ctrl.h bias_rom.h \
           relu_quant.h lane_packer.h output_fifo.h

.PHONY: all test trace vectors clean

all: $(TARGET)

$(TARGET): $(SRCS) $(HDRS)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(SRCS) -o $@

test: $(TARGET)
	./$(TARGET) $(VECTORS)

trace: $(TARGET)
	./$(TARGET) $(VECTORS) -t

vectors:
	$(PYTHON) export_ce_vectors.py

clean:
	rm -rf $(BUILD) ce_conv1_trace.csv ce_conv2_trace.csv
