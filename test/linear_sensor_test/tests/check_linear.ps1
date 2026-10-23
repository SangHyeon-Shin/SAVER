param(
  [string]$Compiler = "$env:LOCALAPPDATA/Arduino15/packages/teensy/tools/teensy-compile/15.2.1/arm/bin/arm-none-eabi-g++.exe"
)
$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath $Compiler)) { throw 'Provide -Compiler pointing to a C++14 compiler.' }
$count = 0
foreach ($filename in @('test_linear_reading.cpp', 'test_linear_storage.cpp')) {
  $source = Join-Path $PSScriptRoot $filename
  & $Compiler -std=c++14 -Wall -Wextra -Werror -fsyntax-only $source
  if ($LASTEXITCODE -ne 0) { throw "Linear sensor compile-time assertions failed: $filename" }
  $count += @(Select-String -LiteralPath $source -Pattern '^static_assert').Count
}
$sketchDir = Split-Path -Parent $PSScriptRoot
$sketch = Get-Content -LiteralPath (Join-Path $sketchDir 'linear_sensor_test.ino') -Raw -Encoding UTF8
$repo = Split-Path -Parent (Split-Path -Parent $sketchDir)
# Arduino stages sketches into a temporary directory, so cross-sketch relative includes fail.
# Keep exact local deployment copies; fail tests if writer/reader implementations diverge.
foreach ($header in @('linear_reading.h', 'linear_calibration.h', 'linear_calibration_store.h')) {
  $canonical = Get-Content -LiteralPath (Join-Path $repo "teensy/src/saver_linear/$header") -Raw -Encoding UTF8
  $local = Get-Content -LiteralPath (Join-Path $sketchDir $header) -Raw -Encoding UTF8
  if ($canonical.Replace("`r`n", "`n") -cne $local.Replace("`r`n", "`n")) {
    throw "Shared linear header differs: $header"
  }
}
$pinmap = Get-Content -LiteralPath (Join-Path $repo 'teensy/pinmap.h') -Raw -Encoding UTF8
$pattern = 'constexpr\s+uint8_t\s+PIN_LINEAR_SENSOR\s*=\s*(A\d+)\s*;'
$actual = [regex]::Match($sketch, $pattern)
$expected = [regex]::Match($pinmap, $pattern)
if (-not $actual.Success -or -not $expected.Success -or $actual.Groups[1].Value -ne $expected.Groups[1].Value) {
  throw 'Linear sensor pin mismatch.'
}
foreach ($required in @('analogReadResolution(12)', 'analogReadAveraging(16)', 'INPUT_DISABLE',
                        'Serial.availableForWrite()', 'command_.expire(millis())',
                        'if (!reading.raw_ok)', 'position_ok=0', 'sensor_presence_known=0',
                        'build=calibration_eeprom_v3', 'calibration_.position(raw, mm)',
                        'capture_.expire(now)', 'capture_.cancel()', 'within_cal_span=',
                        'saver_linear::parseMm(arg, mm)', 'accuracy_verified=0',
                        'saver_linear::storage::load(EEPROM, calibration_)',
                        'saver_linear::storage::save(EEPROM, calibration_)',
                        'saver_linear::storage::invalidate(EEPROM)',
                        'EEPROM_SAVE_VERIFY_FAILED', 'old_record_may_remain=1')) {
  if (-not $sketch.Contains($required)) { throw "Missing source guard: $required" }
}
if ($sketch -match '\b(analogWrite|digitalWrite|attachInterrupt|delay)\s*\(|Serial\d\.|Serial\.flush\(') {
  throw 'Sensor-only test must not drive a motor, UART, or wait on Serial.'
}
$main = Get-Content -LiteralPath (Join-Path $repo 'teensy/saver_teensy.ino') -Raw -Encoding UTF8
if ($main.Contains('#include "src/demo/demo_app.h"')) {
  # The manual-placement firmware moved the legacy monolithic read path into
  # these modules. Check the deployed path, not names from the old entry file.
  $app = Get-Content -LiteralPath (Join-Path $repo 'teensy/src/demo/demo_app.h') -Raw -Encoding UTF8
  $compression = Get-Content -LiteralPath (Join-Path $repo 'teensy/src/demo/compression_module.h') -Raw -Encoding UTF8
  $adapter = Get-Content -LiteralPath (Join-Path $repo 'teensy/src/demo/linear_adapter.h') -Raw -Encoding UTF8
  foreach ($required in @('analogReadResolution(12)', 'analogReadAveraging(16)', 'cp_.loadCalibration(EEPROM)')) {
    if (-not $app.Contains($required)) { throw "Main linear initialization missing: $required" }
  }
  foreach ($required in @('pinMode(A16, INPUT_DISABLE)', 'raw_ = analogRead(A16)',
                          'calibration_.position(raw_, rawPosition_)', 'positionValid_',
                          'LINEAR_INVALID_OR_STALE', 'LINEAR_ADC_RAIL_OR_INVALID')) {
    if (-not $compression.Contains($required)) { throw "Main linear integration missing: $required" }
  }
  foreach ($required in @('saver_linear::storage::load(m, stored_)', 'stored_.position(raw, mm)',
                          'ram_.position(raw, mm)')) {
    if (-not $adapter.Contains($required)) { throw "Read-only calibration adapter missing: $required" }
  }
  if (($adapter + $compression) -match 'storage::(save|invalidate)\(|EEPROM\.(write|update|put)\(') {
    throw 'Main position conversion must not overwrite the bench calibration.'
  }
} else {
foreach ($required in @('linearSensor().begin(EEPROM)', 'readLinearSensor(g_s)',
                        'analogReadAveraging(16)', 'g_s.depth_ok = false;',
                        'sensors.linear_position_ok = false;', 'INPUT_DISABLE')) {
  if (-not $main.Contains($required)) { throw "Main linear calibration integration missing: $required" }
}
if ($main -match 'storage::(save|invalidate)\(|EEPROM\.(write|update|put)\(') {
  throw 'Main firmware must not overwrite the bench calibration.'
}
$readFunction = [regex]::Match($main, '(?s)void readLinearSensor\(.*?\n\}').Value
if (-not $readFunction -or $readFunction -match 'Serial|EEPROM|analogWrite|digitalWrite') {
  throw 'Linear sampling must not log, write storage, or drive motors.'
}
}
Write-Output "PASS: $count compile-time assertions; A16 pinmap and sensor-only source guards."
Write-Output 'PASS: shared EEPROM header equality, main read-only integration and cycle-depth separation.'
Write-Output 'No upload, serial-port access, electrical measurement, or sensor operation was performed.'
