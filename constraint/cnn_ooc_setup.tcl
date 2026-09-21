# One-time project setup for checking a CNN block on its own (out-of-context).
#   Vivado Tcl console:  source <repo>/constraint/cnn_ooc_setup.tcl
# Adds the bias ROM files and the clock constraint, switches synthesis to out-of-context,
# and hides the one message that is expected in this flow.

set repo [file normalize [file join [file dirname [info script]] ..]]

foreach f [glob -nocomplain $repo/rtl/cnn/mem/*.mem] {
    if {[llength [get_files -quiet $f]] == 0} { add_files -norecurse $f }
}
if {[llength [get_files -quiet $repo/constraint/cnn_ooc.xdc]] == 0} {
    add_files -fileset constrs_1 -norecurse $repo/constraint/cnn_ooc.xdc
}

set_property -name {STEPS.SYNTH_DESIGN.ARGS.MORE OPTIONS} -value {-mode out_of_context} -objects [get_runs synth_1]

# Every port of an out-of-context block is reported as lacking HD.PARTPIN_LOCS (one warning
# per port). Port locations only matter when the block is stitched into a parent design.
set_msg_config -id {Route 35-198} -suppress

reset_run synth_1
