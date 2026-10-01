set here [file dirname [file normalize [info script]]]
set root [file dirname $here]
set dest [file join $here project]
if {[file exists [file join $dest tx_top.xpr]]} {
    error "Project already exists. Open vivado/project/tx_top.xpr; do not recreate it."
}
create_project tx_top $dest -part xc7z020clg400-1
set boards [get_board_parts -quiet digilentinc.com:zybo-z7-20:part0:*]
if {[llength $boards]} { set_property board_part [lindex $boards end] [current_project] }
set_property target_language Verilog [current_project]
set_property simulator_language Mixed [current_project]
# Packaged IP: the CNN accelerator and the Digilent camera/HDMI cores.
set_property ip_repo_paths [list [file join $root cnn ip] [file join $root camera P-CAM_HDMI_IP ip_repo]] [current_project]
update_ip_catalog
# RTL that the block design instantiates as module references.
foreach block {preprocess bfsk_tx} {
    add_files -norecurse [glob [file join $root $block rtl *.v]]
}
add_files -fileset constrs_1 -norecurse [file join $here constraints Zybo-Z7-Master.xdc]
update_compile_order -fileset sources_1
source [file join $here tx_top_dma_preprocess_cnn.tcl]
validate_bd_design
save_bd_design
set wrapper [make_wrapper -files [get_files tx_top.bd] -top]
add_files -norecurse $wrapper
set_property top tx_top_wrapper [get_filesets sources_1]
update_compile_order -fileset sources_1
puts "TX_PROJECT_READY: [file join $dest tx_top.xpr]"
close_project
