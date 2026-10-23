param(
  [string]$Compiler = '',
  [string]$Library = "$env:USERPROFILE/Documents/Arduino/libraries/SparkFun_MAX3010x_Pulse_and_Proximity_Sensor_Library/src"
)
$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath (Join-Path $Library 'spo2_algorithm.cpp'))) {
  throw 'Provide -Library pointing to the installed SparkFun MAX3010x src directory.'
}
$taskZig = Join-Path $env:TEMP 'saver-v24-review-zig-0.13.0'
$compilerPrefix = @()
if (-not $Compiler) {
  $candidate = Get-Command g++ -ErrorAction SilentlyContinue
  if ($candidate) { $Compiler = $candidate.Source }
  else {
    $Compiler = Join-Path $taskZig 'zig-windows-x86_64-0.13.0/zig.exe'
    if (-not (Test-Path -LiteralPath $Compiler)) { throw 'Provide -Compiler.' }
    $compilerPrefix = @('c++')
    $env:ZIG_GLOBAL_CACHE_DIR = Join-Path $taskZig 'global-cache'
    $env:ZIG_LOCAL_CACHE_DIR = Join-Path $taskZig 'local-cache'
  }
}
$taskBuild = Join-Path $env:TEMP ('saver-ppg-replay-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $taskBuild | Out-Null
$exe = Join-Path $taskBuild 'replay.exe'
# Library include path MUST precede the mock path: exercise the real estimator.
& $Compiler @compilerPrefix -std=c++14 -Wall -Wextra ('-I' + $Library) `
  ('-I' + (Join-Path $PSScriptRoot 'stubs')) `
  (Join-Path $PSScriptRoot 'test_ppg_diag_replay.cpp') `
  (Join-Path $Library 'spo2_algorithm.cpp') -o $exe
if ($LASTEXITCODE -ne 0) { throw 'Real-library replay build failed.' }
& $exe
if ($LASTEXITCODE -ne 0) { throw 'Real-library replay failed.' }
