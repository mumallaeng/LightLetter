# Fresh Vitis 2020.2 workspace for the TX FPGA app, built from an exported XSA.
#
#   xsct create_tx_fpga_ws.tcl [xsa] [workspace]
#
#     xsa        default: <repo>/hw/tx_top/tx_top_wrapper.xsa
#     workspace  default: <repo>/vitis/tx_fpga_ws   (must not exist yet)
#
# Creates platform tx_top_wrapper (standalone_ps7_cortexa9_0 + zynq_fsbl),
# app tx_fpga, and builds it. The app sources are not copied: src is
# soft-linked to vitis/tx_fpga/tx_fpga/src (the copy tracked in git), so
# edits made in this workspace land in the repo.
#
# After a new XSA (e.g. cnn_ip re-packaged), either run this again into a new
# workspace, or in the existing one:
#   platform active tx_top_wrapper; platform config -updatehw <xsa>; platform generate

set here [file dirname [file normalize [info script]]]
set repo [file dirname $here]
set src  [file join $here tx_fpga tx_fpga src]

set xsa [file join $repo hw tx_top tx_top_wrapper.xsa]
set ws  [file join $here tx_fpga_ws]
if {$argc > 0} { set xsa [file normalize [lindex $argv 0]] }
if {$argc > 1} { set ws  [file normalize [lindex $argv 1]] }

if {![file exists $xsa]} {
    error "XSA not found: $xsa"
}
if {![file isdirectory $src]} {
    error "app sources not found: $src"
}
if {[file exists $ws]} {
    error "workspace already exists: $ws (remove it or pass another path)"
}

# The BSP copies driver sources several folders deep; past Windows' 260-char
# path limit the copy fails and the platform comes out without xparameters.h.
if {[string length $ws] > 100} {
    puts "WARNING: workspace path is [string length $ws] chars, keep it short (Windows path limit)"
}

puts "XSA       : $xsa"
puts "workspace : $ws"
puts "sources   : $src (soft-linked)"

# xsct exits 0 even when a script command fails, so fail explicitly.
if {[catch {
    file mkdir $ws
    setws $ws

    # same steps as the original vitis/tx_fpga/tx_top_wrapper/platform.tcl
    platform create -name tx_top_wrapper -hw $xsa -out $ws
    platform write
    domain create -name standalone_ps7_cortexa9_0 -display-name standalone_ps7_cortexa9_0 \
        -os standalone -proc ps7_cortexa9_0 -runtime cpp -arch 32-bit -support-app empty_application
    platform write
    platform generate

    app create -name tx_fpga -platform tx_top_wrapper -domain standalone_ps7_cortexa9_0 \
        -template {Empty Application} -lang c
    importsources -name tx_fpga -path $src -soft-link
    # The soft link leaves out the linker script, but the generated makefile
    # expects it as a real file at tx_fpga/src/lscript.ld.
    file copy -force [file join $src lscript.ld] [file join $ws tx_fpga src lscript.ld]

    app build -name tx_fpga

    set elf [file join $ws tx_fpga Debug tx_fpga.elf]
    if {![file exists $elf]} {
        error "build finished without $elf"
    }
} err]} {
    puts stderr "FAILED: $err"
    exit 1
}
puts "done: $elf"
