# Synthesize, place and route fc_top by itself with the CNN clock (FCLK_CLK0, 100 MHz) so its DSP,
# LUT, register and timing numbers do not depend on the rest of tx_top.
# Usage: vivado -mode batch -source vivado/fc_synth.tcl ; reports land in vivado/fc_synth/.

set here [file dirname [file normalize [info script]]]
set root [file dirname $here]
set out  [file join $here fc_synth]
set rtl  [file join $root cnn rtl]
set period 10.0
file mkdir $out

foreach m {fc_top fc_ctrl fc_mac fc_quant_out fc_feature_buf fc_weight_rom fc_bias_rom relu quantizer quantizer_signed} {
    read_verilog [file join $rtl $m.v]
}

# $readmemh in the ROMs resolves its file name against the working directory
cd [file join $rtl mem]

synth_design -top fc_top -part xc7z020clg400-1 -mode out_of_context
create_clock -name clk -period $period [get_ports clk]

opt_design
place_design
route_design

report_utilization -hierarchical -hierarchical_depth 2 -file [file join $out utilization.rpt]
report_timing_summary -max_paths 10 -file [file join $out timing_summary.rpt]
report_timing -max_paths 10 -nworst 1 -sort_by slack -file [file join $out timing_paths.rpt]

# which DSP48E1 registers and ports the synthesizer used: PREG/MREG/CREG = 1 means the P, M and C stages sit in the DSP
set f [open [file join $out dsp_config.txt] w]
puts $f "cell AREG BREG CREG MREG PREG USE_MULT"
foreach c [get_cells -hier -filter {REF_NAME == DSP48E1}] {
    puts $f "[get_property NAME $c] [get_property AREG $c] [get_property BREG $c] [get_property CREG $c] [get_property MREG $c] [get_property PREG $c] [get_property USE_MULT $c]"
}
close $f

puts "FC_SYNTH_DONE: $out"
