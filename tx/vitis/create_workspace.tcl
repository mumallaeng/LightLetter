# 기존 원본 프로젝트는 보존하고 별도 작업 공간에서 생성 또는 갱신한다.
set here [file dirname [file normalize [info script]]]
set root [file dirname $here]
set ws [file join $here workspace]
set xsa [file join $root vivado export tx_top_wrapper.xsa]
set src [file join $here tx_fpga src]
if {[catch {
    if {![file exists $xsa]} { error "XSA not found: $xsa" }
    if {![file exists [file join $src main.c]]} { error "TX source not found: $src" }
    puts "TX_XSA: $xsa"
    puts "TX_SOURCE: $src"
    puts "TX_WORKSPACE: $ws"
    setws $ws
    if {[file exists [file join $ws tx_top_wrapper platform.spr]]} {
        platform active tx_top_wrapper
        platform config -updatehw $xsa
    } else {
        platform create -name tx_top_wrapper -hw $xsa -proc ps7_cortexa9_0 -os standalone
    }
    bsp config stdin ps7_uart_1
    bsp config stdout ps7_uart_1
    platform generate
    if {![file exists [file join $ws tx_fpga .project]]} {
        app create -name tx_fpga -platform tx_top_wrapper -template {Empty Application}
        # 원본 소스를 연결하므로 400 us 설정 등 이후 수정도 재빌드에 반영한다.
        importsources -name tx_fpga -path $src -soft-link -linker-script
    }
    app config -name tx_fpga build-config Debug
    app config -name tx_fpga linker-script [file join $src lscript.ld]
    app build -name tx_fpga
    set elf [file join $ws tx_fpga Debug tx_fpga.elf]
    if {![file exists $elf]} { error "ELF not found after build: $elf" }
    puts "TX_BUILD_SUCCESS: $elf"
} message options]} {
    puts stderr "TX_BUILD_FAILED: $message"
    if {[dict exists $options -errorinfo]} { puts stderr [dict get $options -errorinfo] }
    exit 1
}
exit 0
