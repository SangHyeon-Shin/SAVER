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
$taskBuild = Join-Path $env:TEMP ('saver-selectable-tests-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $taskBuild | Out-Null
foreach ($fixture in @($false, $true)) {
  $name = if ($fixture) { 'fixture' } else { 'default' }
  $exe = Join-Path $taskBuild ($name + '.exe')
  $argsList = $compilerPrefix + @('-std=c++14', '-Wall', '-Wextra', '-Werror',
    ('-I' + (Join-Path $PSScriptRoot 'stubs')), (Join-Path $PSScriptRoot 'test_integration.cpp'), '-o', $exe)
  if ($fixture) { $argsList += '-DSELECTABLE_TEST_FIXTURE' }
  & $Compiler @argsList
  if ($LASTEXITCODE -ne 0) { throw "Native build failed: $name" }
  & $exe
  if ($LASTEXITCODE -ne 0) { throw "Integration tests failed: $name" }
}
Write-Output 'Tests used mock USB/UART/ADC/I2C and a synthetic-only configured fixture. No upload or hardware test.'
