// Actual sketch with mock ADC/GPIO/USB. No serial device or hardware is opened.
#include "../test.ino"
#include <cmath>
#include <cstdlib>

uint32_t mockNow = 0;
int mockAdc = 3794;
int mockDigital[42] = {};
int mockPwm[42] = {};
unsigned mockAdcReads = 0;
MockSerial Serial;

namespace {
unsigned checks = 0, failed = 0;
void check(bool passed, const char *label) {
  ++checks;
  if (!passed) {
    ++failed;
    printf("FAIL: %s\n", label);
  }
}
bool contains(const char *value) { return Serial.output.find(value) != std::string::npos; }
bool off() { return !mockDigital[26] && !mockDigital[37] && !mockPwm[24] && !mockPwm[33]; }
float field(const char *key) {
  const std::string name = std::string(key) + "=";
  const size_t at = Serial.output.find(name);
  return at == std::string::npos ? std::numeric_limits<float>::quiet_NaN()
                                 : std::strtof(Serial.output.c_str() + at + name.size(), nullptr);
}
void boot(VacuumPressureBench &device, int raw = 3794) {
  mockNow = 0;
  mockAdc = raw;
  mockAdcReads = 0;
  Serial = MockSerial{};
  for (int i = 0; i < 42; ++i) {
    mockDigital[i] = HIGH;
    mockPwm[i] = 0;
  }
  device.begin();
  device.tick();
}
void command(VacuumPressureBench &device, const std::string &text) {
  Serial.output.clear();
  Serial.feed(text + "\n");
  device.tick();
}
void sample(VacuumPressureBench &device, int raw) {
  Serial.output.clear();
  mockAdc = raw;
  mockNow += 101;
  device.tick();
}
} // namespace

int main() {
  {
    VacuumPressureBench device;
    boot(device);
    check(off() && contains("v4_relative_zero"), "boot is off and identifies new build");
    command(device, "READ");
    check(contains("display_zero_active=0") && !contains("pressure_relative_Pa="),
          "no automatic reference at boot");
    mockAdc = 3575;
    const unsigned reads = mockAdcReads;
    command(device, "ZERO");
    check(mockAdcReads > reads && contains("adc_raw=3575"), "ZERO reads a fresh sample");
    check(contains("OK,DISPLAY_ZEROED") && contains("pressure_calibrated=0"),
          "reference ACK explicitly says uncalibrated");
    check(field("pressure_relative_Pa") == 0 && field("pressure_Pa") < -6000,
          "relative zero does not overwrite original pressure");
    check(off() && contains("armed=0"), "ZERO cannot arm or start motors");
    mockAdc = 3400;
    command(device, "READ");
    check(field("pressure_relative_Pa") < 0, "negative change preserved");
    mockAdc = 3650;
    command(device, "READ");
    check(field("pressure_relative_Pa") > 0, "positive change preserved");
    command(device, "ZERO");
    check(field("pressure_relative_Pa") == 0, "second ZERO replaces reference");
    command(device, "PLOT");
    check(contains("pressure_relative_Pa:0.0") && !contains("pressure_Pa:"),
          "plot uses explicitly named relative trace");
    command(device, "TEXT");
    check(contains("display_zero_active=1"), "TEXT does not erase reference");
    command(device, "UNZERO");
    check(off() && contains("DISPLAY_ZERO_CLEARED") && !contains("pressure_relative_Pa="),
          "UNZERO stops, disarms, removes relative field");
    command(device, "PLOT");
    check(contains("pressure_Pa:") && !contains("pressure_relative_Pa:"),
          "cleared plot returns to original trace");
  }
  {
    VacuumPressureBench device;
    boot(device, 3575);
    command(device, "ZERO");
    command(device, "ARM");
    command(device, "RUN1 10");
    check(mockPwm[24] > 0 && mockDigital[26] == HIGH, "mock run can exercise active stop");
    command(device, "ZERO");
    check(off() && contains("armed=0") && contains("DISPLAY_ZEROED"),
          "ZERO while running stops and disarms before capture");
    command(device, "RUN1 10");
    check(off() && contains("ERROR"), "ZERO does not bypass ARM requirement");
  }
  for (int invalid : {0, 4095, 300, 3900}) {
    VacuumPressureBench device;
    boot(device, 3575);
    command(device, "ZERO");
    command(device, "PLOT");
    sample(device, invalid);
    check(contains("pressure_ok=0") && contains("DISPLAY_ZERO_CLEARED") && off() &&
              !contains("pressure_relative_Pa=") && !contains("pressure_Pa="),
          "bad ADC clears reference and never emits fake pressure");
    mockAdc = 3575;
    command(device, "READ");
    check(contains("display_zero_active=0") && !contains("pressure_relative_Pa="),
          "sensor recovery does not automatically re-zero");
    mockAdc = invalid;
    command(device, "ZERO");
    check(contains("ERROR") && !contains("OK,DISPLAY_ZEROED") && off(),
          "ZERO rejects invalid signal instead of masking fault");
  }
  {
    VacuumPressureBench device;
    boot(device, 3575);
    command(device, "ZERO");
    Serial.connected = false;
    device.tick();
    Serial.connected = true;
    Serial.output.clear();
    device.tick();
    check(off() && contains("USB_CONNECTED"), "disconnect/reconnect remains off");
    command(device, "READ");
    check(contains("display_zero_active=0") && !contains("pressure_relative_Pa="),
          "USB session cannot inherit an old reference");
    command(device, "ZERO 123");
    check(contains("UNEXPECTED_ARGUMENTS") && !contains("DISPLAY_ZEROED"),
          "ZERO requires exact argument-free command");
    command(device, "zero");
    check(contains("DISPLAY_ZEROED"), "existing case-insensitive grammar retained");
    command(device, "STOP");
    check(contains("display_zero_active=1") && off(), "ordinary STOP retains display reference");
    Serial.space = 0;
    command(device, "UNZERO");
    check(off() && Serial.output.empty(), "USB congestion cannot block stop or reference clearing");
    Serial.space = 4096;
    command(device, "STATUS");
    check(contains("display_zero_active=0"), "reference cleared even when ACK was dropped");
  }
  printf("%u checks, %u failures (mock ADC/GPIO/USB only)\n", checks, failed);
  return failed ? 1 : 0;
}
