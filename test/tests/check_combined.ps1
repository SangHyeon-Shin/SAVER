param(
  [string]$Compiler = "$env:LOCALAPPDATA/Arduino15/packages/teensy/tools/teensy-compile/15.2.1/arm/bin/arm-none-eabi-g++.exe"
)
$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath $Compiler)) { throw 'Set -Compiler to a C++14 compiler.' }
$count = 0
foreach ($name in @('test_shared_logic.cpp', 'test_combined.cpp')) {
  $source = Join-Path $PSScriptRoot $name
  & $Compiler -std=c++14 -Wall -Wextra -Werror -fsyntax-only $source
  if ($LASTEXITCODE -ne 0) { throw "Failed: $name" }
  $count += @(Select-String -LiteralPath $source -Pattern '^static_assert').Count
}
$zeroSource = Join-Path $PSScriptRoot 'test_display_zero.cpp'
$stubs = Join-Path $PSScriptRoot 'stubs'
& $Compiler -std=c++14 -Wall -Wextra -Werror -DARDUINO_TEENSY41 -I $stubs -fsyntax-only $zeroSource
if ($LASTEXITCODE -ne 0) { throw 'Actual sketch + display-zero mock harness compilation failed.' }
$testDir = Split-Path -Parent $PSScriptRoot
$text = Get-Content -LiteralPath (Join-Path $testDir 'test.ino') -Raw -Encoding UTF8
$base = Get-Content -LiteralPath (Join-Path $testDir 'vacuum_motor_stage4_fg/bench_logic.h') -Raw -Encoding UTF8
$combined = Get-Content -LiteralPath (Join-Path $testDir 'bench_logic.h') -Raw -Encoding UTF8
# The reused parser/RunState must remain identical to the tested Stage4 baseline.
$baseLogic = [regex]::Match($base, '(?s)// Existing bench limits.*?(?=\} // namespace saver_vacuum_bench)').Value.Trim()
$copyLogic = [regex]::Match($combined, '(?s)// Existing bench limits.*?(?=// Manual-run state)').Value.Trim()
if ($baseLogic.Replace("`r", '') -cne $copyLogic.Replace("`r", '')) { throw 'Stage4 shared logic diverged.' }
if ($text -match '\b(attachInterrupt|PIN_VAC[12]_FG|PIN_MOTOR_FG)\b|\bfg_(raw|ok)=') {
  throw 'FG must remain disabled.'
}
foreach ($required in @('analogReadResolution(12)', 'Serial.availableForWrite()', 'control_.observe(',
                        'line_.expire(millis())', 'synchronizeUsb()', '"pressure_relative_Pa" : "pressure_Pa"',
                        'if (pressure_.ok) snprintf', 'stop(3, true); // Outputs BEFORE logs',
                        'parseRunDuties(', 'run_mode=MANUAL_STOP', 'const ManualRunState& motor',
                        'displayZero_.capture(pressure_.raw)', 'DISPLAY_ZERO_CLEARED',
                        'relative_only=1,pressure_calibrated=0', 'no_fg_pa_manual_v4_relative_zero')) {
  if (-not $text.Contains($required)) { throw "Missing integration guard: $required" }
}
if ($text -match '\b(DEFAULT_RUN_MS|MIN_RUN_MS|MAX_RUN_MS|remaining_ms|duration_ms|timeoutMask|RUN_TIMEOUT_MOTOR_OFF)\b') {
  throw 'Combined sketch must not keep a hidden run timer or misleading countdown.'
}
$manualLogic = [regex]::Match($combined, '(?s)// Manual-run state.*').Value
if (-not $manualLogic -or $manualLogic -match '\b(expire|remaining|duration_|started_|timeoutMask)\b') {
  throw 'Manual-run control must not expire by elapsed time.'
}
$pinmap = Get-Content -LiteralPath (Join-Path (Split-Path -Parent $testDir) 'teensy/pinmap.h') -Raw -Encoding UTF8
foreach ($name in @('PIN_VAC1_VSP','PIN_VAC1_EN','PIN_VAC1_ERR','PIN_VAC1_RUN',
                    'PIN_VAC2_VSP','PIN_VAC2_EN','PIN_VAC2_ERR','PIN_VAC2_RUN','PIN_PRESSURE')) {
  $pattern = "constexpr\s+uint8_t\s+$name\s*=\s*(\d+|A\d+)\s*;"
  $actual = [regex]::Match($text, $pattern)
  $expectedName = if ($name -eq 'PIN_PRESSURE') { 'PIN_PRESSURE_SENSOR' } else { $name }
  $expected = [regex]::Match($pinmap, "constexpr\s+uint8_t\s+$expectedName\s*=\s*(\d+|A\d+)\s*;")
  if (-not $actual.Success -or -not $expected.Success -or
      $actual.Groups[1].Value -ne $expected.Groups[1].Value) { throw "Pin mismatch: $name" }
}
Write-Output "PASS: $count compile-time assertions; display-zero harness syntax, manual-stop mode, Stage4 reuse, Pa output, no-FG and pinmap guards."
Write-Output 'No serial connection, firmware upload, GPIO measurement or motor run was performed.'
