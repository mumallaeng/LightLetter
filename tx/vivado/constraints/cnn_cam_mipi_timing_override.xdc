# OV5640 720p60 mode: MIPI HS clock = 280 MHz.
# DDR unit interval: 1 / (2 * 280 MHz) = 1.7857 ns.
# Digilent's receiver constraint uses setup/hold = 0.15 UI.

set hs_data_ports [get_ports {dphy_data_hs_n[*] dphy_data_hs_p[*]}]

# Remove the receiver IP's default constraints, which assume UI = 5 ns.
reset_input_delay $hs_data_ports

# Apply the 280 MHz DDR input window to both clock edges.
set_input_delay -clock [get_clocks dphy_hs_clock_clk_p] \
    -min 0.268 $hs_data_ports
set_input_delay -clock [get_clocks dphy_hs_clock_clk_p] \
    -max 1.518 $hs_data_ports
set_input_delay -clock [get_clocks dphy_hs_clock_clk_p] \
    -clock_fall -min -add_delay 0.268 $hs_data_ports
set_input_delay -clock [get_clocks dphy_hs_clock_clk_p] \
    -clock_fall -max -add_delay 1.518 $hs_data_ports
