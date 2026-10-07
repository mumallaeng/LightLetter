# Waves of the fc_top handshake for a screenshot: input stream, logit stream and the top-level signals of fc_top.
add_wave -radix unsigned /tb_fc_power/rst_n /tb_fc_power/fc_in_valid /tb_fc_power/fc_in_ready /tb_fc_power/fc_in_data
add_wave -radix unsigned /tb_fc_power/logit_valid /tb_fc_power/logit_ready /tb_fc_power/logit_data
add_wave -radix unsigned /tb_fc_power/u_fc_top/*
run all
if {[info exists ::env(FC_NOGUI)]} { quit }
