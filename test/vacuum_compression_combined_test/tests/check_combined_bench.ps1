param(
  [string]$Compiler = "$env:LOCALAPPDATA/Arduino15/packages/teensy/tools/teensy-compile/15.2.1/arm/bin/arm-none-eabi-g++.exe"
)
$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath $Compiler)) { throw 'Provide -Compiler pointing to a C++14 compiler.' }

$source = Join-Path $PSScriptRoot 'test_combined_bench_logic.cpp'
& $Compiler -std=c++14 -Wall -Wextra -Werror -fsyntax-only $source
if ($LASTEXITCODE -ne 0) { throw 'Combined bench compile-time assertions failed.' }

$sketchDir = Split-Path -Parent $PSScriptRoot
$repo = Split-Path -Parent (Split-Path -Parent $sketchDir)
$sketch = Get-Content -LiteralPath (Join-Path $sketchDir 'vacuum_compression_combined_test.ino') -Raw -Encoding UTF8
$pinmap = Get-Content -LiteralPath (Join-Path $repo 'teensy/pinmap.h') -Raw -Encoding UTF8
$localVesc = (Get-Content -LiteralPath (Join-Path $sketchDir 'vesc_bench.h') -Raw -Encoding UTF8) -replace "`r`n", "`n"
$referenceVesc = (Get-Content -LiteralPath (Join-Path $repo 'test/compression_motor_single_test/vesc_bench.h') -Raw -Encoding UTF8) -replace "`r`n", "`n"
if ($localVesc -cne $referenceVesc) { throw 'Local VESC bench protocol copy differs from the tested reference.' }

$pinNames = @(
  'PIN_CP_UART_RX','PIN_CP_UART_TX',
  'PIN_VAC1_VSP','PIN_VAC1_RUN','PIN_VAC1_EN','PIN_VAC1_ERR',
  'PIN_VAC2_VSP','PIN_VAC2_RUN','PIN_VAC2_EN','PIN_VAC2_ERR'
)
foreach ($name in $pinNames) {
  $pattern = "constexpr\s+uint8_t\s+$name\s*=\s*(\d+)\s*;"
  $actual = [regex]::Match($sketch, $pattern)
  $expected = [regex]::Match($pinmap, $pattern)
  if (-not $actual.Success -or -not $expected.Success -or
      $actual.Groups[1].Value -ne $expected.Groups[1].Value) {
    throw "Pin mismatch: $name"
  }
}

foreach ($required in @(
    'build=vacuum2_vesc_combined_v1', 'RUN_ALL vac1_pct vac2_pct cp_signed_pct duration_ms',
    'ROUNDTRIP vac1_pct vac2_pct cp_first_signed_pct leg_ms',
    'ROUNDTRIP_DWELL_STARTED', 'ROUNDTRIP_RETURN_STARTED', 'ROUNDTRIP_COMPLETE',
    'position_return_verified=0',
    'pressure_enabled=0', 'linear_enabled=0', 'auto_start=0',
    'writeVacuumOutputsOff();', 'queueZero()', 'Serial7.availableForWrite()',
    'VACUUM_ERR_NOT_HIGH_FOR_250MS', 'standstill_verified=0')) {
  if (-not $sketch.Contains($required)) { throw "Missing combined-bench guard: $required" }
}
if ($sketch -match '\banalogRead\s*\(') {
  throw 'Pressure/linear/current ADC acquisition must remain disabled in this test.'
}
if ($sketch -match '\b(delay|Serial7\.flush)\s*\(') {
  throw 'Combined bench must not use blocking delay/flush calls.'
}

$count = @(Select-String -LiteralPath $source -Pattern '^static_assert').Count
Write-Output "PASS: $count combined bench assertions; pinmap/source guards."
Write-Output 'Software-only: no VESC, USB serial, GPIO, power stage or motor was operated.'
