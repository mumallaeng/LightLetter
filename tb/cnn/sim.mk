# RTL simulation for the CNN Output Buffer / ReLU & Quantization and Fully Connected (Icarus Verilog)
#   make -f sim.mk            run the bit-exact test for conv1 and conv2
#   make -f sim.mk fc         run the bit-exact test for fc_top
#   make -f sim.mk vectors    regenerate vectors/ from the C golden model
#   make -f sim.mk fc-vectors regenerate the Fully Connected vectors
#   make -f sim.mk wave       conv2 run with a VCD (build/conv2.vcd)
#   make -f sim.mk clean

IVERILOG ?= iverilog
VVP      ?= vvp
IVFLAGS  ?= -g2005 -Wall

RTL   := ../../rtl/cnn
BUILD := build
SRCS  := $(wildcard $(RTL)/*.v)

CONV1 := -Ptb_output_buffer.N=676 -Ptb_output_buffer.C_OUT=6 -Ptb_output_buffer.NUM_GROUPS=1 \
         -Ptb_output_buffer.PACK=3 -Ptb_output_buffer.SCALE_EXP=16 \
         -Ptb_output_buffer.NSTIM=8112 -Ptb_output_buffer.NSUM=8112 -Ptb_output_buffer.NOUT=2704 \
         '-Ptb_output_buffer.BIAS_FILE="../../rtl/cnn/mem/conv1_bias.mem"' '-Ptb_output_buffer.STIM_FILE="vectors/conv1_stim.mem"' \
         '-Ptb_output_buffer.SUM_FILE="vectors/conv1_sum.mem"' '-Ptb_output_buffer.OUT_FILE="vectors/conv1_out.mem"'
CONV2 :=

# $(call run_fc,name,valid%,ready%,seed)
define run_fc
	@$(IVERILOG) $(IVFLAGS) -s tb_fc -o $(BUILD)/$(1).vvp \
	    -Ptb_fc.VALID_PCT=$(2) -Ptb_fc.READY_PCT=$(3) -Ptb_fc.SEED=$(4) \
	    '-Ptb_fc.VCD_FILE="$(BUILD)/$(1).vcd"' tb_fc.v $(SRCS)
	@$(VVP) -n $(BUILD)/$(1).vvp $(5) | grep -E "^\[|FAIL|rtl "
endef

# $(call run_ob,name,config,valid%,ready%,seed)
define run_ob
	@$(IVERILOG) $(IVFLAGS) -s tb_output_buffer -o $(BUILD)/$(1).vvp $(2) \
	    -Ptb_output_buffer.VALID_PCT=$(3) -Ptb_output_buffer.READY_PCT=$(4) -Ptb_output_buffer.SEED=$(5) \
	    '-Ptb_output_buffer.VCD_FILE="$(BUILD)/$(1).vcd"' tb_output_buffer.v $(SRCS)
	@$(VVP) -n $(BUILD)/$(1).vvp $(6) | grep -E "^\[|FAIL|rtl "
endef

.PHONY: all fc vectors fc-vectors wave clean
.DEFAULT_GOAL := all

$(BUILD):
	@mkdir -p $(BUILD)

all: | $(BUILD)
	$(call run_ob,conv1_a,$(CONV1),100,100,1)
	$(call run_ob,conv1_b,$(CONV1),75,35,2)
	$(call run_ob,conv1_c,$(CONV1),40,5,3)
	$(call run_ob,conv2_a,$(CONV2),100,100,1)
	$(call run_ob,conv2_b,$(CONV2),75,35,2)
	$(call run_ob,conv2_c,$(CONV2),40,60,3)

fc: | $(BUILD)
	$(call run_fc,fc_a,100,80,1)
	$(call run_fc,fc_b,75,35,2)
	$(call run_fc,fc_c,40,60,3)
	$(call run_fc,fc_d,100,100,4)
	$(call run_fc,fc_e,30,15,5)

wave: | $(BUILD)
	$(call run_ob,conv2,$(CONV2),75,35,2,+vcd)
	@echo "wrote $(BUILD)/conv2.vcd"

vectors:
	$(MAKE) -C ../../cnn/golden_model -f output_buffer.mk rtl-vectors

fc-vectors:
	$(MAKE) -C ../../cnn/golden_model -f fc.mk rtl-vectors

clean:
	rm -rf $(BUILD)
