param(
  [string]$Compiler = "$env:LOCALAPPDATA/Arduino15/packages/teensy/tools/teensy-compile/15.2.1/arm/bin/arm-none-eabi-g++.exe",
  [switch]$Run
)
$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath $Compiler)) {
  throw 'Provide -Compiler pointing to a C++14 compiler. Use -Run only with a native compiler.'
}
$source = Join-Path $PSScriptRoot 'test_depth_control.cpp'
& $Compiler -std=c++14 -Wall -Wextra -Werror -fsyntax-only $source
if ($LASTEXITCODE -ne 0) { throw 'Depth-control compile-time assertions failed.' }
$count = @(Select-String -LiteralPath $source -Pattern '^static_assert').Count
Write-Output "PASS: depth-control compile-time assertions ($count assertions in source; GCC also evaluates the trigonometric reference)."
if ($Run) {
  $binary = Join-Path ([System.IO.Path]::GetTempPath()) ("saver-depth-" + [guid]::NewGuid().ToString('N') + '.exe')
  try {
    & $Compiler -std=c++14 -Wall -Wextra -Werror $source -o $binary
    if ($LASTEXITCODE -ne 0) { throw 'Native depth-control build failed.' }
    & $binary
    if ($LASTEXITCODE -ne 0) { throw 'Native depth-control runtime tests failed.' }
    Write-Output 'PASS: native depth-control runtime suite.'
  } finally {
    if (Test-Path -LiteralPath $binary) { Remove-Item -LiteralPath $binary }
  }
} else {
  Write-Output 'Compile-time software evaluation only; no native executable was run.'
}
Write-Output 'No firmware upload, serial-port access, ADC reading, motor run, or hardware validation was performed.'
