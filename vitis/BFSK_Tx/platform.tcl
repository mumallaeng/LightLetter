# 
# Usage: To re-create this platform project launch xsct with below options.
# xsct D:\Working\OnDeviceAi_2\01_PROJECT\01_FINAL_PROJECT\BFSK_Tx\vitis\BFSK_Tx\platform.tcl
# 
# OR launch xsct and run below command.
# source D:\Working\OnDeviceAi_2\01_PROJECT\01_FINAL_PROJECT\BFSK_Tx\vitis\BFSK_Tx\platform.tcl
# 
# To create the platform in a different location, modify the -out option of "platform create" command.
# -out option specifies the output directory of the platform project.

platform create -name {BFSK_Tx}\
-hw {D:\Working\OnDeviceAi_2\01_PROJECT\01_FINAL_PROJECT\BFSK_Tx\tb_xpr\tb_optical_tx_axi_top\XSA\design_1_wrapper.xsa}\
-proc {ps7_cortexa9_0} -os {standalone} -fsbl-target {psu_cortexa53_0} -out {D:/Working/OnDeviceAi_2/01_PROJECT/01_FINAL_PROJECT/BFSK_Tx/vitis}

platform write
platform generate -domains 
platform active {BFSK_Tx}
platform generate
platform clean
platform generate
platform active {BFSK_Tx}
platform clean
platform generate
platform clean
platform generate
platform clean
platform generate
platform clean
platform generate
