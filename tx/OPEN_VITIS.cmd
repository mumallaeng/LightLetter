@echo off
setlocal
set "BFSK_VITIS=C:\Xilinx\Vitis\2020.2\bin\vitis.bat"
if not defined BFSK_VITIS_WORKSPACE set "BFSK_VITIS_WORKSPACE=%~dp0vitis\workspace"
if not exist "%BFSK_VITIS%" (
 echo ERROR: Vitis 2020.2 was not found.
 pause
 exit /b 1
)
if not exist "%BFSK_VITIS_WORKSPACE%\tx_fpga\.project" (
 echo ERROR: Run CREATE_VITIS.cmd successfully first.
 pause
 exit /b 1
)
call "%BFSK_VITIS%" -workspace "%BFSK_VITIS_WORKSPACE%"
