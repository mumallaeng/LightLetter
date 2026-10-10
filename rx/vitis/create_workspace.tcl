# 팀 원본 소스와 XSA를 보존하고 생성 워크스페이스만 갱신한다.
set here [file dirname [file normalize [info script]]]
set root [file dirname $here]
set ws [file join $here workspace]
if {[info exists ::env(BFSK_VITIS_WORKSPACE)] && $::env(BFSK_VITIS_WORKSPACE) ne ""} {
    set ws [file normalize $::env(BFSK_VITIS_WORKSPACE)]
}
set xsa [file join $root vivado export FFT_RX_FINAL.xsa]
set src [file join $here src]
if {[catch {
    foreach path [list $xsa [file join $src main.c] [file join $src lscript.ld]] {
        if {![file exists $path]} { error "Required input not found: $path" }
    }
    puts "WORKSPACE: $ws"
    puts "XSA: $xsa"
    setws $ws
    if {[file exists [file join $ws FFT_RX_PLATFORM platform.spr]]} {
        platform active FFT_RX_PLATFORM
        platform config -updatehw $xsa
    } else {
        platform create -name FFT_RX_PLATFORM -hw $xsa -proc ps7_cortexa9_0 -os standalone
    }
    bsp config stdin ps7_uart_1
    bsp config stdout ps7_uart_1
    platform generate
    if {![file exists [file join $ws BFSK_RX_UART .project]]} {
        app create -name BFSK_RX_UART -platform FFT_RX_PLATFORM -template {Empty Application}
        # 신규 프로젝트는 원본을 연결한다. 기존 프로젝트의 사용자 소스는 덮어쓰지 않는다.
        importsources -name BFSK_RX_UART -path $src -soft-link -linker-script
    }
    app config -name BFSK_RX_UART build-config Debug
    app config -name BFSK_RX_UART linker-script [file join $src lscript.ld]
    app build -name BFSK_RX_UART
    set elf [file join $ws BFSK_RX_UART Debug BFSK_RX_UART.elf]
    if {![file exists $elf]} { error "ELF not found after build: $elf" }
    # 플랫폼 갱신만으로 남을 수 있는 이전 디버그용 하드웨어 복사본을 교체한다.
    set hw [file join $ws FFT_RX_PLATFORM hw]
    set ide [file join $ws BFSK_RX_UART _ide]
    foreach pair {{FFT_RX_FINAL.bit bitstream} {ps7_init.tcl psinit}} {
        lassign $pair name folder
        set input [file join $hw $name]
        if {![file exists $input]} { error "Hardware file missing; export XSA with bitstream: $input" }
        file mkdir [file join $ide $folder]
        file copy -force $input [file join $ide $folder $name]
    }
    if {[info exists ::env(BFSK_VITIS_SUCCESS_FILE)]} {
        set marker [open $::env(BFSK_VITIS_SUCCESS_FILE) w]
        puts $marker $elf
        close $marker
    }
    puts "BUILD_SUCCESS: $elf"
} message options]} {
    puts stderr "BUILD_FAILED: $message"
    if {[dict exists $options -errorinfo]} { puts stderr [dict get $options -errorinfo] }
    exit 1
}
exit 0
