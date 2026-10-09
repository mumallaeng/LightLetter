@echo off
setlocal
cd /d "%~dp0"
set "BFSK_XSCT=C:\Xilinx\Vitis\2020.2\bin\xsct.bat"
if not exist "%BFSK_XSCT%" (
 echo ERROR: Vitis 2020.2 xsct.bat was not found.
 pause
 exit /b 1
)
call "%BFSK_XSCT%" "vitis\create_workspace.tcl"
set "BFSK_BUILD_RESULT=%ERRORLEVEL%"
if not "%BFSK_BUILD_RESULT%"=="0" (
 echo ERROR: TX build failed. See messages above.
) else (
 echo SUCCESS: Open tx\vitis\workspace in Vitis and run tx_fpga.
)
pause
exit /b %BFSK_BUILD_RESULT%
