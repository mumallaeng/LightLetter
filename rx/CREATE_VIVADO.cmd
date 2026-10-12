@echo off
cd /d "%~dp0"
set "VIVADO_CMD=vivado"
if exist "C:\Xilinx\Vivado\2020.2\bin\vivado.bat" set "VIVADO_CMD=C:\Xilinx\Vivado\2020.2\bin\vivado.bat"
if defined XILINX_VIVADO set "VIVADO_CMD=%XILINX_VIVADO%\bin\vivado.bat"
call "%VIVADO_CMD%" -mode batch -source vivado\create_project.tcl
pause

