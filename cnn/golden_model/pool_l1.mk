# pool_l1 (2x2 stride 2 max pooling, conv_l1 -> conv_l2) golden model test
#   make -f pool_l1.mk          build the test
#   make -f pool_l1.mk test     build and run (7 input scenarios x 5 handshake conditions)
#   make -f pool_l1.mk log      clock-by-clock log: logs/pool_l1_<SC>_<HS>.log (readable) and .csv
#                               default SC=real HS=A; e.g. make -f pool_l1.mk log SC=maxpos HS=E
#                               (SC: real pattern maxpos random ties extreme ch_done_off all, HS: A B C D E all)
#   make -f pool_l1.mk out      log + frame 0 output maps: logs/pool_l1_<SC>_<HS>_f0_out.txt / .csv (Python 비교 포함)
#   make -f pool_l1.mk test_l2  same DUT with pool_l2 parameters (11x11 -> 5x5, LANES 1, 16 channels, vectors/conv_l2.txt)
#   make -f pool_l1.mk log_l2   clock log for pool_l2: logs/pool_l2_<SC>_<HS>.log / .csv
#   make -f pool_l1.mk clean

CC      ?= cc
CFLAGS  ?= -std=c99 -Wall -Wextra -O1
PYTHON  ?= python

BUILD   := build
TARGET  := $(BUILD)/test_pool_l1
TARGET2 := $(BUILD)/test_pool_l2
VECTORS := vectors/conv_l1.txt
VECTORS2:= vectors/conv_l2.txt
SC      ?= real
HS      ?= A

SRCS    := test_pool_l1.c pool_l1.c pool_l1_ctrl.c pool_l1_datapath.c
HDRS    := pool_l1.h pool_l1_ctrl.h pool_l1_datapath.h

.PHONY: all test log out test_l2 log_l2 clean

all: $(TARGET) $(TARGET2)

$(TARGET): $(SRCS) $(HDRS)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(SRCS) -o $@

$(TARGET2): $(SRCS) $(HDRS)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -DPOOL_TB_L2 $(SRCS) -o $@

test: $(TARGET)
	./$(TARGET) $(VECTORS)

log: $(TARGET)
	./$(TARGET) $(VECTORS) -l -s $(SC) -h $(HS)

test_l2: $(TARGET2)
	./$(TARGET2) $(VECTORS2)

log_l2: $(TARGET2)
	./$(TARGET2) $(VECTORS2) -l -s $(SC) -h $(HS)

out: log
	$(PYTHON) extract_frame_out.py logs/pool_l1_$(SC)_$(HS).csv

clean:
	rm -f $(TARGET) $(TARGET2) logs/pool_l1_* logs/pool_l2_*
