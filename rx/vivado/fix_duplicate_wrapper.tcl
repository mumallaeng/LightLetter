# 열린 RX 프로젝트에서 보관용 래퍼 등록만 제거한다. 원본 파일은 삭제하지 않는다.
set here [file dirname [file normalize [info script]]]
set expected [file normalize [file join $here project]]
if {[file normalize [get_property DIRECTORY [current_project]]] ne $expected} {
    error "현재 프로젝트가 LightLetter RX가 아닙니다."
}
set old_path [file normalize [file join $here .. bfsk_rx rtl design_1_wrapper.v]]
set generated_path [file normalize [file join $here project rx_final.gen sources_1 bd design_1 hdl design_1_wrapper.v]]
if {![file exists $generated_path]} { error "자동 생성 래퍼를 찾지 못했습니다." }
set generated [get_files -quiet $generated_path]
if {[llength $generated] != 1} { error "자동 생성 래퍼가 프로젝트에 등록되어 있지 않습니다." }
set old [get_files -quiet $old_path]
if {[llength $old]} { remove_files $old }
set_property top design_1_wrapper [get_filesets sources_1]
update_compile_order -fileset sources_1
update_compile_order -fileset sim_1
puts "WRAPPER_FIX_COMPLETE: 보관용 래퍼 등록 제거 완료, 원본 파일 보존."
