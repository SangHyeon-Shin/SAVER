#include <Arduino.h>
#include <Wire.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#ifdef SELECTABLE_TEST_FIXTURE
#include "fixture_config.h"
#endif
#define private public
#include "../selectable_system_bench_test.ino"
#undef private
uint64_t mockUs = 0;
int mockAdc[42] = {}, mockDigital[42] = {}, mockPwm[42] = {};
bool mockVescReplies = true;
MockSerial Serial, Serial7;
TwoWire Wire, Wire1;
static unsigned checks = 0;
#define CHECK(x)                                                                                   \
  do {                                                                                             \
    ++checks;                                                                                      \
    if (!(x)) {                                                                                    \
      std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x);                                    \
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
  step();
}
static bool off() { return !mockDigital[26] && !mockDigital[37] && !mockPwm[24] && !mockPwm[33]; }
int main() {
  Serial7.uart = true;
  Wire.regs[255] = 0x15;
  mockAdc[A8] = 3731;
  mockAdc[A16] = 1000;
  mockDigital[27] = mockDigital[36] = HIGH;
  setup();
  CHECK(off());
  CHECK(!Wire.begins && !Wire1.begins);
  step(2);
  auto &app = selectable::instance();
  command("ARM");
  CHECK(app.mode_ == selectable::Mode::None);
  CHECK(off());
  command("MODE VACUUM");
  step(60);
  CHECK(!Wire.begins && !Wire1.begins);
  const auto queriesBefore = Serial7.queries;
  command("ARM");
  command("RUNBOTH 33 33");
  CHECK(mockDigital[26] && mockDigital[37]);
  CHECK(mockPwm[24] == saver_vacuum_bench::dutyToPwm(33));
  CHECK(Serial7.queries == queriesBefore);
  command("TARGET 5 10");
  CHECK(off()); // Wrong-mode commands fail stopped.
  command("ARM");
  command("RUN1 6");
  CHECK(mockDigital[26]);
  mockDigital[27] = LOW;
  step();
  CHECK(off());
  CHECK(app.vacuum_.state_.motor(0).fault());
  command("CLEAR_ERR");
  CHECK(app.vacuum_.state_.motor(0).fault());
  mockDigital[27] = HIGH;
  step(60);
  command("CLEAR_ERR");
  CHECK(!app.vacuum_.state_.motor(0).fault());
  CHECK(off());
  command("ARM");
  command("RUNBOTH 6 6");
  CHECK(!off());
  mockAdc[A8] = 4095;
  step(3);
  CHECK(off());
  mockAdc[A8] = 3731;
  command("ARM");
  command("RUN1 6");
  command("RUN1 33 500");
  CHECK(off()); // Removed duration is not an unlimited run.
  command("ARM");
  command("RUN1 6");
  Serial.feed("RUN");
  step(210);
  CHECK(off());
  Serial.feed("1 33\n");
  step();
  CHECK(off());
  command("MODE PPG");
  step(20);
  CHECK(off());
  CHECK(Wire.begins == 1);
  CHECK(!Wire1.begins);
  CHECK(app.ppg_.state_ == selectable::Ppg::State::Running);
  CHECK(!app.ppg_.window_.hrOk() && !app.ppg_.window_.spo2Ok());
  command("ARM");
  CHECK(off());
  CHECK(app.mode_ == selectable::Mode::Ppg);
  command("MODE COMPRESSION");
  CHECK(Wire.regs[9] == 0x80);
  CHECK(off());
  step(250);
  CHECK(Wire1.begins == 1);
  CHECK(Serial7.queries > queriesBefore);
  const auto ppgReads = Wire.reads;
  step(10);
  CHECK(Wire.reads == ppgReads);
  command("RUN1 10");
  CHECK(off());
  CHECK(!app.compression_.active());
  command("ZERO");
  CHECK(app.compression_.calibration_.zeroKnown());
  mockAdc[A16] = 2000;
  command("CAL 20");
  CHECK(app.compression_.calibration_.calibrated());
  mockAdc[A16] = 1000;
  step(60);
  command("TARE");
  step(330);
  CHECK(app.compression_.forceCal_.zeroKnown);
  Wire1.nauRaw = 200000;
  command("FORCE_CAL 1");
  step(330);
  CHECK(app.compression_.forceCal_.confirmed);
  double force = 0;
  CHECK(app.compression_.forceCal_.force(200000, force));
  CHECK(force > 9.806 && force < 9.807);
  // Failed replacement calibration cannot keep exposing the old force scale.
  command("FORCE_CAL 1");
  Wire1.nauRaw = 100000;
  step(330);
  CHECK(!app.compression_.forceCal_.confirmed);
  command("TARE");
  step(330);
  Wire1.nauRaw = 200000;
  command("FORCE_CAL 1");
  step(330);
  CHECK(app.compression_.forceCal_.confirmed);
  command("TARGET 5 10");
  step(60);
#ifndef SELECTABLE_TEST_FIXTURE
  command("ARM");
  command("RUN 1");
  CHECK(!app.compression_.active());
  CHECK(saver_depth_bench::configIssue(true) != nullptr);
#else
  CHECK(saver_depth_bench::configIssue(true) == nullptr);
  command("ARM");
  CHECK(app.compression_.gate_.armed());
  command("RUN 1");
  CHECK(app.compression_.active());
  step(40);
  CHECK(app.compression_.active());
  bool nonzero = false;
  for (int32_t v : Serial7.currents)
    if (v)
      nonzero = true;
  CHECK(nonzero);
  CHECK(off()); // Only the VESC can receive nonzero in this mode.
  command("STOP");
  CHECK(!app.compression_.active());
  CHECK(Serial7.currents.back() == 0);
  // A scripted sensor trajectory exercises a complete down-and-back profile.
  // This is not a motor/plant or biomechanical simulation.
  step(60);
  command("ARM");
  command("RUN 1");
  CHECK(app.compression_.active());
  for (unsigned i = 0; i < 1210 && app.compression_.active(); ++i) {
    const auto ref =
        saver_depth::reference(0, 5, 10, uint32_t(micros() + 5000 - app.compression_.started_), 1);
    mockAdc[A16] = 1000 + int(ref.mm * 50 + .5f);
    step();
  }
  CHECK(!app.compression_.active());
  CHECK(!app.compression_.gate_.fault());
  CHECK(Serial7.currents.back() == 0);
  mockAdc[A16] = 1000;
  step(60);
  command("ARM");
  command("RUN 1");
  CHECK(app.compression_.active());
  mockVescReplies = false;
  step(80);
  CHECK(!app.compression_.active());
  CHECK(app.compression_.gate_.fault());
  CHECK(Serial7.currents.back() == 0);
  mockVescReplies = true;
  step(250);
  command("CLEAR");
  step(60);
  command("ARM");
  command("RUN 1");
  CHECK(app.compression_.active());
  mockUs += 25000;
  loop();
  CHECK(!app.compression_.active());
  CHECK(Serial7.currents.back() == 0);
  step(60);
  command("CLEAR");
  step(60);
  command("ARM");
  command("RUN 1");
  CHECK(app.compression_.active());
  mockAdc[A16] = 4095;
  step();
  CHECK(!app.compression_.active());
  CHECK(Serial7.currents.back() == 0);
  mockAdc[A16] = 1000;
  step(60);
  command("CLEAR");
  step(60);
  command("ARM");
  command("RUN 1");
  CHECK(app.compression_.active());
  Wire1.fail = true;
  step();
  CHECK(!app.compression_.active());
  CHECK(!app.compression_.forceCal_.confirmed);
  CHECK(Serial7.currents.back() == 0);
  Wire1.fail = false;
#endif
  command("MODE VACUUM");
  step(60);
  command("ARM");
  command("RUNBOTH 6 6");
  CHECK(!off());
  Serial.connected = false;
  step();
  CHECK(off());
  CHECK(app.mode_ == selectable::Mode::None);
  Serial.feed("MODE VACUUM\nARM\nRUNBOTH 100 100\n");
  Serial.connected = true;
  step(10);
  CHECK(off());
  CHECK(app.mode_ == selectable::Mode::None);
  command("MODE PPG");
  step(20);
  Wire.fail = true;
  command("MODE VACUUM");
  CHECK(app.mode_ == selectable::Mode::None);
  CHECK(off());
  command("ARM");
  CHECK(off());
  Wire.fail = false;
  command("MODE VACUUM");
  CHECK(app.mode_ == selectable::Mode::Vacuum);
  Serial7.space = 0;
  command("MODE COMPRESSION");
  CHECK(app.mode_ == selectable::Mode::None);
  CHECK(app.compression_.stopPending());
  Serial7.space = 4096;
  step();
  CHECK(!app.compression_.stopPending());
  // Selecting an absent PPG module fails locally, without locking a module
  // which was never energized by this sketch.
  Wire.fail = true;
  command("MODE PPG");
  CHECK(app.ppg_.quiet());
  command("MODE VACUUM");
  CHECK(app.mode_ == selectable::Mode::Vacuum);
  Wire.fail = false;
  step(60);
  command("ARM");
  command("RUNBOTH 6 6");
  CHECK(!off());
  command("SET1 33");
  CHECK(mockPwm[24] == saver_vacuum_bench::dutyToPwm(33));
  command("SET2 0");
  CHECK(!mockDigital[37] && mockDigital[26]);
  // USB congestion cannot suppress a physical error check or restart output.
  Serial.space = 0;
  mockDigital[27] = LOW;
  step(1600);
  CHECK(off());
  CHECK(app.log_.dropped > 0);
  mockDigital[27] = HIGH;
  step(60);
  Serial.space = 4096;
  // Tiny USB space: no blocking and no interleaving of telemetry/urgent lines.
  selectable::Log log;
  Serial.output.clear();
  Serial.space = 3;
  log.data("TELEMETRY\n");
  log.flush();
  log.emit("URGENT");
  for (unsigned i = 0; i < 10; ++i)
    log.flush();
  CHECK(Serial.output == "TELEMETRY\nURGENT\n");
  log.data("OLD\n");
  log.flush();
  log.clear();
  log.emit("NEW");
  for (unsigned i = 0; i < 10; ++i)
    log.flush();
  CHECK(Serial.output.find("NEW\n") != std::string::npos);
  std::printf("PASS: %u actual-sketch integration checks (%s). No hardware tested.\n", checks,
#ifdef SELECTABLE_TEST_FIXTURE
              "synthetic configured fixture"
#else
              "shipping guarded defaults"
#endif
  );
}
