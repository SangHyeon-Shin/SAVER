param(
  [string]$Compiler = "$env:LOCALAPPDATA/Arduino15/packages/teensy/tools/teensy-compile/15.2.1/arm/bin/arm-none-eabi-g++.exe"
)
$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath $Compiler)) { throw 'Provide -Compiler pointing to a C++14 compiler.' }
$source = Join-Path $PSScriptRoot 'test_logger.cpp'
& $Compiler -std=c++14 -Wall -Wextra -Werror -fsyntax-only $source
if ($LASTEXITCODE -ne 0) { throw 'Logger compile-time checks failed.' }
$count = @(Select-String -LiteralPath $source -Pattern '^static_assert').Count
$root = Split-Path -Parent $PSScriptRoot
$sketch = Get-Content -LiteralPath (Join-Path $root 'reaction_force_logger.ino') -Raw -Encoding UTF8
foreach ($guard in @('Serial.availableForWrite()', 'Nau7802Reader::Result::Sample', 'utcKnown_',
                      'SENSOR_ERROR', 'USB_DISCONNECTED', 'cal_.reset()', 'command_.expire')) {
  if (-not $sketch.Contains($guard)) { throw "Missing guard: $guard" }
}
if ($sketch -match '\b(analogWrite|digitalWrite|attachInterrupt|delay)\s*\(|Serial[1-8]\.|Serial\.flush\(') {
  throw 'This recording-only sketch must not control a motor or block on Serial.'
}
$sd = Get-Content -LiteralPath (Join-Path $root 'sd_recorder.h') -Raw -Encoding UTF8
foreach ($guard in @('O_EXCL', 'preAllocate(', 'file_.isBusy()', 'SD_DRAIN_TIMEOUT', 'SD_WRITE_FAILED', 'BUFFER_OVERFLOW')) {
  if (-not $sd.Contains($guard)) { throw "Missing SD guard: $guard" }
}
Write-Output "PASS: $count compile-time assertions, driver transport mock, sensor-only and SD source guards."
Write-Output 'No native C++ execution, upload, I2C, SD-card, force, or motor hardware test performed.'
