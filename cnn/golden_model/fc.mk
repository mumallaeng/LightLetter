# Fully Connected golden model (stage 1: reuses Output Buffer / ReLU & Quantization)
#   make -f fc.mk           build the test
#   make -f fc.mk test      build and run
#   make -f fc.mk vectors   regenerate vectors/fc*.txt and rtl/cnn/mem/fc*.mem from the dump
#   make -f fc.mk rtl-vectors  golden stimulus/expected files for the RTL testbench (tb/cnn/vectors)
#   make -f fc.mk clean

CC      ?= cc
CFLAGS  ?= -std=c99 -Wall -Wextra -O1 -DOB_MAX_C_OUT=120
PYTHON  ?= ../../tb/.venv/bin/python
DUMP    ?= ../../tb/cnn_golden/results/layer_outputs/lenet5_3x3_schedule.json

BUILD   := build
TARGET  := $(BUILD)/test_fc

MODEL   := fc_top.c fc_layer.c fc_staging.c fc_ctrl.c fc_weight_rom.c fc_mac.c fc_quant_signed.c \
           output_buffer.c partial_sum.c buffer_ctrl.c bias_rom.c relu_quant.c lane_packer.c \
           out_reorder.c
SRCS    := test_fc.c $(MODEL)
RTL_VEC := ../../tb/cnn/vectors
HDRS    := fc_common.h fc_top.h fc_layer.h fc_staging.h fc_ctrl.h fc_weight_rom.h fc_mac.h \
           fc_quant_signed.h ob_common.h output_buffer.h partial_sum.h buffer_ctrl.h \
           bias_rom.h relu_quant.h lane_packer.h out_reorder.h

.PHONY: all test vectors rtl-vectors clean

all: $(TARGET)

$(TARGET): $(SRCS) $(HDRS)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(SRCS) -lm -o $@

test: $(TARGET)
	./$(TARGET) vectors

vectors:
	$(PYTHON) export_fc_vectors.py $(DUMP)

$(BUILD)/gen_fc_rtl_vectors: gen_fc_rtl_vectors.c $(MODEL) $(HDRS)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) gen_fc_rtl_vectors.c $(MODEL) -lm -o $@

rtl-vectors: $(BUILD)/gen_fc_rtl_vectors
	@mkdir -p $(RTL_VEC)
	./$(BUILD)/gen_fc_rtl_vectors vectors $(RTL_VEC)

clean:
	rm -rf $(BUILD)
