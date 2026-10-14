# Vivado power report of fc_top with the toggle rates of a SAIF recorded by cnn/tb/fc_saif.sh.
# Usage: vivado -mode batch -source vivado/fc_power.tcl -tclargs fc_synth/saif_v100_r80.saif
# The report lands next to the checkpoint; check "Design Nets Matched" in its Confidence Level table,
# a low match means the SAIF names did not line up with the netlist.

set here [file dirname [file normalize [info script]]]
set out  [file join $here fc_synth]
set saif [file normalize [lindex $argv 0]]

open_checkpoint [file join $out fc_routed.dcp]
create_clock -name clk -period 10.0 [get_ports clk]

# the testbench instance is tb_fc_power/u_fc_top, the checkpoint top is fc_top itself
read_saif -strip_path tb_fc_power/u_fc_top $saif
report_power -file [file join $out power_[file rootname [file tail $saif]].rpt]

puts "FC_POWER_DONE: $out"
