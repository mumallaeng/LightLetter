@echo off
rem Opens the routed fc_top in the Vivado GUI for screenshots (utilization, timing, power, DSP placement).
rem   fc_gui.cmd                           reports without switching activity
rem   fc_gui.cmd fc_synth\saif_v100_r80.saif   power with the toggle rates of that SAIF
setlocal
cd /d "%~dp0"
set VIVADO=C:\Xilinx\Vivado\2020.2\bin\vivado.bat
if "%~1"=="" (
    call "%VIVADO%" -mode gui -source fc_gui.tcl
) else (
    call "%VIVADO%" -mode gui -source fc_gui.tcl -tclargs "%~1"
)
