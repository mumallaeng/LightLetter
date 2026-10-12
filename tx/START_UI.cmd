@echo off
cd /d "%~dp0"
python ui\capture_test.py %*
pause
