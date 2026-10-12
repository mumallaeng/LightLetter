$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
$work = Join-Path $PSScriptRoot 'highspeed_run'
New-Item -ItemType Directory -Path $work -Force | Out-Null
$files = @(Get-ChildItem (Join-Path $repo 'tx/bfsk_tx/rtl/*.v'))
$files += Get-Item (Join-Path $PSScriptRoot 'tb_highspeed_link.v')
$files += Get-ChildItem (Join-Path $repo 'rx/fft/rtl/*.v') | Where-Object Name -ne 'fft_top.v'
foreach ($name in @('rx_bin_detector','rx_symbol_detector','rx_symbol_sync','rx_frame_decoder','rx_crc8_check','rx_top')) {
    $files += Get-Item (Join-Path $repo "rx/bfsk_rx/rtl/$name.v")
}
$filePaths = $files | ForEach-Object { $_.FullName }
Copy-Item (Join-Path $repo 'rx/fft/rtl/twiddle_128_q14.mem') $work -Force
Push-Location $work
try {
    # Pass paths directly so a repository path containing Korean characters
    # is not corrupted by an ASCII .prj file.
    & xvlog.bat --work xil_defaultlib @filePaths
    if ($LASTEXITCODE) { throw 'xvlog failed' }
    & xelab.bat xil_defaultlib.tb_highspeed_link -s highspeed_link
    if ($LASTEXITCODE) { throw 'xelab failed' }
    & xsim.bat highspeed_link -runall
    if ($LASTEXITCODE) { throw 'xsim failed' }
    $log = Get-Content 'xsim.log' -Raw
    if ($log -notmatch 'HIGHSPEED_LINK_PASS' -or $log -match 'Fatal:|FATAL:') { throw 'Simulation assertions failed' }
} finally { Pop-Location }
