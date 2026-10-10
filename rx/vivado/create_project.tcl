set here [file dirname [file normalize [info script]]]
set root [file dirname $here]
set dest [file join $here project]
if {[file exists [file join $dest rx_final.xpr]]} {
    error "Project already exists. Open vivado/project/rx_final.xpr; do not recreate it."
}
create_project rx_final $dest -part xc7z020clg400-1
set boards [get_board_parts -quiet digilentinc.com:zybo-z7-20:part0:*]
if {[llength $boards]} { set_property board_part [lindex $boards end] [current_project] }
set_property target_language Verilog [current_project]
set_property simulator_language Mixed [current_project]
foreach block {fft bfsk_rx snapshot} {
    foreach rtl_file [glob [file join $root $block rtl *.v]] {
        # 최상위 래퍼는 아래 make_wrapper에서 생성하므로 보관본은 등록하지 않는다.
        if {[file tail $rtl_file] ne "design_1_wrapper.v"} {
            add_files -norecurse $rtl_file
        }
    }
}
add_files -norecurse [file join $root fft rtl twiddle_128_q14.mem]
import_ip -files [file join $here ip xadc_wiz_0 xadc_wiz_0.xci]
add_files -fileset constrs_1 -norecurse [file join $here constraints Zybo-Z7-RX.xdc]
# Recreate IP and wiring from the original project export.
update_compile_order -fileset sources_1
source [file join $here recreate_bd.tcl]
validate_bd_design
save_bd_design
set wrapper [make_wrapper -files [get_files design_1.bd] -top]
add_files -norecurse $wrapper
set_property top design_1_wrapper [get_filesets sources_1]
update_compile_order -fileset sources_1
puts "RX_PROJECT_READY: [file join $dest rx_final.xpr]"
close_project

