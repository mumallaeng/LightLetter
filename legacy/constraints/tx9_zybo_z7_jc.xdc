# ==============================================================================
# TX-9 Hardware Verification - Zybo Z7-20 PMOD JC
#
# 사용:
#   optical_tx -> JC Pin 1
#   tx_enable  -> JC Pin 2
#
# 주의:
#   AXI/TX Clock 및 Reset은 Zynq PS의 FCLK / Processor System Reset에서 공급하므로
#   이 XDC에서는 외부 Clock/Reset 핀을 지정하지 않는다.
#
# Zybo Z7 Rev.B / Zybo Z7-20 기준
# ==============================================================================

# JC Pin 1 : optical_tx
set_property -dict { PACKAGE_PIN V15 IOSTANDARD LVCMOS33 } [get_ports { optical_tx }]

# JC Pin 2 : tx_enable
set_property -dict { PACKAGE_PIN W15 IOSTANDARD LVCMOS33 } [get_ports { tx_enable }]
