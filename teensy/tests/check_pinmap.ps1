param(
  [string]$Compiler = "$env:LOCALAPPDATA/Arduino15/packages/teensy/tools/teensy-compile/15.2.1/arm/bin/arm-none-eabi-g++.exe",
  [string]$Core = "$env:LOCALAPPDATA/Arduino15/packages/teensy/hardware/avr/1.62.0/cores/teensy4"
)
$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath $Compiler)) { throw 'Provide the installed ARM C++ compiler.' }
if (-not (Test-Path -LiteralPath (Join-Path $Core 'Arduino.h'))) { throw 'Provide the Teensy 4.x core directory.' }
$source = Join-Path $PSScriptRoot 'test_pinmap.cpp'
& $Compiler -std=gnu++17 -Wall -Wextra -Werror -fsyntax-only -mcpu=cortex-m7 -mthumb `
  -mfpu=fpv5-d16 -mfloat-abi=hard -D__IMXRT1062__ -DARDUINO_TEENSY41 -DUSB_SERIAL `
  -DF_CPU=600000000 -DLAYOUT_US_ENGLISH -isystem $Core $source
if ($LASTEXITCODE -ne 0) { throw 'Pinmap compile-time assertions failed.' }
$count = @(Select-String -LiteralPath $source -Pattern '^static_assert').Count
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$pinmap = Get-Content -LiteralPath (Join-Path $repo 'teensy/pinmap.h') -Raw -Encoding UTF8
function PinValue([string]$text, [string]$name) {
  $match = [regex]::Match($text, "(?m)^constexpr\s+uint8_t\s+$name\s*=\s*(\d+|A\d+)\s*;")
  if (-not $match.Success) { throw "Missing pin definition: $name" }
  return $match.Groups[1].Value
}
function MatchesPin([string]$text, [string]$name, [string]$canonical) {
  if ((PinValue $text $name) -cne (PinValue $pinmap $canonical)) { throw "Pin mismatch: $name" }
}
$combined = Get-Content -LiteralPath (Join-Path $repo 'test/test.ino') -Raw -Encoding UTF8
foreach ($name in @('PIN_VAC1_VSP','PIN_VAC1_EN','PIN_VAC1_ERR','PIN_VAC1_RUN',
                    'PIN_VAC2_VSP','PIN_VAC2_EN','PIN_VAC2_ERR','PIN_VAC2_RUN')) {
  MatchesPin $combined $name $name
}
if (-not $combined.Contains('emitf("PINS,M1_VSP=%u')) { throw 'Combined pin banner must use actual constants.' }
$ppg = Get-Content -LiteralPath (Join-Path $repo 'test/max30102_sensor_test/pinmap.h') -Raw -Encoding UTF8
MatchesPin $ppg 'PIN_MAX_SDA' 'PIN_MAX30102_SDA'
MatchesPin $ppg 'PIN_MAX_SCL' 'PIN_MAX30102_SCL'
foreach ($name in @('PIN_VAC1_EN','PIN_VAC2_EN','PIN_VAC1_VSP','PIN_VAC2_VSP')) {
  MatchesPin $ppg ($name + '_GUARD') $name
}
$logger = Get-Content -LiteralPath (Join-Path $repo 'test/reaction_force_logger/reaction_force_logger.ino') -Raw -Encoding UTF8
MatchesPin $logger 'SDA_PIN' 'PIN_NAU7802_SDA'
MatchesPin $logger 'SCL_PIN' 'PIN_NAU7802_SCL'
if ($logger -match '\bWire\s*\.\s*\w+\s*\(') { throw 'NAU7802 must not access the PPG Wire bus.' }
foreach ($required in @('Wire1.setSDA(SDA_PIN)', 'Wire1.setSCL(SCL_PIN)', 'Wire1.begin()',
                        'Wire1.beginTransmission(', 'Wire1.requestFrom(', 'Wire1.end()', 'BUS=Wire1')) {
  if (-not $logger.Contains($required)) { throw "Missing independent-bus path: $required" }
}
$main = Get-Content -LiteralPath (Join-Path $repo 'teensy/saver_teensy.ino') -Raw -Encoding UTF8
$main += Get-Content -LiteralPath (Join-Path $repo 'teensy/src/demo/demo_app.h') -Raw -Encoding UTF8
if ($main -match '\b(pinMode|digitalWrite|analogRead|attachInterrupt)\s*\(\s*PIN_\w+_RESERVED\b') {
  throw 'Main must not access disconnected spare ADC/Hall pins.'
}
foreach ($name in @('PIN_GYRO_UART_RX','PIN_GYRO_UART_TX')) {
  if (-not $main.Contains("pinMode($name, INPUT)")) { throw "Missing high-Z reservation: $name" }
}
if ($main -match 'Serial8\.begin\(') { throw 'Manual-placement firmware must not activate an unimplemented gyro driver.' }
$bindings = Get-Content -LiteralPath (Join-Path $repo 'teensy/src/demo/bindings.h') -Raw -Encoding UTF8
foreach ($required in @('PIN_NAU7802_SDA == 17','PIN_NAU7802_SCL == 16', 'PIN_CP_UART_RX == 28')) {
  if (-not $bindings.Contains($required)) { throw "Missing frozen driver pin guard: $required" }
}
Write-Output "PASS: $count pinmap compile-time assertions; combined/PPG/Wire1/main source guards."
Write-Output 'No serial port, firmware upload, UART/I2C transaction, GPIO or motor hardware test performed.'
