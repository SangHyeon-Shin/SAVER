param(
  [string]$Compiler = "$env:LOCALAPPDATA/Arduino15/packages/teensy/tools/teensy-compile/15.2.1/arm/bin/arm-none-eabi-g++.exe"
)
$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath $Compiler)) {
  throw 'Provide -Compiler pointing to a C++14 compiler.'
}
$source = Join-Path $PSScriptRoot 'test_calibration_capture.cpp'
& $Compiler -std=c++14 -Wall -Wextra -Werror -fsyntax-only $source
if ($LASTEXITCODE -ne 0) { throw 'Calibration-capture compile-time assertions failed.' }
$count = @(Select-String -LiteralPath $source -Pattern '^static_assert').Count
Write-Output "PASS: calibration-capture compile-time assertions ($count assertions in source)."
Write-Output 'Compile-time software evaluation only; no native executable was run.'
Write-Output 'No firmware upload, serial-port access, ADC reading, motor run, or hardware validation was performed.'
