# Out-of-context timing check for the CNN blocks (output_buffer, relu_quant, ...).
# Not a board constraint file: there are no pin locations here, because these modules sit
# inside the PL and their ports are not package pins. Use together with
#   synth_design -mode out_of_context
# (Settings > Synthesis > More Options), otherwise the placer tries to map every port
# to an I/O site and fails with [Place 30-58].

# 100 MHz placeholder - replace with the PL clock the team settles on.
create_clock -period 10.000 -name clk [get_ports clk]

# Out-of-context: tell the tools which global buffer will drive clk in the real design,
# so clock delay / skew can be estimated ([Timing 38-242], [Route 35-197]).
set_property HD.CLK_SRC BUFGCTRL_X0Y0 [get_ports clk]

# Module-only check: a Zynq part expects a processing_system7 (PS7) instance, which a
# stand-alone CNN block does not have ([DRC ZPS7-1]). Waive it rather than disabling the
# check: a disabled check is itself reported as a CHECK-1 violation, while a waived one
# leaves report_drc at zero violations and one waiver. Do not reuse this file in the
# integrated design - there a missing PS7 is a real error.
create_waiver -quiet -type DRC -id {ZPS7-1} \
    -description "module-only out-of-context check: no PS7 in a stand-alone CNN block"
