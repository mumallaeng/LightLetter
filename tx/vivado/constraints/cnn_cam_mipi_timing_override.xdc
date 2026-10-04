# OV5640 720p60 mode: MIPI HS clock = 280 MHz.
# DDR unit interval: 1 / (2 * 280 MHz) = 1.7857 ns.
# Digilent's receiver constraint uses setup/hold = 0.15 UI.

set hs_data_ports [get_ports {dphy_data_hs_n[*] dphy_data_hs_p[*]}]

# The receiver IP's defaults assume UI = 5 ns. A set_input_delay without -add_delay
# replaces every earlier input delay on these ports; the falling edge is then added.
# The IP applies its defaults with PROCESSING_ORDER LATE, so this file must be LATE too.
set_input_delay -clock [get_clocks dphy_hs_clock_clk_p] \
    -min 0.268 $hs_data_ports
set_input_delay -clock [get_clocks dphy_hs_clock_clk_p] \
    -max 1.518 $hs_data_ports
set_input_delay -clock [get_clocks dphy_hs_clock_clk_p] \
    -clock_fall -min -add_delay 0.268 $hs_data_ports
set_input_delay -clock [get_clocks dphy_hs_clock_clk_p] \
    -clock_fall -max -add_delay 1.518 $hs_data_ports
