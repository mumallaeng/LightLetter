# Opens the routed fc_top in the Vivado GUI with the views to screenshot: utilization, timing summary, power
# (with the toggle rates of a SAIF when one is given) and the DSP cells highlighted in the device view.
# Usage: fc_gui.cmd [fc_synth\saif_v100_r80.saif]   (run vivado/fc_synth.tcl first)

set here [file dirname [file normalize [info script]]]
set out  [file join $here fc_synth]

open_checkpoint [file join $out fc_routed.dcp]

report_utilization -hierarchical -hierarchical_depth 2 -name util_fc
report_timing_summary -max_paths 10 -name timing_fc

if {[llength $argv] > 0} {
    read_saif -strip_path tb_fc_power/u_fc_top [file normalize [lindex $argv 0]]
}
report_power -name power_fc

catch {highlight_objects -color red [get_cells -hier -filter {REF_NAME == DSP48E1}]}
