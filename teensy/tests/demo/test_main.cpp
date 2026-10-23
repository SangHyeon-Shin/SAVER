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
int mockAdc[42] = {}, mockDigital[42] = {}, mockPwm[42] = {};
bool mockVescReplies = true;
MockSerial Serial, Serial7;
TwoWire Wire, Wire1;
MockMemory EEPROM;
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
