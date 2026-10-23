param(
  [Parameter(Mandatory=$true)][string]$LogPath,
  [string]$Compiler = "$env:LOCALAPPDATA/Arduino15/packages/teensy/tools/teensy-compile/15.2.1/arm/bin/arm-none-eabi-g++.exe"
)
$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath $Compiler)) { throw 'Set -Compiler to arm-none-eabi-g++.exe.' }
$sketchDirectory = Split-Path -Parent $PSScriptRoot
$events = [System.Collections.Generic.List[string]]::new()
$previousSeq = $null
$previousMs = $null
$discontinuities = 0
foreach ($line in (Get-Content -LiteralPath $LogPath -Encoding UTF8)) {
  if ($line -notmatch '^RAW,') { continue }
  $fields = @{}
  foreach ($field in ($line -split ',' | Select-Object -Skip 1)) {
    $pair = $field -split '=', 2
    if ($pair.Length -eq 2) { $fields[$pair[0]] = $pair[1] }
  }
  foreach ($key in @('seq','rx_ms','red_raw','ir_raw','adc_rail')) {
    if (-not $fields.ContainsKey($key) -or $fields[$key] -notmatch '^\d+$') {
      throw "Malformed RAW row: missing/invalid $key. Do not substitute DATA summaries."
    }
  }
  $seq = [uint32]::Parse($fields['seq'])
  $sampleMs = [uint32]::Parse($fields['rx_ms'])
  $red = [uint32]::Parse($fields['red_raw'])
  $ir = [uint32]::Parse($fields['ir_raw'])
  if ($red -gt 262143 -or $ir -gt 262143) { throw 'RAW value exceeds 18-bit ADC.' }
  # Conservative discontinuity reset. rx_ms is reception time, not sensor timing.
  # Backward clocks/seq (including rollover) also reset this OFFLINE replay.
  $reset = $null -ne $previousSeq -and (
    [uint64]$seq -ne ([uint64]$previousSeq + 1) -or
    $sampleMs -lt $previousMs -or ([int64]$sampleMs - [int64]$previousMs) -ge 200)
  if ($reset) { $discontinuities++ }
  if ($fields['adc_rail'] -ne '0') { $red = 0 } # Revoke window; never count flagged rails as usable.
  $events.Add("{$($red)U,$($ir)U,$($reset.ToString().ToLowerInvariant())},")
  $previousSeq = $seq
  $previousMs = $sampleMs
}
if ($events.Count -lt 200) { throw 'Need at least 200 RAW pairs; DATA is not a waveform.' }
$eventRows = $events -join "`n"
$source = @"
#include "ppg_signal.h"
using namespace saver_max_bench;
struct Event { uint32_t red,ir; bool reset; };
constexpr Event events[] = {
$eventRows
};
struct Stats { unsigned windows=0,accepted=0,rejected=0,confirmed=0,minHr=10000,maxHr=0;
               unsigned reasons[9]={}; };
constexpr Stats replay() {
  Stats s; PulseConfirmation confirmation;
  uint32_t r[WINDOW_SAMPLES]={},i[WINDOW_SAMPLES]={}; unsigned count=0;
  for(const auto& event:events) {
    if(event.reset || !event.red || !event.ir || event.red>=ADC_MAX || event.ir>=ADC_MAX) {
      count=0;confirmation.reset();
      if(!event.red || !event.ir || event.red>=ADC_MAX || event.ir>=ADC_MAX)continue;
    }
    r[count]=event.red;i[count]=event.ir;++count;
    if(count!=WINDOW_SAMPLES)continue;
    const auto q=inspectPulse(r,i,WINDOW_SAMPLES);++s.windows;++s.reasons[unsigned(q.reason)];
    if(q.accepted){++s.accepted;if(unsigned(q.hrBpm)<s.minHr)s.minHr=q.hrBpm;
                  if(unsigned(q.hrBpm)>s.maxHr)s.maxHr=q.hrBpm;}else ++s.rejected;
    if(confirmation.accept(q))++s.confirmed;
    for(unsigned k=0;k<WINDOW_SAMPLES-STEP_SAMPLES;++k){r[k]=r[k+STEP_SAMPLES];i[k]=i[k+STEP_SAMPLES];}
    count=WINDOW_SAMPLES-STEP_SAMPLES;
  }return s;
}
constexpr auto result=replay();
static_assert(result.windows>0 && result.accepted+result.rejected==result.windows,"Replay accounting");
static_assert(result.confirmed<=result.accepted,"Confirmation cannot manufacture accepted estimates");
extern const unsigned ppg_replay_summary[]={result.windows,result.accepted,result.rejected,result.confirmed,
  result.accepted?result.minHr:0,result.maxHr,result.reasons[0],result.reasons[1],result.reasons[2],
  result.reasons[3],result.reasons[4],result.reasons[5],result.reasons[6],result.reasons[7],result.reasons[8]};
"@
# Evaluate the ACTUAL header at compile time, then inspect its emitted constants.
# No executable run, Arduino, upload, port opening, or input/output fixture files.
$assembly = $source | & $Compiler -O2 -std=c++14 -Wall -Wextra -Werror `
  -fconstexpr-ops-limit=500000000 -S -x c++ -I $sketchDirectory -o - -
if ($LASTEXITCODE -ne 0) { throw 'Recorded RAW signal replay failed.' }
$values = [System.Collections.Generic.List[uint32]]::new()
$inSummary = $false
foreach ($line in $assembly) {
  if ($line -match '^ppg_replay_summary:') { $inSummary = $true; continue }
  if (-not $inSummary) { continue }
  if ($line -match '^\s*\.word\s+(\d+)') { $values.Add([uint32]$Matches[1]) }
  elseif ($line -match '^\s*\.space\s+(\d+)') {
    for ($n=0; $n -lt ([int]$Matches[1] / 4); $n++) { $values.Add(0) }
  }
  if ($values.Count -eq 15) { break }
}
if ($values.Count -ne 15) { throw 'Cannot decode compiler replay summary.' }
Write-Output "PASS: $($events.Count) recorded RAW pairs; $discontinuities discontinuity resets."
Write-Output "WINDOWS=$($values[0]),accepted=$($values[1]),rejected=$($values[2]),confirmed=$($values[3])"
if ($values[1]) { Write-Output "CANDIDATE_HR_RANGE=$($values[4])..$($values[5]) BPM (NOT verified physiology)" }
Write-Output ('REASONS[WAITING,BAD_INPUT,FLAT,NO_PERIOD,WEAK_PERIOD,CHANNEL_MISMATCH,UNSTABLE,PERIODIC]=' + ($values[6..13] -join ','))
Write-Output 'Compile-time/offline signal investigation ONLY; no SpO2 algorithm, contact calibration, hardware timing or medical accuracy validation.'
