@echo off
cd /d "%~dp0"
call vivado -mode batch -source vivado\create_project.tcl
pause
