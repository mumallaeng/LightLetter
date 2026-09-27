# Fully Connected golden model (step 2: shared engine, P = 20)
#   make -f fc.mk           build the test
#   make -f fc.mk test      build and run
#   make -f fc.mk vectors   regenerate vectors/fc*.txt and rtl/cnn/mem/fc_weight.mem, fc_bias.mem from the dump
#   make -f fc.mk rtl-vectors  golden stimulus/expected files for the RTL testbench (tb/cnn/vectors)
#   make -f fc.mk clean

CC      ?= cc
CFLAGS  ?= -std=c99 -Wall -Wextra -O1
PYTHON  ?= ../../tb/.venv/bin/python
DUMP    ?= ../../tb/cnn_golden/results/layer_outputs/lenet5_3x3_schedule.json

BUILD   := build
TARGET  := $(BUILD)/test_fc

MODEL   := fc_top.c fc_ctrl.c fc_act_buf.c fc_weight_rom.c fc_bias_rom.c fc_mac_acc.c fc_drain.c fc_vec_io.c
SRCS    := test_fc.c $(MODEL)
RTL_VEC := ../../tb/cnn/vectors
HDRS    := fc_common.h fc_top.h fc_ctrl.h fc_act_buf.h fc_weight_rom.h fc_bias_rom.h fc_mac_acc.h fc_drain.h fc_vec_io.h

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
