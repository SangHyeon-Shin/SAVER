param(
  [string]$Compiler = "$env:LOCALAPPDATA/Arduino15/packages/teensy/tools/teensy-compile/15.2.1/arm/bin/arm-none-eabi-g++.exe",
  [switch]$AllowConfigured
)
$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath $Compiler)) { throw 'Provide -Compiler pointing to a C++14 compiler.' }
$source = Join-Path $PSScriptRoot 'test_vesc_bench.cpp'
& $Compiler -std=c++14 -Wall -Wextra -Werror -fsyntax-only $source
if ($LASTEXITCODE -ne 0) { throw 'VESC protocol/control compile-time assertions failed.' }
$count = @(Select-String -LiteralPath $source -Pattern '^static_assert').Count
$sketchDir = Split-Path -Parent $PSScriptRoot
$sketch = Get-Content -LiteralPath (Join-Path $sketchDir 'compression_motor_single_test.ino') -Raw -Encoding UTF8
$repo = Split-Path -Parent (Split-Path -Parent $sketchDir)
$pinmap = Get-Content -LiteralPath (Join-Path $repo 'teensy/pinmap.h') -Raw -Encoding UTF8
foreach ($pin in @('PIN_CP_UART_RX','PIN_CP_UART_TX')) {
  $pattern = "constexpr\s+uint8_t\s+$pin\s*=\s*(\d+)\s*;"
  $actual = [regex]::Match($sketch,$pattern)
  $expected = [regex]::Match($pinmap,$pattern)
  if (-not $actual.Success -or -not $expected.Success -or $actual.Groups[1].Value -ne $expected.Groups[1].Value) {
    throw "Pin mismatch: $pin"
  }
}
if (-not $AllowConfigured) {
  # Preserve the user's existing profile; 50/10 is NOT a recommended motor limit.
  $expectedProfile = @{
    BENCH_SETUP_CONFIRMED = 'true'
    MAX_DUTY_PERCENT = '50'
    MAX_RUN_MS = '5000'
    DEFAULT_DUTY_PERCENT = '10'
    DEFAULT_RUN_MS = '5000'
  }
  foreach ($name in $expectedProfile.Keys) {
    $value = $expectedProfile[$name]
    if ($sketch -notmatch "\b$name\s*=\s*$value\s*;") {
      throw "Expected preserved user bench profile: $name=$value (use -AllowConfigured for a reviewed alternative)."
    }
  }
}
foreach ($required in @('Serial7.availableForWrite()', 'Serial.availableForWrite()', 'control_.check(',
                        'line_.expire(now)', 'packets_.expire(now)', 'if (fresh)',
                        'standstill_verified=0', 'rotation_verified=0',
                        'build=vesc_uart_bidirectional_v2', 'parseDutyPercent(',
                        'reportedZeroStable(', 'duty_pct=%d', 'signed_pct',
                        'decoded.fault, decoded.erpm, decoded.dutyMilli, now')) {
  if (-not $sketch.Contains($required)) { throw "Missing default/guard: $required" }
}
if ($sketch -match '\b(analogWrite|digitalWrite|attachInterrupt|delay)\s*\(|Serial7\.flush\(') {
  throw 'Standalone UART bench must not drive other GPIOs or use blocking waits.'
}
Write-Output "PASS: $count compile-time assertions; pinmap and source guards; configured_override=$AllowConfigured."
Write-Output 'This is software evaluation only: no VESC, serial port, GPIO, firmware upload or motor run.'
