param([string]$Compiler = "$env:LOCALAPPDATA/Arduino15/packages/teensy/tools/teensy-compile/15.2.1/arm/bin/arm-none-eabi-g++.exe")
$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath $Compiler)) { throw 'Provide installed C++14 compiler path.' }
$sketchDir = Split-Path -Parent $PSScriptRoot
$repo = Split-Path -Parent (Split-Path -Parent $sketchDir)
$source = Join-Path $PSScriptRoot 'test_bench_guard.cpp'
& $Compiler -std=c++14 -Wall -Wextra -Werror -fsyntax-only $source
if ($LASTEXITCODE -ne 0) { throw 'Bench guard compile-time assertions failed.' }
$count = @(Select-String -LiteralPath $source -Pattern '^static_assert').Count
$sketch = Get-Content -LiteralPath (Join-Path $sketchDir 'compression_linear_pid_test.ino') -Raw -Encoding UTF8
$pins = Get-Content -LiteralPath (Join-Path $repo 'teensy/pinmap.h') -Raw -Encoding UTF8
foreach ($pin in @('PIN_CP_UART_RX','PIN_CP_UART_TX','PIN_LINEAR_SENSOR','PIN_LIMIT_TOP','PIN_LIMIT_BOTTOM','PIN_ESTOP')) {
  $pattern = "constexpr\s+uint8_t\s+$pin\s*=\s*(\w+)\s*;"
  $actual = [regex]::Match($sketch, $pattern)
  $expected = [regex]::Match($pins, $pattern)
  if (-not $actual.Success -or -not $expected.Success -or $actual.Groups[1].Value -ne $expected.Groups[1].Value) {
    throw "Pin mismatch: $pin"
  }
}
$control = [regex]::Match($sketch, '(?s)const char \*controlTick\(\).*?(?=void checkSafety\()').Value
if (-not $control -or $control -match '\bSerial\d*\b|\bemit\(|\bsend\(') {
  throw 'Control tick missing or mixed with logging/UART I/O.'
}
foreach ($required in @('analogReadResolution(12)', 'Serial.availableForWrite()', 'Serial7.availableForWrite()',
    'configIssue()', 'runtimeIssue()', 'vesc::command(vesc::SET_CURRENT, milliAmps)', 'vesc::releaseCommand()',
    'CONTROL_DEADLINE_MISSED', 'FOLLOWING_ERROR', 'END_POSITION_ERROR', 'POSITION_JUMP', 'PHYSICAL_ESTOP_ACTIVE',
    'TOP_LIMIT_ACTIVE', 'BOTTOM_LIMIT_ACTIVE', 'if (preview_)', 'profileEnded_',
    'USB_BAD_OR_LONG_COMMAND', 'USB_COMMAND_TIMEOUT', 'standstill_verified=0',
    'lm_current_v3', 'capture_.sample(position_.raw, millis())', 'CAPTURE_ONLY_NO_DRIVE',
    'configCheck(checkIndex_)', 'capture_.cancel()', 'CAPTURE_CANCELLED', 'CAPTURE_STATUS',
    'quality_verified=0', 'applied=0', 'CURRENT_COMMAND_OUT_OF_RANGE', 'RETURN_ZERO_TIMEOUT',
    'calibration_.calibrate(position_.raw, knownMm)', 'calibration_.zero(position_.raw)',
    'saver_current::Pid pid_', 'cfg::CURRENT_TX_MS', 'CURRENT_GAINS_OUT_OF_RANGE')) {
  if (-not $sketch.Contains($required)) { throw "Missing guard: $required" }
}
if ($sketch -match 'rpmCommand\(|SET_RPM|\batof\(|\batoi\(') {
  throw 'Active sketch must use current PID, not RPM or permissive numeric conversion.'
}
$captureCommand = [regex]::Match($sketch, '(?s)if \(!strcmp\(cmd, "CAPTURE"\).*?(?=if \(capture_\.active\(\)\))').Value
if (-not $captureCommand -or $captureCommand -notmatch 'requestStop\(\)' -or
    $captureCommand -notmatch 'originKnown_ = false' -or
    $captureCommand -match 'gate_\.(arm|start)|rpmCommand|cfg::CALIBRATION\s*=') {
  throw 'Diagnostic capture must stop/disarm, invalidate zero, and never activate calibration or drive.'
}
if ($sketch -match '\b(analogWrite|digitalWrite|delay)\s*\(|Serial7\.flush\(') {
  throw 'PID test must not operate other GPIO motor outputs or block with waits.'
}
Write-Output "PASS: $count bench guard compile-time assertions; pinmap/control-source checks."
Write-Output 'No actual loop scheduling, hardware input, UART transport, motor motion, or loaded axis verified.'
