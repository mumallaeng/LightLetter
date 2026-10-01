@echo off
setlocal
set "BLOCK_ROOT=%~dp0.."
set "TEST_OUT=%TEMP%\lightletter_snapshot_%RANDOM%"
mkdir "%TEST_OUT%"
pushd "%TEST_OUT%"
call xvlog --sv "%BLOCK_ROOT%\rtl\fft_snapshot_buffer.v" "%~dp0tb_fft_snapshot_gpio.sv"
if errorlevel 1 goto failed
call xelab tb_fft_snapshot_gpio -s snapshot_gpio_tb
if errorlevel 1 goto failed
call xsim snapshot_gpio_tb -runall
findstr /C:"PASS: 261 reads" xsim.log >nul
if errorlevel 1 goto failed
echo PASS. Logs: %TEST_OUT%
popd
exit /b 0
:failed
echo FAILED. Logs: %TEST_OUT%
popd
exit /b 1
