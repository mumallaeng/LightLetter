# Output Buffer / ReLU & Quantization golden model
#   make -f output_buffer.mk          build the test
#   make -f output_buffer.mk test     build and run
#   make -f output_buffer.mk vectors  regenerate vectors/ from the Python dump
#   make -f output_buffer.mk clean

CC      ?= cc
CFLAGS  ?= -std=c99 -Wall -Wextra -O1
PYTHON  ?= ../../tb/.venv/bin/python

BUILD   := build
TARGET  := $(BUILD)/test_output_buffer

SRCS    := test_output_buffer.c output_buffer.c partial_sum.c buffer_ctrl.c bias_rom.c \
           relu_quant.c lane_packer.c output_fifo.c
HDRS    := ob_common.h output_buffer.h partial_sum.h buffer_ctrl.h bias_rom.h \
           relu_quant.h lane_packer.h output_fifo.h

.PHONY: all test vectors clean

all: $(TARGET)

$(TARGET): $(SRCS) $(HDRS)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(SRCS) -lm -o $@

test: $(TARGET)
	./$(TARGET) vectors

vectors:
	$(PYTHON) export_ob_vectors.py

clean:
	rm -rf $(BUILD)
