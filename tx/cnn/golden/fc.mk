# Fully Connected golden model (step 2: shared engine, P = 20)
#   make -f fc.mk           build the test
#   make -f fc.mk test      build and run
#   make -f fc.mk vectors   regenerate vectors/fc*.txt and tx/cnn/rtl/mem/fc_weight.mem, fc_bias.mem from the dump
#   make -f fc.mk rtl-vectors  golden stimulus/expected files for the RTL testbench (tx/cnn/tb/vectors)
#   make -f fc.mk clean

CC      ?= cc
CFLAGS  ?= -std=c99 -Wall -Wextra -O1
PYTHON  ?= ../model/.venv/bin/python
DUMP    ?= ../model/cnn_golden/results/layer_outputs/lenet5_3x3_schedule.json

BUILD   := build
TARGET  := $(BUILD)/test_fc

MODEL   := fc_top.c fc_ctrl.c fc_feature_buf.c fc_weight_rom.c fc_bias_rom.c fc_mac.c fc_quant_out.c fc_vec_io.c
SRCS    := test_fc.c $(MODEL)
RTL_VEC := ../tb/vectors
HDRS    := fc_common.h fc_top.h fc_ctrl.h fc_feature_buf.h fc_weight_rom.h fc_bias_rom.h fc_mac.h fc_quant_out.h fc_vec_io.h

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
