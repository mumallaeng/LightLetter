# 
# Usage: To re-create this platform project launch xsct with below options.
# xsct D:\OndeviceAI2\CNN_CAM\vitis\cnn_cam_platform\platform.tcl
# 
# OR launch xsct and run below command.
# source D:\OndeviceAI2\CNN_CAM\vitis\cnn_cam_platform\platform.tcl
# 
# To create the platform in a different location, modify the -out option of "platform create" command.
# -out option specifies the output directory of the platform project.

platform create -name {cnn_cam_platform}\
-hw {D:\OndeviceAI2\CNN_CAM\xsa\cnn_cam_base.xsa}\
-proc {ps7_cortexa9_0} -os {standalone} -fsbl-target {psu_cortexa53_0} -out {D:/OndeviceAI2/CNN_CAM/vitis}

platform write
platform generate -domains 
platform active {cnn_cam_platform}
platform generate
platform active {cnn_cam_platform}
platform config -updatehw {D:/OndeviceAI2/CNN_CAM/xsa/cnn_cam_base.xsa}
platform generate -domains 
platform clean
platform generate
