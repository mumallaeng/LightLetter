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
# Packaged IP: the CNN accelerator, rebuilt here from tx/cnn/rtl, and the Digilent camera/HDMI cores.
source [file join $here cnn_ip_from_rtl.tcl]
set cnn_repo [cnn_ip_from_rtl $root [file join $dest ip_repo]]
set_property ip_repo_paths [list $cnn_repo [file join $root camera P-CAM_HDMI_IP ip_repo]] [current_project]
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
# Simulation set: the Fully Connected bit-exact testbench with the RTL it instantiates.
# These go in sim_1 only; the CNN IP already carries the same RTL for synthesis, so adding
# them to sources_1 would define every module twice. Vivado copies the .mem files of the
# simulation set into the xsim run directory, which is where tb_fc and fc_top read them.
set sim_files [list [file join $root cnn tb tb_fc.v]]
foreach m {fc_top fc_ctrl fc_mac fc_quant_out fc_feature_buf fc_weight_rom fc_bias_rom relu quantizer quantizer_signed} {
    lappend sim_files [file join $root cnn rtl $m.v]
}
foreach m {fc_weight fc_bias} { lappend sim_files [file join $root cnn rtl mem $m.mem] }
foreach m {fc_stim fc1_out fc2_out fc3_out} { lappend sim_files [file join $root cnn tb vectors $m.mem] }
add_files -fileset sim_1 -norecurse $sim_files
set_property top tb_fc [get_filesets sim_1]
set_property top_lib xil_defaultlib [get_filesets sim_1]
set_property -name {xsim.simulate.runtime} -value {10ms} -objects [get_filesets sim_1]
update_compile_order -fileset sim_1
puts "TX_PROJECT_READY: [file join $dest tx_top.xpr]"
close_project
