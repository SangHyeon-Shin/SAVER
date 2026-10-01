param(
  [string]$LogPath = '',
  [string]$Compiler = "$env:LOCALAPPDATA/Arduino15/packages/teensy/tools/teensy-compile/15.2.1/arm/bin/arm-none-eabi-g++.exe"
)
$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath $Compiler)) { throw "Set -Compiler to arm-none-eabi-g++.exe or a C++14 compiler." }
$sketchDirectory = Split-Path -Parent $PSScriptRoot
$policyTests = Join-Path $PSScriptRoot 'test_optical_window.cpp'
& $Compiler -std=c++14 -Wall -Wextra -Werror -fsyntax-only $policyTests
if ($LASTEXITCODE -ne 0) { throw 'Compile-time policy tests failed.' }
$assertionCount = @(Select-String -LiteralPath $policyTests -Pattern '^static_assert').Count
Write-Output "PASS: $assertionCount compile-time policy assertions (no hardware or executable run)."
$diagnosticTests = Join-Path $PSScriptRoot 'test_diagnostics.cpp'
& $Compiler -std=c++14 -Wall -Wextra -Werror -fsyntax-only $diagnosticTests
if ($LASTEXITCODE -ne 0) { throw 'Compile-time diagnostic tests failed.' }
$diagnosticCount = @(Select-String -LiteralPath $diagnosticTests -Pattern '^static_assert').Count
Write-Output "PASS: $diagnosticCount compile-time diagnostic assertions (no hardware or executable run)."
if (-not $LogPath) { exit 0 }

# Replay only observed raw summaries through the actual C++ contact/window policy.
# A 10 Hz summary is NOT the original 25 Hz optical waveform. This checks gating,
# not pulse detection, SpO2 accuracy or the device's physical removal latency.
$events = [System.Collections.Generic.List[string]]::new()
$rawRows = 0
foreach ($line in (Get-Content -LiteralPath $LogPath -Encoding UTF8)) {
  if ($line -match '^OK,(SENSOR_RECONNECT_REQUESTED|ACQUISITION_STOPPED)') {
    $events.Add('{0U,0U,0U,true},')
  } elseif ($line -match '^DATA,') {
    $fields = @{}
    foreach ($field in ($line -split ',' | Select-Object -Skip 1)) {
      $pair = $field -split '=', 2
      if ($pair.Length -eq 2) { $fields[$pair[0]] = $pair[1] }
    }
    if ($fields['raw_ok'] -eq '1') {
      foreach ($key in @('ms','red_raw','ir_raw')) {
        if (-not $fields.ContainsKey($key)) { throw "Missing $key in a raw_ok=1 row." }
      }
      $ms = [uint32]::Parse($fields['ms'])
      $red = [uint32]::Parse($fields['red_raw'])
      $ir = [uint32]::Parse($fields['ir_raw'])
      $events.Add("{$($ms)U,$($red)U,$($ir)U,false},")
      $rawRows++
    } elseif ($fields['sensor_ok'] -eq '0') {
      $events.Add('{0U,0U,0U,true},')
    }
  }
}
if ($rawRows -eq 0) { throw 'No raw DATA rows found.' }
$eventRows = $events -join "`n"
$cppReplay = @"
#include "optical_window.h"
using namespace saver_max_bench;
struct Event { uint32_t ms, red, ir; bool reset; };
constexpr Event events[] = {
$eventRows
};
constexpr bool replay() {
  OpticalWindow window;
  bool sawMockEstimate = false, revokedMockEstimate = false;
  unsigned lowRows = 0;
  for (const Event& event : events) {
    if (event.reset) { window.reset(); continue; }
    const bool previouslyValid = window.hrOk() && window.spo2Ok();
    if (window.push(event.red, event.ir, event.ms)) {
      // Deliberately assume an estimator always claims valid: the gate must
      // still suppress its numbers after removal. This is NOT a measured result.
      window.completeCalculation(80, true, 97, true);
      sawMockEstimate = true;
    }
    if (event.red <= CONTACT_OFF_RAW || event.ir <= CONTACT_OFF_RAW ||
        event.red >= ADC_MAX || event.ir >= ADC_MAX) {
      ++lowRows;
      if (window.contactReady() || window.hrOk() || window.spo2Ok() || window.count() != 0) return false;
      if (previouslyValid) revokedMockEstimate = true;
    }
  }
  return sawMockEstimate && revokedMockEstimate && lowRows > 0;
}
static_assert(replay(), "Recorded removal must revoke a previously valid mock estimate");
"@
# Send generated test source to compiler stdin; do not create or edit a fixture.
$cppReplay | & $Compiler -std=c++14 -Wall -Wextra -Werror -fsyntax-only -x c++ -I $sketchDirectory -
if ($LASTEXITCODE -ne 0) { throw 'Log replay assertion failed.' }
Write-Output "PASS: $rawRows recorded raw summaries replayed; removal revoked mock estimates."
