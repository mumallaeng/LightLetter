@echo off
cd /d "%~dp0"
if not exist vivado\fc_synth mkdir vivado\fc_synth
call vivado -mode batch -source vivado\fc_synth.tcl -log vivado\fc_synth\vivado.log -journal vivado\fc_synth\vivado.jou -notrace
pause
