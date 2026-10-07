@echo off
rem RTL waveform of fc_top (tb_fc_power) in the xsim GUI.
rem   fc_wave.cmd [VALID READY SEED]     e.g. fc_wave.cmd 40 60 3, default 100 80 1
rem   set FC_NOGUI=1 to run without the GUI (checks that the flow works)
setlocal
cd /d "%~dp0"
set BIN=C:\Xilinx\Vivado\2020.2\bin
set V=%1
set R=%2
set S=%3
if "%V%"=="" set V=100
if "%R%"=="" set R=80
if "%S%"=="" set S=1
if not exist build\wave mkdir build\wave
copy /y ..\rtl\mem\fc*.mem build\wave >nul
copy /y vectors\fc_stim.mem build\wave >nul
copy /y vectors\fc3_out.mem build\wave >nul
cd build\wave
(for %%f in (..\..\..\rtl\*.v) do @echo %%f) > files.f
echo ..\..\tb_fc_power.v>> files.f
call "%BIN%\xvlog.bat" --nolog -f files.f || exit /b 1
(echo --nolog& echo -debug& echo typical& echo tb_fc_power& echo -s snap_wave) > xelab.opt
call "%BIN%\xelab.bat" -f xelab.opt || exit /b 1
set GUI=-gui
if defined FC_NOGUI set GUI=
call "%BIN%\xsim.bat" snap_wave %GUI% -testplusarg VALID%V% -testplusarg READY%R% -testplusarg SEED%S% -tclbatch ../../fc_wave.tcl
