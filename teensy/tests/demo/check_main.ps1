param([string]$Compiler = '')
$ErrorActionPreference = 'Stop'
$taskZig = Join-Path $env:TEMP 'saver-v24-review-zig-0.13.0'
$compilerPrefix = @()
if (-not $Compiler) {
  $candidate = Get-Command g++ -ErrorAction SilentlyContinue
  if ($candidate) { $Compiler = $candidate.Source }
  else {
    $Compiler = Join-Path $taskZig 'zig-windows-x86_64-0.13.0/zig.exe'
    if (-not (Test-Path -LiteralPath $Compiler)) { throw 'Set -Compiler to a native g++/clang++ compiler.' }
    $compilerPrefix = @('c++')
    $env:ZIG_GLOBAL_CACHE_DIR = Join-Path $taskZig 'global-cache'
    $env:ZIG_LOCAL_CACHE_DIR = Join-Path $taskZig 'local-cache'
  }
}
$taskBuild = Join-Path $env:TEMP ('saver-main-tests-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $taskBuild | Out-Null
foreach ($fixture in @($false, $true)) {
  $name = if ($fixture) { 'fixture' } else { 'default' }
  $exe = Join-Path $taskBuild ($name + '.exe')
  $argsList = $compilerPrefix + @('-std=c++14', '-Wall', '-Wextra', '-Werror',
    ('-I' + (Join-Path $PSScriptRoot 'stubs')), (Join-Path $PSScriptRoot 'test_main.cpp'), '-o', $exe)
  if ($fixture) { $argsList += '-DSAVER_DEMO_TEST_FIXTURE' }
  & $Compiler @argsList
  if ($LASTEXITCODE -ne 0) { throw "Native build failed: $name" }
  $testOutput = @(& $exe --wire)
  if ($LASTEXITCODE -ne 0) { throw "Integration tests failed: $name" }
  $wireCount=0
  $pairs=@{}
  foreach($line in $testOutput) {
    if ($line.StartsWith('PASS ')) { Write-Output $line; continue }
    if ($line.Length -gt 511) { throw 'Output exceeds protocol line length.' }
    $message = $line | ConvertFrom-Json
    if ($message.v -ne 1) { throw 'Unexpected output version.' }
    $wireCount++
    if ($message.t -eq 'tel') { $pairs[[string]$message.tn]=$message }
    if ($message.t -eq 'mot' -and $pairs.ContainsKey([string]$message.tn)) {
      $paired=$pairs[[string]$message.tn]
      if($paired.ms -ne $message.ms -or $paired.boot_id -ne $message.boot_id) { throw 'Telemetry pair mismatch.' }
    }
  }
  Write-Output "PASS $wireCount JSON output lines / paired telemetry framing ($name)."
}
Write-Output 'Tests used mock USB/UART/ADC/I2C and a synthetic-only configured fixture. No upload or hardware test.'
