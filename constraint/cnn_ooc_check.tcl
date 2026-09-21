# Out-of-context synthesis + implementation check of the CNN output-stage blocks (non-project mode).
#   vivado -mode batch -source <repo>/constraint/cnn_ooc_check.tcl
# Does not touch any .xpr. Reports go to <repo>/tb/cnn/build/ooc_check/<config>/, summary.txt next to them.

set repo [file normalize [file join [file dirname [info script]] ..]]
set part xc7z020clg400-1
set out  [file join $repo tb cnn build ooc_check]
file mkdir $out

# $readmemh uses bare file names, so run from the folder that holds the .mem files
cd [file join $repo rtl cnn mem]

# one warning per port in out-of-context mode; port locations only matter inside a parent design
set_msg_config -id {Route 35-198} -suppress

set configs {
    {ob_conv2    output_buffer        {}}
    {ob_conv1    output_buffer        {N=676 C_OUT=6 NUM_GROUPS=1 BIAS_FILE="conv1_bias.mem"}}
    {rq_conv1    relu_quant           {PACK=3}}
    {rq_conv2    relu_quant           {PACK=1}}
    {stage_conv1 conv_out_stage_synth {LAYER=1}}
    {stage_conv2 conv_out_stage_synth {LAYER=2}}
}

set summary {}
lappend summary [format "%-12s %8s %8s %6s %6s %6s %5s %5s  %s" config WNS(ns) WHS(ns) LUT FF BRAM warn crit "worst setup path (logic levels)"]

foreach cfg $configs {
    lassign $cfg name top generics
    set dir [file join $out $name]
    file mkdir $dir

    set warn0 [get_msg_config -severity {WARNING} -count]
    set crit0 [get_msg_config -severity {CRITICAL WARNING} -count]

    read_verilog [glob $repo/rtl/cnn/*.v]
    read_verilog $repo/tb/cnn/synth/conv_out_stage_synth.v
    read_xdc     $repo/constraint/cnn_ooc.xdc

    set args {}
    foreach g $generics { lappend args -generic $g }
    synth_design -top $top -part $part -mode out_of_context {*}$args
    opt_design
    place_design
    route_design

    report_timing_summary -file [file join $dir timing_summary.rpt]
    report_utilization    -file [file join $dir utilization.rpt]
    report_timing -setup -max_paths 1 -file [file join $dir worst_setup_path.rpt]

    set sp  [get_timing_paths -setup -max_paths 1]
    set hp  [get_timing_paths -hold  -max_paths 1]
    set wns [expr {[llength $sp] ? [get_property SLACK $sp] : "n/a"}]
    set whs [expr {[llength $hp] ? [get_property SLACK $hp] : "n/a"}]
    set path "n/a"
    if {[llength $sp]} {
        set path "[get_property STARTPOINT_PIN $sp] -> [get_property ENDPOINT_PIN $sp] ([get_property LOGIC_LEVELS $sp])"
    }
    set lut  [llength [get_cells -hier -filter {REF_NAME =~ LUT*}]]
    set ff   [llength [get_cells -hier -filter {REF_NAME =~ FD*}]]
    set b36  [llength [get_cells -hier -filter {REF_NAME =~ RAMB36*}]]
    set b18  [llength [get_cells -hier -filter {REF_NAME =~ RAMB18*}]]
    set bram [expr {$b36 + 0.5 * $b18}]
    set warn [expr {[get_msg_config -severity {WARNING} -count] - $warn0}]
    set crit [expr {[get_msg_config -severity {CRITICAL WARNING} -count] - $crit0}]

    lappend summary [format "%-12s %8s %8s %6d %6d %6s %5d %5d  %s" $name $wns $whs $lut $ff $bram $warn $crit $path]
    close_design
    remove_files [get_files]
}

set fh [open [file join $out summary.txt] w]
foreach line $summary { puts $fh $line; puts "OOC_SUMMARY: $line" }
close $fh
