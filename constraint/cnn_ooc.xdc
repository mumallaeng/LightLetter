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
