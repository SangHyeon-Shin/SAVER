param([string]$Compiler = "$env:LOCALAPPDATA/Arduino15/packages/teensy/tools/teensy-compile/15.2.1/arm/bin/arm-none-eabi-g++.exe")
$ErrorActionPreference = 'Stop'
$source = Join-Path $PSScriptRoot 'test_current_control.cpp'
& $Compiler -std=c++14 -Wall -Wextra -Werror -fsyntax-only $source
if ($LASTEXITCODE -ne 0) { throw 'Current-control compile-time assertions failed.' }
$count = @(Select-String -LiteralPath $source -Pattern '^static_assert').Count
Write-Output "PASS: $count current PID/calibration/wire compile-time assertions."
Write-Output 'No native executable, firmware upload, UART or hardware motion tested.'
