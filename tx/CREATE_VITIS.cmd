@echo off
setlocal
cd /d "%~dp0"
set "BFSK_XSCT=C:\Xilinx\Vitis\2020.2\bin\xsct.bat"
if not exist "%BFSK_XSCT%" (
 echo ERROR: Vitis 2020.2 xsct.bat was not found.
 if /i not "%~1"=="--no-pause" pause
 exit /b 1
)
echo Close Vitis using this workspace before building.
set "BFSK_VITIS_SUCCESS_FILE=%TEMP%\lightletter-tx-%RANDOM%-%RANDOM%.ok"
if exist "%BFSK_VITIS_SUCCESS_FILE%" del "%BFSK_VITIS_SUCCESS_FILE%"
call "%BFSK_XSCT%" "%~dp0vitis\create_workspace.tcl"
set "BFSK_BUILD_RESULT=%ERRORLEVEL%"
if not exist "%BFSK_VITIS_SUCCESS_FILE%" set "BFSK_BUILD_RESULT=1"
if exist "%BFSK_VITIS_SUCCESS_FILE%" del "%BFSK_VITIS_SUCCESS_FILE%"
if not "%BFSK_BUILD_RESULT%"=="0" (
 echo ERROR: Build failed. See messages above.
) else (
 echo SUCCESS: Run OPEN_VITIS.cmd to open the workspace.
)
if /i not "%~1"=="--no-pause" pause
exit /b %BFSK_BUILD_RESULT%
