param([string]$Compiler = '')
$ErrorActionPreference = 'Stop'

# Prefer an installed native compiler, then the installed Teensy ARM compiler.
# All tests are C++14 constexpr assertions; cross-compilation can evaluate them
# without a board, native executable, upload, UART access, or motor movement.
if (-not $Compiler) {
  foreach ($name in @('g++', 'clang++')) {
    $found = Get-Command $name -CommandType Application -ErrorAction SilentlyContinue |
      Select-Object -First 1
    if ($found) {
      $Compiler = $found.Source
      break
    }
  }
}
if (-not $Compiler) {
  $teensyTools = Join-Path $env:LOCALAPPDATA 'Arduino15/packages/teensy/tools/teensy-compile'
  if (Test-Path -LiteralPath $teensyTools) {
    $versions = Get-ChildItem -LiteralPath $teensyTools -Directory | Sort-Object Name -Descending
    foreach ($version in $versions) {
      $candidate = Join-Path $version.FullName 'arm/bin/arm-none-eabi-g++.exe'
      if (Test-Path -LiteralPath $candidate) {
        $Compiler = $candidate
        break
      }
    }
  }
}
if (-not $Compiler -or -not (Test-Path -LiteralPath $Compiler -PathType Leaf)) {
  throw 'Provide -Compiler pointing to an installed GCC/Clang C++14 compiler.'
}

$source = Join-Path $PSScriptRoot 'test_vesc_link.cpp'
$header = Join-Path (Split-Path -Parent $PSScriptRoot) 'vesc_link.h'
foreach ($path in @($source, $header, $PSCommandPath)) {
  $contents = Get-Content -LiteralPath $path -Raw -Encoding UTF8
  if ($contents -match '(?m)[ \t]+\r?$' -or $contents.Contains("`t") -or
      -not $contents.EndsWith("`n")) {
    throw "Formatting check failed (trailing whitespace, tabs, or missing final newline): $path"
  }
}
$codec = Get-Content -LiteralPath $header -Raw -Encoding UTF8
if ($codec -match '#include\s*[<"]Arduino|\bSerial\d*\s*[.]|\b(analogWrite|digitalWrite|delay)\s*\(') {
  throw 'The wire codec must remain independent of Arduino I/O and scheduling.'
}
if ($codec -match '\bdutyCommand\s*\(|\bSET_DUTY\b|\bclass\s+Control\b') {
  throw 'The PID transport must not expose the old duty controller.'
}

& $Compiler -std=c++14 -Wall -Wextra -Werror -fsyntax-only $source
if ($LASTEXITCODE -ne 0) { throw 'VESC transport compile-time assertions failed.' }
$count = @(Select-String -LiteralPath $source -Pattern '^static_assert').Count
Write-Output "PASS: $count compile-time assertions; transport/source formatting guards."
Write-Output "Compiler: $Compiler"
Write-Output 'Software only: no UART, GPIO, firmware upload, VESC response, or motor behavior verified.'
