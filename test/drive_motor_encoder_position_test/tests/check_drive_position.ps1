param(
  [string]$Compiler = "$env:LOCALAPPDATA/Arduino15/packages/teensy/tools/teensy-compile/15.2.1/arm/bin/arm-none-eabi-g++.exe"
)
$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath $Compiler)) { throw 'Set -Compiler to a C++14 compiler.' }

$testSource = Join-Path $PSScriptRoot 'test_bench_logic.cpp'
& $Compiler -std=c++14 -Wall -Wextra -Werror -fsyntax-only $testSource
if ($LASTEXITCODE -ne 0) { throw 'Drive position compile-time tests failed.' }
$assertions = @(Select-String -LiteralPath $testSource -Pattern '^static_assert').Count

$sketchDirectory = Split-Path -Parent $PSScriptRoot
$repoDirectory = Split-Path -Parent (Split-Path -Parent $sketchDirectory)
$sketch = Get-Content -LiteralPath (Join-Path $sketchDirectory 'drive_motor_encoder_position_test.ino') -Raw -Encoding UTF8
$pinmap = Get-Content -LiteralPath (Join-Path $repoDirectory 'teensy/pinmap.h') -Raw -Encoding UTF8

$pairs = @{
  'PIN_RPWM' = 'PIN_DRIVE1_RPWM'
  'PIN_LPWM' = 'PIN_DRIVE1_LPWM'
  'PIN_ENABLE' = 'PIN_DRIVE1_EN'
  'PIN_ENCODER_A' = 'PIN_DRIVE1_ENCODER_A'
  'PIN_ENCODER_B' = 'PIN_DRIVE1_ENCODER_B'
}
foreach ($localName in $pairs.Keys) {
  $local = [regex]::Match($sketch, "constexpr\s+uint8_t\s+$localName\s*=\s*(\d+)\s*;")
  $mapName = $pairs[$localName]
  $expected = [regex]::Match($pinmap, "constexpr\s+uint8_t\s+$mapName\s*=\s*(\d+)\s*;")
  if (-not $local.Success -or -not $expected.Success -or $local.Groups[1].Value -ne $expected.Groups[1].Value) {
    throw "Pin mismatch: $localName versus $mapName"
  }
}

foreach ($guard in @(
  'ENCODER_ELECTRICAL_INTERFACE_VERIFIED = true',
  'BTS_CONTROL_INTERFACE_VERIFIED = true',
  'PWM_MAX_PERCENT = 50',
  'MAX_TARGET_COUNTS = 0',
  'motorOff(); // Dangerous errors stop outputs before reporting.',
  'state_ = MotionState::ReverseDwell',
  'ENCODER_NO_PROGRESS',
  'WRONG_DIRECTION_OUTBOUND',
  'WRONG_DIRECTION_RETURN',
  'NOT_SETTLED_BEFORE_REVERSE',
  'MOVE_TIMEOUT'
)) {
  if (-not $sketch.Contains($guard)) { throw "Missing safety/source guard: $guard" }
}

$offBody = [regex]::Match($sketch, '(?s)void motorOff\(\)\s*\{(.*?)\n\s*\}').Groups[1].Value
$rpwmOff = $offBody.IndexOf('analogWrite(PIN_RPWM, 0)')
$lpwmOff = $offBody.IndexOf('analogWrite(PIN_LPWM, 0)')
$enableOff = $offBody.IndexOf('digitalWriteFast(PIN_ENABLE, LOW)')
if ($rpwmOff -lt 0 -or $lpwmOff -lt 0 -or $enableOff -lt 0 -or
    $rpwmOff -gt $enableOff -or $lpwmOff -gt $enableOff) {
  throw 'motorOff must remove both PWM outputs before EN LOW.'
}

Write-Output "PASS: $assertions compile-time logic assertions; pinmap and safety source guards."
Write-Output 'No firmware upload, serial connection, GPIO voltage, encoder pulse or motor motion was tested.'
