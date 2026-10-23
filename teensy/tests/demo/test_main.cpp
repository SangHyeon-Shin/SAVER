#include <Arduino.h>
#include <Wire.h>
#include <EEPROM.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#ifdef SAVER_DEMO_TEST_FIXTURE
#include "fixture_config.h"
#endif
#define private public
#include "../../saver_teensy.ino"
#undef private
uint64_t mockUs = 0;
int mockAdc[42] = {}, mockDigital[42] = {}, mockPwm[42] = {}, mockPinMode[42] = {};
bool mockVescReplies = true;
MockSerial Serial, Serial7;
TwoWire Wire, Wire1;
MockMemory EEPROM;
MockPpgAlgorithm mockPpgAlgorithm;
unsigned checks = 0;
#define CHECK(x)                                                                                   \
  do {                                                                                             \
    ++checks;                                                                                      \
    if (!(x)) {                                                                                    \
      std::fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x);                                         \
      return 1;                                                                                    \
    }                                                                                              \
  } while (0)
static void step(unsigned n = 1) {
  while (n--) {
    mockUs += 5000;
    loop();
  }
}
static void command(const char *c) {
  Serial.feed(std::string(c) + "\n");
  step(4);
}
static bool off() { return !mockDigital[26] && !mockDigital[37] && !mockPwm[24] && !mockPwm[33]; }
static void json(unsigned seq, const char *cmd, unsigned aid = 0, const char *params = "") {
  char b[512];
  snprintf(b, sizeof(b),
           "{\"v\":1,\"t\":\"cmd\",\"seq\":%u,\"cmd\":\"%s\",\"bid\":%u,\"aid\":%u%s}", seq, cmd,
           saver_demo::instance().session_.boot, aid, params);
  command(b);
}
static int stopTestTests() {
  Serial.connected = true;
  Serial.space = Serial7.space = 4096;
  Serial.input.clear();
  mockVescReplies = true;
  Serial7.uart = true;
  Serial7.inputDeciV = 240;
  Serial7.fetDeciC = 300;
  Serial7.motorCentiA = 100;
  Serial7.inputCentiA = 50;
  Serial7.erpm = Serial7.fault = 0;
  // Missing pressure/load-cell hardware must not prevent an UNCOUPLED motor
  // transport test. It must still prevent normal position/force operation.
  mockAdc[A8] = 4095;
  mockAdc[A16] = 1000;
  mockDigital[27] = mockDigital[36] = LOW;
  Wire1.fail = true;
  saver_demo::App app;
  app.begin();
  auto advance = [&](unsigned n) {
    while (n--) {
      mockUs += 5000;
      app.tick();
    }
  };
  auto cmd = [&](const char *s) {
    Serial.feed(std::string(s) + "\n");
    advance(4);
  };
  auto startPulse = [&]() {
    cmd("CP STOPTEST ARM");
    cmd("CP STOPTEST PULSE");
    advance(5);
    return app.cp_.active();
  };
  auto recover = [&]() {
    mockAdc[A16] = 1000;
    mockVescReplies = true;
    Serial7.space = 4096;
    Serial7.inputDeciV = 240;
    Serial7.fault = 0;
    Serial7.erpm = 0;
    Serial7.inputCentiA = 50;
    Serial7.motorCentiA = 100;
    Serial7.fetDeciC = 300;
    advance(300);
    cmd("CP STOPTEST CLEAR");
    return !app.cp_.fault() && !app.faulted();
  };
  advance(300);
  CHECK(!app.cp_.active() && !app.cp_.stopTestEnabled() && off());
  cmd("CP STOPTEST PULSE");
  CHECK(!app.cp_.active() && !app.cp_.armed());
  cmd("CP STOPTEST UNCOUPLED");
  CHECK(app.cp_.stopTestEnabled());
  CHECK(mockPinMode[A16] == INPUT_PULLDOWN);
  CHECK(!app.cp_.calibration_.calibrated());
  CHECK(!app.cp_.forceCal_.confirmed);
  advance(60);
  const auto first = Serial7.currents.size();
  CHECK(startPulse());
  advance(210);
  CHECK(!app.cp_.active() && !app.cp_.armed());
  CHECK(!app.cp_.fault() && !app.faulted());
  CHECK(Serial7.currents.back() == 0);
  bool nonzero = false;
  for (size_t i = first; i < Serial7.currents.size(); ++i) {
    CHECK(Serial7.currents[i] == 0 || Serial7.currents[i] == 1000);
    nonzero = nonzero || Serial7.currents[i] == 1000;
  }
  CHECK(nonzero);
  advance(300);
  CHECK(!app.cp_.active());
  cmd("CP STOPTEST PULSE"); // One-shot ARM was consumed.
  CHECK(!app.cp_.active());
  CHECK(startPulse());
  cmd("STOP");
  CHECK(!app.cp_.active() && Serial7.currents.back() == 0);
  CHECK(!strcmp(app.state_, "IDLE"));
  CHECK(startPulse());
  cmd("CP STOPTEST PULSE"); // Duplicate must stop, never extend/repeat.
  CHECK(!app.cp_.active() && Serial7.currents.back() == 0);
  CHECK(recover());
  CHECK(startPulse());
  mockAdc[A16] = 0;
  advance(1);
  CHECK(!app.cp_.active() && app.cp_.fault());
  CHECK(!strcmp(app.faultWhy_, "LINEAR_ADC_RAIL_OR_INVALID"));
  CHECK(Serial7.currents.back() == 0);
  cmd("CP STOPTEST CLEAR");
  CHECK(app.cp_.fault());
  CHECK(recover());
  CHECK(startPulse());
  mockAdc[A16] = 4095;
  advance(1);
  CHECK(!app.cp_.active() && Serial7.currents.back() == 0);
  CHECK(recover());
  CHECK(startPulse());
  mockVescReplies = false;
  advance(70);
  CHECK(!app.cp_.active());
  CHECK(!strcmp(app.faultWhy_, "VESC_VALUES_STALE_OR_FW_UNSUPPORTED"));
  CHECK(Serial7.currents.back() == 0);
  CHECK(recover());
  CHECK(startPulse());
  Serial7.space = 0;
  advance(5);
  CHECK(!app.cp_.active() && app.cp_.stopPending());
  CHECK(!strcmp(app.faultWhy_, "UART_TX_FULL"));
  cmd("CP STOPTEST CLEAR");
  CHECK(app.cp_.fault() && !app.cp_.active());
  CHECK(recover());
  CHECK(Serial7.currents.back() == 0);
  CHECK(startPulse());
  Serial7.feed(std::string(1, char(0x7F)));
  advance(1);
  CHECK(!app.cp_.active());
  CHECK(!strcmp(app.faultWhy_, "VESC_BAD_OR_EXPIRED_PACKET"));
  CHECK(Serial7.currents.back() == 0);
  CHECK(recover());
  CHECK(startPulse());
  mockUs += 40000;
  app.tick();
  CHECK(!app.cp_.active() && Serial7.currents.back() == 0);
  CHECK(!strcmp(app.faultWhy_, "CONTROL_DEADLINE_MISSED"));
  CHECK(recover());
  CHECK(startPulse());
  Serial7.inputDeciV = 20; // Models loss of motor power, not an aux E-stop input.
  advance(30);
  CHECK(!app.cp_.active() && !strcmp(app.faultWhy_, "INPUT_VOLTAGE_LIMIT"));
  CHECK(Serial7.currents.back() == 0);
  CHECK(recover());
  CHECK(startPulse());
  Serial7.fault = 2;
  advance(30);
  CHECK(!app.cp_.active() && !strcmp(app.faultWhy_, "VESC_REPORTED_FAULT"));
  CHECK(recover());
  CHECK(startPulse());
  Serial7.inputCentiA = 500;
  advance(30);
  CHECK(!app.cp_.active() && !strcmp(app.faultWhy_, "INPUT_CURRENT_LIMIT"));
  CHECK(recover());
  CHECK(startPulse());
  Serial7.motorCentiA = 2000;
  advance(30);
  CHECK(!app.cp_.active() && !strcmp(app.faultWhy_, "MOTOR_CURRENT_LIMIT"));
  CHECK(recover());
  CHECK(startPulse());
  Serial7.fetDeciC = 900;
  advance(30);
  CHECK(!app.cp_.active() && !strcmp(app.faultWhy_, "FET_TEMPERATURE_LIMIT"));
  CHECK(recover());
  CHECK(startPulse());
  Serial.space = 0; // Logging congestion cannot prolong the 1-second stimulus.
  advance(220);
  CHECK(!app.cp_.active() && Serial7.currents.back() == 0);
  Serial.space = 4096;
  advance(200);
  cmd("CP STOPTEST ARM");
  advance(2010);
  CHECK(!app.cp_.armed());
  cmd("CP STOPTEST PULSE");
  CHECK(!app.cp_.active());
  CHECK(startPulse());
  cmd("CONTROL REMOTE");
  CHECK(!app.cp_.active() && app.owner_ == saver_demo::App::Owner::Local);
  CHECK(app.cp_.stopTestEnabled());
  CHECK(Serial7.currents.back() == 0);
  cmd("CP PREPARE");
  CHECK(!app.prepared_ && !app.cp_.active());
  cmd("CP RUN 1");
  CHECK(!app.cp_.active());
  cmd("BOTH RUN 1");
  CHECK(!app.combinedPending_ && !app.cp_.active() && off());
  cmd("VAC RUNBOTH 22 22");
  CHECK(!app.cp_.active() && off());
  cmd("CP STOPTEST PULSE 3");
  CHECK(!app.cp_.active());
  cmd("CP STOPTEST a b c d e f g h i"); // Never access a ninth token.
  CHECK(!app.cp_.active());
  Serial7.erpm = 1000;
  advance(30);
  cmd("CP STOPTEST ARM");
  CHECK(!app.cp_.armed());
  Serial7.erpm = 0;
  advance(100);
  CHECK(startPulse());
  Serial.connected = false; // MCU remains powered in this mock only.
  app.tick();
  CHECK(!app.cp_.active() && !app.cp_.stopTestEnabled());
  CHECK(mockPinMode[A16] == INPUT_DISABLE);
  CHECK(Serial7.currents.back() == 0);
  Serial.feed("CP STOPTEST UNCOUPLED\nCP STOPTEST ARM\nCP STOPTEST PULSE\n");
  Serial.connected = true;
  advance(10);
  CHECK(!app.cp_.active() && !app.cp_.stopTestEnabled());
  CHECK(!Serial.available());
  cmd("CP STOPTEST UNCOUPLED");
  advance(100);
  CHECK(startPulse());
  cmd("CP STOPTEST OFF");
  CHECK(!app.cp_.active() && !app.cp_.stopTestEnabled());
  CHECK(Serial7.currents.back() == 0);
  CHECK(mockPinMode[A16] == INPUT_DISABLE);
  CHECK(app.cp_.readyIssue() != nullptr);
#ifndef SAVER_DEMO_TEST_FIXTURE
  CHECK(!saver_depth_config::BENCH_DRIVE_CONFIRMED);
  CHECK(saver_depth_config::SENSOR_WIRING_AND_FAULT_TEST_VERIFIED);
  CHECK(!saver_depth_config::VESC_SETTINGS_AND_TIMEOUT_VERIFIED);
  CHECK(saver_depth_config::SOFT_MIN_MM == -7);
  CHECK(saver_depth_config::SOFT_MAX_MM == 60);
  CHECK(saver_depth_config::REQUEST_MAX_RATE_CPM == 120);
  CHECK(saver_depth_config::START_TOLERANCE_MM == 0);
  CHECK(saver_depth_config::MAX_MEASURED_VELOCITY_MM_S == 0);
  CHECK(saver_depth_config::SAMPLE_NOISE_ALLOWANCE_MM == 0);
  CHECK(saver_depth_config::MAX_PROFILE_VELOCITY_MM_S > 314 &&
        saver_depth_config::MAX_PROFILE_VELOCITY_MM_S < 315);
  CHECK(saver_depth_config::MAX_PROFILE_ACCELERATION_MM_S2 > 3947 &&
        saver_depth_config::MAX_PROFILE_ACCELERATION_MM_S2 < 3949);
#endif
  Wire1.fail = false;
  mockDigital[27] = mockDigital[36] = HIGH;
  mockAdc[A8] = 3800;
  for (unsigned i = 0; i < 1000; ++i)
    app.log_.flush();
  return 0;
}
static int ppgDisplayTests() {
  mockPpgAlgorithm = {};
  mockPpgAlgorithm.hr = 75;
  mockPpgAlgorithm.spo2 = 98;
  mockPpgAlgorithm.hrValid = mockPpgAlgorithm.spo2Valid = 1;
  selectable::PpgDiagnostic diagnostic;
  diagnostic.enable(true);
  int32_t value = 0;
  for (unsigned i = 0; i < 99; ++i)
    diagnostic.push(12000, 18000, i * 40);
  CHECK(!mockPpgAlgorithm.calls);
  CHECK(!diagnostic.heartRate(3920, value));
  diagnostic.push(12000, 18000, 3960);
  CHECK(mockPpgAlgorithm.calls == 1);
  CHECK(diagnostic.heartRate(3960, value) && value == 75);
  CHECK(diagnostic.saturation(3960, value) && value == 98);
  CHECK(diagnostic.samples() == 75);
  diagnostic.push(0, 18000, 4000);
  CHECK(!diagnostic.heartRate(4000, value));
  CHECK(diagnostic.samples() == 0);
  for (unsigned i = 0; i < 100; ++i)
    diagnostic.push(12000, 18000, 4040 + i * 40);
  CHECK(diagnostic.heartRate(8000, value));
  CHECK(!diagnostic.heartRate(8200, value));
  diagnostic.expire(8200);
  CHECK(diagnostic.samples() == 0);
  mockPpgAlgorithm.hr = -999;
  mockPpgAlgorithm.spo2 = 101;
  for (unsigned i = 0; i < 100; ++i)
    diagnostic.push(12000, 18000, 8240 + i * 40);
  CHECK(!diagnostic.heartRate(12200, value));
  CHECK(!diagnostic.saturation(12200, value));
  mockPpgAlgorithm.hr = 75;
  mockPpgAlgorithm.spo2 = 98;
  mockPpgAlgorithm.hrValid = mockPpgAlgorithm.spo2Valid = 0;
  for (unsigned i = 0; i < 25; ++i)
    diagnostic.push(12000, 18000, 12240 + i * 40);
  CHECK(!diagnostic.heartRate(13200, value));
  CHECK(!diagnostic.saturation(13200, value));
  diagnostic.enable(false);
  CHECK(!diagnostic.enabled() && !diagnostic.samples());
  mockPpgAlgorithm = {};
  mockPpgAlgorithm.hr = 75;
  mockPpgAlgorithm.spo2 = 98;
  mockPpgAlgorithm.hrValid = mockPpgAlgorithm.spo2Valid = 1;
  mockAdc[A8] = 3800;
  mockAdc[A16] = 1000;
  mockDigital[27] = mockDigital[36] = HIGH;
  Wire.regs[255] = 0x15;
  Wire.ppgSamples = true;
  saver_demo::App app;
  app.begin();
  auto advance = [&](unsigned count) {
    while (count--) {
      mockUs += 5000;
      app.tick();
    }
  };
  auto cmd = [&](const char *s) {
    Serial.feed(std::string(s) + "\n");
    advance(4);
  };
  auto flush = [&]() {
    for (unsigned i = 0; i < 1000; ++i)
      app.log_.flush();
  };
  cmd("STREAM OFF");
  cmd("PPG ON");
  CHECK(app.ppgOn_ && app.ppgShow_);
  CHECK(!app.ppg_.diagnostic_.enabled());
  advance(850);
  CHECK(!mockPpgAlgorithm.calls); // Low raw light still cannot pass NORMAL contact gating.
  CHECK(!app.ppg_.window_.count());
  CHECK(off() && !app.cp_.active());
  cmd("PPG DIAG ON");
  CHECK(app.ppg_.diagnostic_.enabled());
  advance(850);
  CHECK(mockPpgAlgorithm.calls);
  CHECK(app.ppg_.diagnostic_.heartRate(millis(), value) && value == 75);
  float physical = 0;
  CHECK(!app.ppg_.heartRate(physical) && !app.ppg_.saturation(physical));
  flush();
  CHECK(Serial.output.find("candidate_hr_bpm=75") != std::string::npos);
  CHECK(Serial.output.find("candidate_spo2_pct=98") != std::string::npos);
  CHECK(Serial.output.find("clinical_validated=0") != std::string::npos);
  const size_t at = Serial.output.size();
  app.telemetry();
  flush();
  const auto telemetry = Serial.output.substr(at);
  CHECK(telemetry.find("ppg_hr_bpm") == std::string::npos);
  CHECK(telemetry.find("spo2_pct") == std::string::npos);
  CHECK(telemetry.find("candidate_") == std::string::npos);
  cmd("PPG SHOW OFF");
  CHECK(!app.ppgShow_ && app.ppgOn_);
  const unsigned before = mockPpgAlgorithm.calls;
  advance(250);
  CHECK(mockPpgAlgorithm.calls > before); // Output off does not stop acquisition/calculation.
  cmd("PPG SHOW ON");
  CHECK(app.ppgShow_);
  cmd("STREAM OFF");
  CHECK(!app.ppgShow_ && app.ppgOn_);
  cmd("PPG SHOW ON");
  cmd("CONTROL REMOTE");
  CHECK(!app.ppgShow_ && !app.ppg_.diagnostic_.enabled());
  advance(420);
  cmd("CONTROL LOCAL");
  cmd("PPG DIAG ON");
  advance(850);
  CHECK(app.ppg_.diagnostic_.heartRate(millis(), value));
  Serial.connected = false;
  advance(1);
  CHECK(!app.ppgShow_ && !app.ppg_.diagnostic_.enabled());
  Serial.connected = true;
  advance(3);
  CHECK(!app.ppgShow_);
  cmd("PPG DIAG ON");
  advance(850);
  CHECK(app.ppg_.diagnostic_.heartRate(millis(), value));
  Wire.ppgSamples = false;
  advance(45);
  CHECK(!app.ppg_.diagnostic_.heartRate(millis(), value));
  flush();
  const size_t invalidAt = Serial.output.size();
  app.ppg_.reportLive();
  flush();
  CHECK(Serial.output.substr(invalidAt).find("candidate_hr_bpm") == std::string::npos);
  CHECK(Serial.output.substr(invalidAt).find("candidate_spo2_pct") == std::string::npos);
  advance(170);
  CHECK(!app.ppg_.acquiring());
  cmd("PPG OFF");
  CHECK(!app.ppgOn_ && !app.ppgShow_);
  CHECK(!app.ppg_.diagnostic_.enabled());
  Wire.regs[255] = 0;
  cmd("PPG DIAG ON");
  CHECK(!app.ppg_.acquiring());
  CHECK(!app.ppg_.diagnostic_.heartRate(millis(), value));
  CHECK(!strcmp(app.ppg_.reason_, "PPG_NOT_FOUND"));
  CHECK(off() && !app.cp_.active());
  cmd("PPG OFF");
  Wire.regs[255] = 0x15;
  mockPpgAlgorithm = {};
  flush();
  return 0;
}
static int sensorlessTests() {
  Serial.connected = true;
  mockVescReplies = true;
  mockAdc[A8] = 0; // Unplugged OUT with the board's lower divider resistor still fitted.
  mockAdc[A16] = 1000;
  mockDigital[27] = mockDigital[36] = HIGH;
  saver_demo::App app;
  app.begin();
  auto advance = [&](unsigned n) {
    while (n--) {
      mockUs += 5000;
      app.tick();
    }
  };
  auto cmd = [&](const char *s) {
    Serial.feed(std::string(s) + "\n");
    advance(4);
  };
  auto flush = [&]() {
    for (unsigned i = 0; i < 300; ++i)
      app.log_.flush();
  };
  advance(60);
  cmd("STREAM OFF");
  CHECK(off() && !app.vac_.sensorless());
  cmd("VAC ARM");
  CHECK(!app.vac_.state_.armed() && off());
  cmd("VAC SENSORLESS ON");
  CHECK(app.vac_.sensorless() && off());
  cmd("VAC ARM");
  CHECK(!app.vac_.state_.armed()); // No timer configuration is still a rejection.
  cmd("VAC CONFIG 22 300000 2000 1.5 3000");
  CHECK(app.vac_.maxOnMs_ == 300000);
  advance(60);
  cmd("VAC CLEAR_ERR");
  CHECK(!app.vac_.fault());
  cmd("VAC ARM");
  CHECK(app.vac_.state_.armed());
  cmd("VAC RUNBOTH 22 22");
  CHECK(app.vac_.active() && !app.cp_.active());
  CHECK(mockPwm[24] == saver_vacuum_bench::dutyToPwm(22));
  CHECK(mockPwm[33] == saver_vacuum_bench::dutyToPwm(22));
  const auto boot = app.session_.boot;
  mockAdc[A8] = 4095;
  advance(1700); // Past the reported ~8-second reboot interval, simulated time only.
  CHECK(app.session_.boot == boot && app.vac_.active());
  float value = 123;
  CHECK(!app.vac_.pressure(value) && value == 123);
  CHECK(app.vac_.pressure_.raw == -1 && !app.vac_.pressure_.ok);
  CHECK(!app.vac_.holding() && !app.vac_.released());
  CHECK(!strcmp(app.vac_.startAuto(-22.5f), "SENSORLESS_MANUAL_MOTORS_ONLY"));
  CHECK(!strcmp(app.start(1), "SENSORLESS_MANUAL_MOTORS_ONLY"));
  CHECK(!strcmp(app.combinedIssue(1), "SENSORLESS_MANUAL_MOTORS_ONLY"));
  flush();
  const auto statusAt = Serial.output.size();
  cmd("STATUS");
  flush();
  CHECK(Serial.output.substr(statusAt).find("vac_kPa") == std::string::npos);
  CHECK(Serial.output.substr(statusAt).find("sensorless=1") != std::string::npos);
  cmd("STOP");
  CHECK(off() && !app.vac_.state_.armed());
  cmd("VAC RUNBOTH 22 22");
  CHECK(off()); // STOP does not leave a reusable arm latch.
  cmd("VAC ARM");
  cmd("VAC RUN1 22");
  CHECK(mockDigital[26] && !mockDigital[37]);
  cmd("DISARM");
  CHECK(off());
  cmd("VAC ARM");
  cmd("VAC RUN2 22");
  CHECK(!mockDigital[26] && mockDigital[37]);
  cmd("VAC OFF");
  CHECK(off() && !app.vac_.released());
  CHECK(!strcmp(app.state_, "IDLE") && !app.localRelease_);
  cmd("CP PREPARE");
  cmd("CP ARM");
  cmd("CP RUN 1");
  cmd("BOTH RUN 1");
  CHECK(!app.cp_.active() && !app.prepared_ && !app.combinedPending_ && off());
  cmd("VAC ON");
  CHECK(!app.vac_.requested() && off());
  cmd("VAC ARM");
  cmd("VAC RUNBOTH 22 22");
  Serial.connected = false;
  advance(2);
  CHECK(off() && app.vac_.sensorless() && !app.vac_.state_.armed());
  Serial.connected = true;
  advance(4);
  CHECK(off());
  cmd("VAC RUNBOTH 22 22");
  CHECK(off());
  cmd("VAC ARM");
  cmd("VAC RUNBOTH 22 22");
  CHECK(!off());
  cmd("CONTROL REMOTE");
  CHECK(off() && app.vac_.sensorless());
  CHECK(app.owner_ != saver_demo::App::Owner::Remote);
  cmd("VAC ARM");
  cmd("VAC RUNBOTH 22 22");
  cmd("BAD_COMMAND");
  CHECK(off());
  cmd("VAC ARM");
  cmd("VAC RUNBOTH 22 22");
  Serial.feed("PARTIAL");
  advance(220);
  CHECK(off());
  Serial.feed("\n");
  advance(4);
  cmd("VAC ARM");
  cmd("VAC RUNBOTH 22 22");
  cmd("VAC SENSORLESS OFF");
  CHECK(off() && app.vac_.sensorless()); // Mode-change attempt stops, but needs a fresh OFF.
  mockDigital[27] = LOW;
  advance(2);
  cmd("VAC CLEAR_ERR");
  CHECK(app.vac_.fault());
  mockDigital[27] = HIGH;
  advance(60);
  cmd("VAC CLEAR_ERR");
  cmd("CLEAR_FAULT");
  CHECK(!app.vac_.fault() && !app.faulted());
  cmd("VAC ARM");
  cmd("VAC RUNBOTH 22 22");
  mockDigital[36] = LOW;
  advance(2);
  CHECK(off() && app.vac_.fault() && app.faulted());
  mockDigital[36] = HIGH;
  advance(60);
  cmd("VAC CLEAR_ERR");
  cmd("CLEAR_FAULT");
  CHECK(!app.vac_.fault() && off());
  cmd("VAC CONFIG 22 200 50 1.5 50");
  cmd("VAC ARM");
  cmd("VAC RUNBOTH 22 22");
  advance(45);
  CHECK(off() && app.vac_.fault());
  CHECK(!strcmp(app.vac_.faultReason(), "BLOWER_ON_TIME_LIMIT"));
  cmd("VAC CLEAR_ERR");
  cmd("CLEAR_FAULT");
  mockAdc[A8] = 0;
  cmd("VAC SENSORLESS OFF");
  CHECK(!app.vac_.sensorless() && off());
  cmd("VAC ARM");
  CHECK(off() && !app.vac_.state_.armed());
  mockAdc[A8] = 3800;
  advance(20);
  CHECK(app.vac_.pressure(value));
  CHECK(app.vac_.released());
  cmd("VAC SENSORLESS MAYBE");
  CHECK(!app.vac_.sensorless() && off());
  flush();
  saver_demo::ResetDiagnostic diagnostic;
  diagnostic.capture();
  CHECK(!diagnostic.registerAvailable_ && !diagnostic.crashAvailable_);
  diagnostic.append('"');
  diagnostic.append('\\');
  diagnostic.append('\n');
  for (unsigned i = 0; i < 2200; ++i)
    diagnostic.append('X');
  CHECK(diagnostic.truncated_ && diagnostic.size_ == sizeof(diagnostic.text_));
  const auto size = diagnostic.size_;
  const auto diagAt = Serial.output.size();
  diagnostic.report(app.log_);
  flush();
  CHECK(diagnostic.size_ == size); // BOOT INFO can replay without consuming the capture.
  CHECK(Serial.output.substr(diagAt).find("CRASH_REPORT") != std::string::npos);
  CHECK(Serial.output.substr(diagAt).find("truncated=1") != std::string::npos);
  const auto queryAt = Serial.output.size();
  cmd("BOOT INFO");
  flush();
  CHECK(Serial.output.substr(queryAt).find("BOOT_DIAG") != std::string::npos);
  return 0;
}
static int combinedTests() {
  saver_demo::CombinedPower power;
  CHECK(power.issue(3) != nullptr);
  CHECK(!power.configure(0, 3, 0));
  CHECK(!power.configure(3.01f, 3, 0));
  CHECK(!power.configure(3, 3, 1)); // No headroom at the full 10 A supply limit.
  CHECK(!power.configure(3, 3, NAN));
  CHECK(power.configure(3, 3, 0));
  CHECK(!power.issue(3));
  CHECK(power.issue(3.01f) != nullptr);
  CHECK(power.otherLoadA() == 6);
  power.invalidate();
  CHECK(!power.known());
  Serial.connected = true;
  mockVescReplies = true;
  Serial7.inputCentiA = 50;
  mockDigital[27] = mockDigital[36] = HIGH;
  mockAdc[A8] = 3800;
  mockAdc[A16] = 1000;
  Wire1.nauRaw = 100000;
  saver_demo::App app;
  app.begin();
  auto advance = [&](unsigned count) {
    while (count--) {
      mockUs += 5000;
      app.tick();
    }
  };
  auto cmd = [&](const char *s) {
    Serial.feed(std::string(s) + "\n");
    advance(4);
  };
  auto rawPressure = [](float kPa) {
    return int((4.5f + kPa / 25.0f) / 1.47f / 3.3f * 4095.0f + .5f);
  };
  cmd("STREAM OFF");
  advance(240);
  cmd("BOTH RUN 1");
  CHECK(!app.combinedPending_ && !app.cp_.active() && off());
  cmd("VAC CONFIG 22 1000 100 1 100");
  advance(30);
  CHECK(app.vac_.released());
  cmd("BOTH POWER 3 3 0");
  CHECK(app.combinedPower_.known());
  cmd("BOTH STATUS");
  cmd("BOTH CHECK");
  CHECK(!app.cp_.active() && off());
  mockAdc[A8] = 2968; // Reported bad atmospheric offset must NOT become suction success.
  advance(4);
  cmd("BOTH RUN 1");
  CHECK(!app.combinedPending_ && !app.cp_.active() && off());
  CHECK(!strcmp(app.combinedIssue(1), "BOTH_START_FROM_VERIFIED_RELEASED_PRESSURE"));
  mockAdc[A8] = 3800;
  advance(30);
  cmd("CP ZERO");
  mockAdc[A16] = 2000;
  cmd("CP CAL 20");
  mockAdc[A16] = 1000;
  advance(60);
  cmd("CP TARE");
  advance(330);
  Wire1.nauRaw = 200000;
  cmd("CP FORCE_CAL 1");
  advance(330);
  Wire1.nauRaw = 100000;
  advance(60);
  cmd("CP TARGET 5 10");
  cmd("CP FORCELIMIT 600");
  cmd("CP INLIMIT 3");
  cmd("CP PREPARE");
#ifdef SAVER_DEMO_TEST_FIXTURE
  CHECK(app.prepared_);
  CHECK(!app.combinedIssue(1));
  cmd("BOTH RUN 999999");
  CHECK(!app.combinedPending_ && !app.cp_.active() && off());
  CHECK(app.prepared_);
  cmd("BOTH RUN 3");
  CHECK(app.combinedPending_ && !app.cp_.active());
  CHECK(mockPwm[24] == saver_vacuum_bench::dutyToPwm(22));
  CHECK(mockPwm[33] == saver_vacuum_bench::dutyToPwm(22));
  cmd("CONTROL REMOTE");
  CHECK(app.combinedPending_ && app.owner_ == saver_demo::App::Owner::Local);
  mockAdc[A8] = rawPressure(-23);
  advance(5);
  CHECK(!app.cp_.active()); // Stable suction, not merely one low sample.
  advance(22);
  CHECK(app.cp_.active() && app.combinedRunning_ && !app.combinedPending_);
  CHECK(app.cp_.cycles_ == 3);
  CHECK(app.cp_.inputCurrentLimit() == 3);
  CHECK(app.cp_.commandCurrentLimit() == 6); // Input current != motor phase current.
  cmd("{\"v\":1,\"t\":\"cmd\",\"seq\":64999,\"cmd\":\"DRIVE_STOP\"}");
  CHECK(app.cp_.active() && app.combinedRunning_); // Unrelated drive stop cannot erase the timer.
  const size_t currentCount = Serial7.currents.size();
  advance(30);
  CHECK(Serial7.currents.size() > currentCount);
  for (size_t i = currentCount; i < Serial7.currents.size(); ++i)
    CHECK(abs(Serial7.currents[i]) <= 6000);
  Serial7.inputCentiA = 301;
  advance(25);
  CHECK(!app.cp_.active() && !app.combinedRunning_);
  CHECK(!strcmp(app.cp_.faultReason(), "INPUT_CURRENT_LIMIT"));
  CHECK(app.vac_.requested()); // Do not automatically drop attachment after a CP fault.
  CHECK(Serial7.currents.back() == 0);
  auto prepare = [&]() {
    Serial7.inputCentiA = 50;
    mockAdc[A8] = 3800;
    cmd("STOP");
    cmd("VAC OFF");
    advance(80);
    cmd("CLEAR_FAULT");
    cmd("BOTH POWER 3 3 0");
    cmd("CP INLIMIT 3");
    advance(60);
    cmd("CP PREPARE");
  };
  prepare();
  CHECK(app.prepared_);
  cmd("BOTH RUN 1");
  cmd("BOTH STOP");
  CHECK(!app.combinedPending_ && !app.cp_.active());
  CHECK(app.vac_.requested());
  mockAdc[A8] = rawPressure(-23);
  advance(30);
  CHECK(!app.cp_.active()); // Cancelled wait never starts later.
  prepare();
  cmd("BOTH RUN 1");
  Serial.connected = false;
  Serial.feed("BOTH RUN 1\n");
  advance(2);
  CHECK(!app.combinedPending_ && !app.cp_.active());
  CHECK(!app.combinedPower_.known());
  Serial.connected = true;
  advance(3);
  CHECK(!Serial.available());
  mockAdc[A8] = rawPressure(-23);
  advance(30);
  CHECK(!app.cp_.active());
  prepare();
  cmd("BOTH RUN 1");
  cmd("TYPO");
  CHECK(!app.combinedPending_ && !app.cp_.active());
  CHECK(!strcmp(app.state_, "FAULT"));
  prepare();
  cmd("BOTH RUN 1");
  mockAdc[A8] = rawPressure(-23);
  advance(5); // Reset the continuous blower timer without satisfying stable hold.
  mockAdc[A8] = rawPressure(-19);
  advance(198);
  CHECK(!app.combinedPending_ && !app.cp_.active() && off());
  CHECK(!strcmp(app.faultWhy_, "BOTH_VACUUM_ACQUIRE_TIMEOUT"));
  prepare();
  cmd("BOTH RUN 1");
  mockAdc[A8] = 4095;
  advance(4);
  CHECK(!app.combinedPending_ && !app.cp_.active() && off());
  CHECK(!strcmp(app.vac_.faultReason(), "PRESSURE_UNAVAILABLE"));
  prepare();
  cmd("BOTH RUN 1");
  mockVescReplies = false;
  advance(70);
  CHECK(!app.combinedPending_ && !app.cp_.active());
  CHECK(!strcmp(app.faultWhy_, "VESC_VALUES_STALE_OR_FW_UNSUPPORTED"));
  mockVescReplies = true;
  prepare();
  cmd("BOTH RUN 1");
  Wire1.fail = true;
  advance(4);
  CHECK(!app.combinedPending_ && !app.cp_.active());
  Wire1.fail = false;
  // A failed force acquisition invalidates its calibration, rather than reusing
  // a stale valid force. Restore it explicitly for the remaining profile tests.
  cmd("STOP");
  cmd("VAC OFF");
  advance(80);
  cmd("CP SENSOR_RETRY");
  advance(240);
  cmd("CP TARE");
  advance(330);
  Wire1.nauRaw = 200000;
  cmd("CP FORCE_CAL 1");
  advance(330);
  Wire1.nauRaw = 100000;
  advance(60);
  prepare();
  CHECK(app.prepared_);
  cmd("BOTH RUN 1");
  mockAdc[A8] = rawPressure(-23);
  advance(30);
  CHECK(app.cp_.active());
  advance(1250); // One finite cycle; not a continuous repeat mode.
  CHECK(!app.cp_.active() && !app.combinedRunning_);
  CHECK(!strcmp(app.state_, "VACUUM_HOLD"));
  CHECK(app.vac_.requested());
  prepare();
  cmd("BOTH RUN 1");
  mockAdc[A8] = rawPressure(-23);
  advance(30);
  CHECK(app.cp_.active());
  app.combinedAt_ = millis() - selectable::cfg::MAX_RUN_MS;
  advance(1);
  CHECK(!app.cp_.active() && !app.combinedRunning_);
  CHECK(!strcmp(app.faultWhy_, "BOTH_TOTAL_TIME_LIMIT"));
  CHECK(app.vac_.requested());
  prepare();
  cmd("BOTH RUN 1");
  cmd("{\"v\":1,\"t\":\"cmd\",\"seq\":65000,\"cmd\":\"STOP_CPR\"}");
  CHECK(!app.combinedPending_ && !app.cp_.active());
  mockAdc[A8] = rawPressure(-23);
  advance(30);
  CHECK(!app.cp_.active());
  prepare();
  cmd("BOTH RUN 1");
  mockDigital[27] = LOW;
  advance(2);
  CHECK(!app.combinedPending_ && !app.cp_.active() && off());
  mockDigital[27] = HIGH;
  prepare();
  cmd("CP INLIMIT 4");
  cmd("CP PREPARE");
  CHECK(app.combinedIssue(1) && !strcmp(app.combinedIssue(1), "BOTH_SET_CP_INLIMIT_LE_3A"));
#else
  CHECK(!app.prepared_);
  cmd("BOTH RUN 1");
  CHECK(!app.combinedPending_ && !app.cp_.active() && off());
  (void)rawPressure;
#endif
  cmd("STOP");
  cmd("VAC OFF");
  cmd("BOTH POWER 3 3 1");
  CHECK(!app.combinedPower_.known());
  cmd("BOTH POWER 3 3 0");
  CHECK(app.combinedPower_.known());
  cmd("VAC CONFIG 21 1000 100 1 100");
  CHECK(!app.combinedPower_.known()); // A new duty cannot reuse old power measurements.
  cmd("BOTH POWER 3 3 0");
  cmd("BOTH POWER invalid 3 0");
  CHECK(!app.combinedPower_.known());
  CHECK(!app.cp_.active() && off());
  for (unsigned i = 0; i < 1000; ++i)
    app.log_.flush();
  return 0;
}
int main(int argc, char **argv) {
  using saver_demo::Object;
  Object j;
  uint32_t u = 0;
  CHECK(j.parse("{\"v\":1,\"t\":\"cmd\",\"seq\":65535,\"cmd\":\"PING\"}"));
  CHECK(j.u32("seq", u) && u == 65535);
  CHECK(!j.parse("{\"v\":1,\"v\":2}"));
  CHECK(!j.parse("{\"cmd\":\"PAUSE_CPR\",}"));
  CHECK(!j.parse("{\"x\":trueX}"));
  CHECK(!j.parse("{\"x\":01}"));
  CHECK(!j.parse("{\"x\":1.}"));
  CHECK(!j.parse("{\"x\":1e999}"));
  CHECK(!j.parse("{\"x\":{}}"));
  CHECK(!j.parse("{\"x\":1}junk"));
  CHECK(j.parse("{\"x\":4294967295}"));
  CHECK(j.u32("x", u) && u == UINT32_MAX);
  CHECK(j.parse("{\"x\":4294967296}"));
  CHECK(!j.u32("x", u));
  CHECK(j.parse("{\"seq\":1.5}"));
  CHECK(!j.u32("seq", u));
  CHECK(j.parse("{\"seq\":1,\"depth_mm\":5.001}"));
  char a[768], b[768];
  CHECK(j.canonical(a, sizeof(a)));
  CHECK(j.parse("{\"depth_mm\":5.004,\"seq\":1}"));
  CHECK(j.canonical(b, sizeof(b)));
  CHECK(!strcmp(a, b));
  MockMemory m;
  CHECK(saver_demo::nextBoot(m) == 1);
  CHECK(saver_demo::nextBoot(m) == 2);
  CHECK(saver_demo::nextBoot(m) == 3);
  CHECK(m.read(4096) == 0xFF);
  MockMemory corrupt;
  corrupt.bytes[4160] = 0;
  CHECK(saver_demo::nextBoot(corrupt) == 0);
  MockMemory noWrite;
  noWrite.failWrites = true;
  CHECK(saver_demo::nextBoot(noWrite) == 0);
  m.failWrites = true;
  CHECK(saver_demo::nextBoot(m) == 0);
  m.failWrites = false;
  CHECK(saver_demo::nextBoot(m) == 4);
  saver_demo::Session session;
  session.remember(true, true, 1, 1, "action", "accepted");
  session.remember(false, true, 2, 2, "setting", "setting-ack");
  session.remember(true, false, 3, 3, "rejected", "negative-ack");
  CHECK(session.lookup(1) && !strcmp(session.lookup(1)->ack, "accepted"));
  CHECK(session.lookup(2) && !strcmp(session.lookup(2)->ack, "setting-ack"));
  CHECK(session.lookup(3) && !strcmp(session.lookup(3)->ack, "negative-ack"));
  CHECK(session.maxAid == 3 && session.lastSeq == 3);
  CHECK(!session.lookup(0) && !session.lookup(4));
  Serial7.uart = true;
  Wire.regs[255] = 0x15;
  mockAdc[A8] = 3800;
  mockAdc[A16] = 1000;
  mockDigital[27] = mockDigital[36] = HIGH;
  setup();
  step(240);
  auto &app = saver_demo::instance();
  CHECK(off());
  CHECK(!app.cp_.active());
  CHECK(app.session_.boot == 1);
  CHECK(!app.ppgOn_);
  CHECK(!Wire.begins);
  CHECK(Wire1.begins == 1);
  CHECK(!strcmp(app.state_, "IDLE"));
  const auto beforePlanCurrents = Serial7.currents.size();
  command("CP PLAN 50 120");
  for (unsigned i = 0; i < 100; ++i)
    app.log_.flush();
  CHECK(Serial.output.find("peak_velocity_mm_s=314.159") != std::string::npos);
  CHECK(Serial.output.find("peak_acceleration_mm_s2=3947.842") != std::string::npos);
  CHECK(Serial.output.find("peak_motor_rpm=2827.43") != std::string::npos);
  CHECK(!app.cp_.active() && !app.cp_.armed());
  CHECK(app.cp_.depth_ == 0 && app.cp_.rate_ == 0);
  CHECK(Serial7.currents.size() >= beforePlanCurrents);
  for (size_t i = beforePlanCurrents; i < Serial7.currents.size(); ++i)
    CHECK(Serial7.currents[i] == 0);
  command("CP PLAN 50 NaN");
  command("CP PLAN 50 0");
  command("CP PLAN 50 120 extra");
  CHECK(!app.cp_.active() && !app.cp_.armed());
  command("HELP");
  CHECK(off());
  json(1, "GET_INFO");
  CHECK(app.session_.maxAid == 0);
  json(2, "HOME", 1);
  CHECK(!strcmp(app.state_, "IDLE"));
  CHECK(!app.session_.rejected.event[0]);
  CHECK(strstr(app.session_.rejected.ack, "HOME_UNAVAILABLE"));
  const std::string original = app.session_.rejected.ack;
  json(2, "HOME", 1);
  CHECK(original == app.session_.rejected.ack);
  json(3, "HOME", 1);
  CHECK(app.session_.maxAid == 1);
  json(4, "SET_TEL_RATE", 2, ",\"hz\":10");
  CHECK(app.period_ == 100);
  json(4, "SET_TEL_RATE", 2, ",\"hz\":20");
  CHECK(app.period_ == 100);
  json(2, "HOME", 1);
  CHECK(app.session_.maxAid == 2);
  CHECK(original == app.session_.rejected.ack);
  json(5, "ARM", 0);
  CHECK(!strcmp(app.state_, "IDLE"));
  CHECK(app.session_.maxAid == 2);
  command("VAC CONFIG 6 1000 100 1 100");
  step(30);
  CHECK(app.vac_.released());
  command("VAC ARM");
  command("VAC RUNBOTH 33 33");
  CHECK(!off());
  CHECK(mockPwm[24] == saver_vacuum_bench::dutyToPwm(33));
  command("STOP");
  CHECK(!off()); // Compression STOP never releases suction.
  command("VAC OFF");
  CHECK(off());
  step(30);
  command("PPG ON");
  step(20);
  CHECK(app.ppgOn_);
  CHECK(Wire.begins == 1);
  CHECK(!app.ppg_.window_.hrOk());
  CHECK(!app.ppg_.window_.spo2Ok());
  command("CP ZERO");
  mockAdc[A16] = 2000;
  command("CP CAL 20");
  mockAdc[A16] = 1000;
  step(60);
  command("CP TARE");
  step(330);
  CHECK(app.cp_.forceCal_.zeroKnown);
  Wire1.nauRaw = 200000;
  command("CP FORCE_CAL 1");
  step(330);
  CHECK(app.cp_.forceCal_.confirmed);
  Wire1.nauRaw = 100000;
  step(60);
  command("CP TARGET 5 10");
  command("CP FORCELIMIT 600");
  command("CP CHECK");
  step(20);
#ifdef SAVER_DEMO_TEST_FIXTURE
  CHECK(!app.cp_.readyIssue());
  command("CP PREPARE");
  CHECK(app.prepared_);
  CHECK(!strcmp(app.state_, "CPR_READY"));
  command("CP RUN 1");
  CHECK(app.cp_.active());
  command("CP PREPARE");
  CHECK(app.cp_.active());
  CHECK(!strcmp(app.state_, "CPR_RUNNING"));
  step(30);
  CHECK(!Serial7.currents.empty());
  bool negative = false;
  for (int32_t current : Serial7.currents) {
    if (current < 0)
      negative = true;
    CHECK(current >= -6000 && current <= 6000);
  }
  CHECK(negative);
  command("STOP");
  CHECK(!app.cp_.active());
  CHECK(Serial7.currents.back() == 0);
  mockAdc[A16] = 1000;
  step(60);
  command("CP CLEAR");
  CHECK(!app.fault_[0]);
  command("CP PREPARE");
  command("CP RUN");
  CHECK(app.cp_.active());
  Wire1.nauRaw = 7000000;
  step(3);
  CHECK(!app.cp_.active());
  CHECK(app.cp_.fault());
  Wire1.nauRaw = 100000;
  step(60);
  command("CP CLEAR");
  command("CP PREPARE");
  command("CP RUN 1");
  CHECK(app.cp_.active());
  mockVescReplies = false;
  step(100);
  CHECK(!app.cp_.active());
  mockVescReplies = true;
  step(250);
  command("CP CLEAR");
  command("CP PREPARE");
  command("CP RUN 1");
  CHECK(app.cp_.active());
  Serial7.space = 0;
  step(5);
  CHECK(!app.cp_.active());
  CHECK(app.cp_.stopPending());
  Serial7.space = 4096;
  step(10);
  CHECK(!app.cp_.stopPending());
  CHECK(Serial7.currents.back() == 0);
  step(60);
  command("CP CLEAR");
  command("CP PREPARE");
  command("CP RUN 1");
  CHECK(app.cp_.active());
  Serial.connected = false;
  Serial.feed("CP PREPARE\nCP RUN 1\n");
  step(3);
  CHECK(!app.cp_.active());
  CHECK(!app.prepared_);
  Serial.connected = true;
  step(3);
  CHECK(!app.cp_.active());
  CHECK(!Serial.available());
  step(60);
  command("CP PREPARE");
  command("CP RUN 1");
  CHECK(app.cp_.active());
  Serial.feed("{");
  step(205);
  CHECK(!app.cp_.active());
  CHECK(!strcmp(app.state_, "FAULT"));
  Serial.feed("\n");
  step(60);
  command("CP CLEAR");
  command("CP PREPARE");
  command("CP RUN 1");
  CHECK(app.cp_.active());
  Serial.feed("{\"v\":1,\"t\":\"cmd\",\"seq\":55,\"cmd\":\"PAUSE_\rCPR\"}\n");
  step(4);
  CHECK(!app.cp_.active());
  CHECK(!app.pausePending_);
  step(60);
  command("CP CLEAR");
  CHECK(!app.fault_[0]);
  command("CP PREPARE");
  command("CONTROL REMOTE");
  json(10, "START_CPR", 3);
  CHECK(app.cp_.active());
  command("{\"v\":1,\"t\":\"cmd\",\"cmd\":\"PAUSE_CPR\",\"seq\":11}");
  CHECK(!app.cp_.active());
  CHECK(app.pausePending_);
  CHECK(!strcmp(app.state_, "CPR_RUNNING"));
  step(60);
  CHECK(!strcmp(app.state_, "CPR_PAUSED"));
  json(12, "RESUME_CPR", 4);
  CHECK(app.cp_.active());
  // Same accepted request cannot restart a stopped axis.
  command("STOP");
  json(12, "RESUME_CPR", 4);
  CHECK(!app.cp_.active());
  step(420);
  command("CONTROL LOCAL");
  command("CP PREPARE");
  command("CONTROL REMOTE");
  json(13, "START_CPR", 5);
  CHECK(app.cp_.active());
  step(410);
  CHECK(!app.cp_.active());
  CHECK(!strcmp(app.fault_, "3-CM-010"));
  command("CONTROL LOCAL");
  command("CP CLEAR");
  command("CP PREPARE");
  command("CP RUN 1");
  CHECK(app.cp_.active());
  Serial.space = 0;
  step(10);
  CHECK(app.cp_.active());
  CHECK(app.log_.dropped < 1000);
  Serial.space = 4096;
  mockAdc[A16] = 4095;
  step();
  CHECK(!app.cp_.active());
  CHECK(!strcmp(app.state_, "FAULT"));
  CHECK(Serial7.currents.back() == 0);
#else
  CHECK(app.cp_.readyIssue() != nullptr);
  command("CP PREPARE");
  CHECK(!app.prepared_);
  command("CP RUN 1");
  CHECK(!app.cp_.active());
  for (int32_t current : Serial7.currents)
    CHECK(current == 0);
#endif
  // JSON stop is applied even with old boot/missing aid. Malformed input is
  // never interpreted by substring matching, but still causes safe CP stop.
  app.cp_.zeroPending_ = false;
  command("{\"v\":1,\"t\":\"cmd\",\"seq\":50,\"cmd\":\"STOP_CPR\",\"bid\":99999,\"aid\":1}");
  CHECK(!app.cp_.active());
  CHECK(app.session_.maxAid <= 5);
  command("VAC OFF");
  CHECK(off());
  const auto boot = app.session_.boot;
  Serial.connected = false;
  step();
  CHECK(!app.cp_.active());
  CHECK(!app.prepared_);
  Serial.connected = true;
  step();
  command("");
  command("STATUS");
  CHECK(app.session_.boot == boot);
  CHECK(!app.cp_.active());
  step(40);
  CHECK(Serial.output.find("\"t\":\"tel\"") != std::string::npos);
  CHECK(Serial.output.find("\"t\":\"mot\"") != std::string::npos);
  CHECK(Serial.output.find("HOMING_DONE") == std::string::npos);
  CHECK(Serial.output.find("CP_ZERO_SET") == std::string::npos);
  command("CONTROL REMOTE_V1");
  command("{\"v\":1,\"t\":\"cmd\",\"seq\":61,\"cmd\":\"SET_TEL_RATE\",\"hz\":25}");
  CHECK(app.period_ == 40);
  command("{\"v\":1,\"t\":\"cmd\",\"seq\":61,\"cmd\":\"SET_TEL_RATE\",\"hz\":20}");
  CHECK(app.period_ == 40);
  command("{\"v\":1,\"t\":\"cmd\",\"seq\":62,\"cmd\":\"GET_INFO\"}");
  CHECK(app.legacy_);
  command("CONTROL REMOTE");
  CHECK(!app.legacy_);
  step(420);
  command("CONTROL LOCAL");
  command("STREAM OFF");
  CHECK(app.quiet_);
  command("STREAM ON");
  CHECK(!app.quiet_);
  for (unsigned i = 0; i < 1000; ++i)
    app.log_.flush();
  CHECK(!combinedTests());
  CHECK(!ppgDisplayTests());
  CHECK(!sensorlessTests());
  CHECK(!stopTestTests());
  // Real vacuum module: no immediate hold/release success, hysteresis,
  // attained-pressure fallback, timeout, ERR latch and invalid ADC handling.
  selectable::Log vacuumLog;
  selectable::Vacuum vacuum(vacuumLog);
  mockAdc[A8] = 3800;
  mockDigital[27] = mockDigital[36] = HIGH;
  vacuum.begin();
  auto vstep = [&](unsigned n) {
    while (n--) {
      mockUs += 5000;
      vacuum.tick();
    }
  };
  auto vcmd = [&](const char *text) {
    char b[128];
    snprintf(b, sizeof(b), "%s", text);
    selectable::Command c(b);
    vacuumLog.beginCommand();
    return vacuum.command(c) && !vacuumLog.failed();
  };
  auto rawPressure = [](float kPa) {
    return int((4.5f + kPa / 25.0f) / 1.47f / 3.3f * 4095.0f + .5f);
  };
  CHECK(vcmd("CONFIG 6 200 50 1 50"));
  vstep(14);
  CHECK(vacuum.released());
  CHECK(!vacuum.startAuto(-22.5f));
  CHECK(vacuum.active());
  CHECK(!vacuum.holding());
  mockAdc[A8] = rawPressure(-23);
  vstep(3);
  CHECK(!vacuum.active());
  CHECK(!vacuum.holding());
  vstep(12);
  CHECK(vacuum.holding());
  CHECK(!vacuum.released());
  mockAdc[A8] = rawPressure(-19);
  vstep(3);
  CHECK(vacuum.active());
  mockDigital[27] = LOW;
  vstep(1);
  CHECK(vacuum.fault());
  CHECK(!vacuum.active());
  mockDigital[27] = HIGH;
  CHECK(!vcmd("CLEAR_ERR"));
  vstep(55);
  CHECK(vcmd("CLEAR_ERR"));
  mockAdc[A8] = 3800;
  vstep(14);
  CHECK(vacuum.released());
  CHECK(!vacuum.startAuto(-22.5f));
  mockAdc[A8] = rawPressure(-21);
  vstep(45);
  CHECK(!vacuum.active());
  CHECK(!vacuum.fault());
  CHECK(vacuum.holding());
  mockAdc[A8] = rawPressure(-19);
  vstep(45);
  CHECK(vacuum.fault());
  CHECK(!vacuum.active());
  CHECK(vcmd("CLEAR_ERR"));
  CHECK(!vacuum.startAuto(-22.5f));
  mockAdc[A8] = 4095;
  vstep(3);
  CHECK(vacuum.fault());
  CHECK(!vacuum.active());
  for (unsigned i = 0; i < 1000; ++i)
    vacuumLog.flush();
  EEPROM.failWrites = true;
  mockAdc[A8] = 3800;
  saver_demo::App failedBoot;
  failedBoot.begin();
  CHECK(!failedBoot.session_.boot);
  CHECK(!strcmp(failedBoot.state_, "FAULT"));
  char badArm[] = "VAC ARM";
  failedBoot.local(badArm);
  CHECK(!failedBoot.vac_.active());
  CHECK(!strcmp(failedBoot.start(1), "BOOT_JOURNAL_UNAVAILABLE"));
  CHECK(!strcmp(failedBoot.startCombined(1), "BOOT_JOURNAL_UNAVAILABLE"));
  CHECK(failedBoot.resetFault() != nullptr);
  CHECK(!strcmp(failedBoot.state_, "FAULT"));
  EEPROM.failWrites = false;
  for (unsigned i = 0; i < 1000; ++i)
    failedBoot.log_.flush();
  if (argc > 1 && !strcmp(argv[1], "--wire"))
    std::printf("%s%s", Serial.completedStreams.c_str(), Serial.output.c_str());
  std::printf("PASS %u main firmware checks (%s), mock hardware only\n", checks,
#ifdef SAVER_DEMO_TEST_FIXTURE
              "synthetic fixture"
#else
              "shipping defaults"
#endif
  );
}
