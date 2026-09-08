param(
  [string]$Compiler = "$env:LOCALAPPDATA/Arduino15/packages/teensy/tools/teensy-compile/15.2.1/arm/bin/arm-none-eabi-g++.exe"
)
$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath $Compiler)) { throw 'Set -Compiler to a C++14 compiler.' }
$testSource = Join-Path $PSScriptRoot 'test_bench_logic.cpp'
& $Compiler -std=c++14 -Wall -Wextra -Werror -fsyntax-only $testSource
if ($LASTEXITCODE -ne 0) { throw 'Stage 4 compile-time tests failed.' }
$count = @(Select-String -LiteralPath $testSource -Pattern '^static_assert').Count
Write-Output "PASS: $count compile-time assertions against the sketch's bench_logic.h."

# Guard the integration too: FG must not return as a floating input/ISR/log.
$sketchDirectory = Split-Path -Parent $PSScriptRoot
$repoDirectory = Split-Path -Parent (Split-Path -Parent $sketchDirectory)
$sketchText = Get-Content -LiteralPath (Join-Path $sketchDirectory 'vacuum_motor_stage4_fg.ino') -Raw -Encoding UTF8
$logicText = Get-Content -LiteralPath (Join-Path $sketchDirectory 'bench_logic.h') -Raw -Encoding UTF8
$pinmapText = Get-Content -LiteralPath (Join-Path $repoDirectory 'teensy/pinmap.h') -Raw -Encoding UTF8
$mainText = Get-Content -LiteralPath (Join-Path $repoDirectory 'teensy/saver_teensy.ino') -Raw -Encoding UTF8
if (($sketchText + $logicText) -match '\b(PIN_MOTOR_FG|FgWindow|FG_WINDOW_US|onFgPulse|fgPulse|startFgWindow|updateFg|attachInterrupt)\b') {
  throw 'FG pin access, acquisition state or interrupt registration remains in Stage 4.'
}
if ($sketchText -match '(fg_raw|fg_ok|fg_window_pulses|frequency_Hz|rpm_candidate)=') {
  throw 'Removed FG measurements must be omitted, not reported as zeros.'
}
if (($pinmapText + $mainText) -match '\bPIN_VAC[12]_FG\b') {
  throw 'The integrated pinmap/firmware still reserves a vacuum FG pin.'
}
if ($sketchText -notmatch 'SAVER_VACUUM_MOTOR_STAGE4_NO_FG_READY,build=no_fg_v1') {
  throw 'Missing firmware identifier for the no-FG build.'
}

# Resolve analog aliases to physical pins before checking duplicates/spares.
$analogPins = @{ A0 = 14; A8 = 22; A9 = 23; A16 = 40; A17 = 41 }
$pins = @{}
$used = [System.Collections.Generic.HashSet[int]]::new()
$definitions = [regex]::Matches($pinmapText, '(?m)^constexpr\s+uint8_t\s+(PIN_\w+)\s*=\s*(\d+|A\d+)\s*;')
foreach ($definition in $definitions) {
  $name = $definition.Groups[1].Value
  $value = $definition.Groups[2].Value
  if ($value.StartsWith('A')) {
    if (-not $analogPins.ContainsKey($value)) { throw "Unmapped analog alias: $value" }
    $pin = [int]$analogPins[$value]
  } else { $pin = [int]$value }
  if ($pin -lt 0 -or $pin -gt 41 -or -not $used.Add($pin)) { throw "Invalid/duplicate physical pin: $pin" }
  $pins[$name] = $pin
}
if ($used.Count -ne 37) { throw 'Expected 37 assigned outer pins and five spares.' }
foreach ($spare in @(11,12,13,32,37)) {
  if ($used.Contains($spare)) { throw "Spare pin $spare is unexpectedly assigned." }
}
foreach ($signal in @('VSP','EN','ERR','RUN')) {
  $definition = [regex]::Match($sketchText, "(?m)^constexpr\s+uint8_t\s+PIN_MOTOR_$signal\s*=\s*(\d+)\s*;")
  if (-not $definition.Success -or [int]$definition.Groups[1].Value -ne $pins["PIN_VAC1_$signal"]) {
    throw "Stage 4 $signal does not match teensy/pinmap.h."
  }
}
Write-Output 'PASS: no-FG source guards; 37 unique physical pins; 11/12/13/32/37 spare; Stage 4 pins match pinmap.'
Write-Output 'No executable, Arduino firmware, serial port, GPIO, or motor was run.'
