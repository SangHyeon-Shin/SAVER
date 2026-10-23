param([string]$Compiler = "$env:LOCALAPPDATA/Arduino15/packages/teensy/tools/teensy-compile/15.2.1/arm/bin/arm-none-eabi-g++.exe")
$ErrorActionPreference = 'Stop'
$source = Join-Path $PSScriptRoot 'test_current_pulse.cpp'
$stubs = Join-Path $PSScriptRoot 'stubs'
& $Compiler -std=c++14 -Wall -Wextra -Werror -DARDUINO_TEENSY41 -I $stubs -fsyntax-only $source
if ($LASTEXITCODE -ne 0) { throw 'Actual sketch + mock harness compilation failed.' }
$root = Split-Path -Parent $PSScriptRoot
$sketch = Get-Content -Raw -Encoding UTF8 -LiteralPath (Join-Path $root 'compression_current_only_test.ino')
foreach ($required in @('PULSE_MS = 1000', 'Serial7.availableForWrite()', 'Serial.availableForWrite()',
    'UART_TX_FULL', 'USB_DISCONNECTED', 'COMMAND_TIMEOUT', 'zeroPending_', 'feedback_verified=0',
    'INT32_MAX', 'magnitude > maxMagnitude', 'current_manual_stop_v3',
    'active_ && timed_', 'OK,RUN_STARTED', 'run_mode=MANUAL_STOP',
    'BAD_COMMAND_EXPECT_PULSE_OR_RUN_SIGNED_A')) {
  if (-not $sketch.Contains($required)) { throw "Missing guard: $required" }
}
if ($sketch.Contains('GRAMMAR_MAX_MILLI_A')) { throw 'Legacy 15 A command ceiling must be removed.' }
if ($sketch -match '\b(delay|analogWrite|digitalWrite|analogRead|attachInterrupt)\s*\(|Serial7\.flush\(|\bSET_(DUTY|RPM)\b') {
  throw 'Pulse sketch must send current only, without other GPIO control or blocking waits.'
}
Write-Output 'PASS: real sketch/mock harness syntax and current-only/source guards.'
Write-Output 'Run the host test executable separately for mock runtime checks. No hardware was used.'
