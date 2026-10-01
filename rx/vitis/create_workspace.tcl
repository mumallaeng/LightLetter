set here [file dirname [file normalize [info script]]]
set root [file dirname $here]
set ws [file join $here workspace]
if {[file exists $ws]} { error "Workspace exists. Open it in Vitis instead of overwriting." }
setws $ws
platform create -name FFT_RX_PLATFORM -hw [file join $root vivado export FFT_RX_FINAL.xsa] -proc ps7_cortexa9_0 -os standalone
bsp config stdin ps7_uart_1
bsp config stdout ps7_uart_1
platform generate
app create -name BFSK_RX_UART -platform FFT_RX_PLATFORM -template {Empty Application}
importsources -name BFSK_RX_UART -path [file join $here src]
app build -name BFSK_RX_UART
puts "RX_WORKSPACE_READY: $ws"
exit
