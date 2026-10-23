#ifndef SAVER_DEMO_VACUUM_H
#define SAVER_DEMO_VACUUM_H
#include "bench_io.h"
#include "src/pressure_conversion.h"
#include "src/bench_guard.h"
#include <math.h>
namespace selectable {
class Vacuum {
public:
  explicit Vacuum(Log &log) : log_(log) {}
  void begin() {
    analogWriteResolution(8);
    for (unsigned i = 0; i < 2; ++i) {
      digitalWriteFast(en(i), LOW);
      digitalWriteFast(vsp(i), LOW);
      pinMode(en(i), OUTPUT);
      pinMode(vsp(i), OUTPUT);
      analogWriteFrequency(vsp(i), 10000);
      pinMode(err(i), INPUT);
      pinMode(run(i), INPUT);
    }
    pinMode(A8, INPUT_DISABLE);
    stop();
    sample();
  }
  void stop() {
    automatic_ = hold_ = false;
    state_.stop(3, true);
    sync();
  }
  bool active() const { return state_.runningMask() != 0; }
  bool holding() const { return automatic_ && hold_; }
  bool requested() const { return automatic_; }
  bool released() const { return released_; }
  bool fault() const { return fault_; }
  const char *faultReason() const { return reason_; }
  bool pressure(float &kPa) const {
    kPa = pressure_.pressurePa / 1000.0f;
    return pressure_.ok && uint32_t(millis() - sampleAt_) < 100;
  }
  bool configured() const { return maxOnMs_ && stableMs_ && releaseMs_ && releaseBand_ > 0; }
  uint32_t acquisitionLimitMs() const { return maxOnMs_; }
  const char *startAuto(float target) {
    if (!configured())
      return "VAC_CONFIG_REQUIRED";
    if (target != -22.5f)
      return "VAC_TARGET_NOT_REVIEWED";
    if (fault_)
      return "VAC_FAULT_USE_CLEAR_ERR";
    sample();
    observe();
    const char *why = state_.arm(pressure_.ok);
    if (why)
      return why;
    why = state_.start(3, duty_, duty_, pressure_.ok);
    if (why)
      return why;
    automatic_ = true;
    hold_ = false;
    released_ = false;
    stable_ = releaseStable_ = false;
    onAt_ = millis();
    offAtTarget_ = target;
    sync();
    return nullptr;
  }
  void tick() {
    observe();
    if (uint32_t(millis() - sampleAt_) >= 10)
      sample();
    float kPa = 0;
    const bool valid = pressure(kPa);
    if (!valid && (active() || automatic_))
      trip("PRESSURE_UNAVAILABLE");
    if (active() && maxOnMs_ && uint32_t(millis() - onAt_) >= maxOnMs_) {
      // D-27 fallback: stop at attained pressure only if the reviewed lower
      // hold bound (-20 kPa) is met. Otherwise fail stopped, never run forever.
      if (automatic_ && valid && kPa <= -20) {
        offAtTarget_ = kPa;
        state_.stop(3, false);
        sync();
      } else
        trip("BLOWER_ON_TIME_LIMIT");
    }
    if (automatic_ && !fault_ && valid) {
      if (active() && kPa <= offAtTarget_) {
        state_.stop(3, false);
        sync();
      }
      if (!active() && kPa > -20) {
        const char *why = state_.start(3, duty_, duty_, true);
        if (why)
          trip(why);
        else {
          onAt_ = millis();
          sync();
        }
      }
      if (kPa <= -20) {
        if (!stable_) {
          stable_ = true;
          stableAt_ = millis();
        }
        if (uint32_t(millis() - stableAt_) >= stableMs_)
          hold_ = true;
      } else
        stable_ = false;
      if (hold_ && kPa > -10)
        trip("VACUUM_LOST");
    }
    const bool releaseCandidate =
        configured() && !active() && !automatic_ && valid && fabsf(kPa) <= releaseBand_;
    if (!releaseCandidate) {
      releaseStable_ = released_ = false;
    } else {
      if (!releaseStable_) {
        releaseStable_ = true;
        releaseAt_ = millis();
      }
      released_ = uint32_t(millis() - releaseAt_) >= releaseMs_;
    }
  }
  bool command(const Command &c) {
    const char *why = nullptr;
    uint32_t a = 0, b = 0, t = 0, stable = 0, release = 0;
    float band = 0;
    if (c.is("CONFIG", 6)) {
      if (active() || automatic_)
        why = "VAC_CONFIG_REQUIRES_OFF";
      else if (!saver_vacuum_bench::parseUnsigned(c.tokens[1], a) || a < 1 || a > 100 ||
               !saver_vacuum_bench::parseUnsigned(c.tokens[2], t) || !t || t > 600000 ||
               !saver_vacuum_bench::parseUnsigned(c.tokens[3], stable) || !stable || stable > t ||
               !saver_depth_bench::parseNumber(c.tokens[4], band) || band <= 0 || band >= 10 ||
               !saver_vacuum_bench::parseUnsigned(c.tokens[5], release) || !release ||
               release > 600000)
        why = "BAD_VAC_CONFIG";
      else {
        duty_ = uint8_t(a);
        maxOnMs_ = t;
        stableMs_ = stable;
        releaseBand_ = band;
        releaseMs_ = release;
        released_ = releaseStable_ = false;
      }
    } else if (c.is("ARM", 1)) {
      if (!configured())
        why = "VAC_CONFIG_REQUIRED";
      else if (fault_)
        why = "VAC_FAULT_USE_CLEAR_ERR";
      else {
        sample();
        observe();
        why = state_.arm(pressure_.ok);
      }
    } else if (c.is("OFF", 1))
      stop();
    else if (c.is("CLEAR_ERR", 1)) {
      if (active() || automatic_)
        why = "VAC_CLEAR_REQUIRES_OFF";
      else {
        sample();
        observe();
        why = state_.clear(3, millis());
        if (!why && pressure_.ok) {
          fault_ = false;
          reason_ = "NONE";
        } else if (!why)
          why = "PRESSURE_UNAVAILABLE";
      }
    } else if (c.count && (!strcmp(c.tokens[0], "RUN1") || !strcmp(c.tokens[0], "RUN2") ||
                           !strcmp(c.tokens[0], "RUNBOTH"))) {
      const uint8_t mask = !strcmp(c.tokens[0], "RUN1") ? 1 : !strcmp(c.tokens[0], "RUN2") ? 2 : 3;
      if (!configured())
        why = "VAC_CONFIG_REQUIRED";
      else if (fault_ || automatic_)
        why = "VAC_FAULT_OR_AUTOMATIC_ACTIVE";
      else {
        why = saver_vacuum_bench::parseRunDuties(mask, c.count > 1 ? c.tokens[1] : nullptr,
                                                 c.count > 2 ? c.tokens[2] : nullptr,
                                                 c.count > 3 ? c.tokens[3] : nullptr, duty_, a, b);
        if (!why)
          why = state_.start(mask, a, b, pressure_.ok);
        if (!why) {
          onAt_ = millis();
          released_ = false;
          sync();
        }
      }
    } else
      return false;
    if (why) {
      stop();
      log_.emit("ERROR,%s", why);
    } else
      log_.emit("OK,VAC_%s", c.tokens[0]);
    return true;
  }
  void report() {
    log_.emit("VACUUM,configured=%u,auto=%u,holding=%u,released=%u,fault=%u,m1_pct=%u,m2_pct=%u,"
              "adc=%d,reason=%s",
              configured(), automatic_, hold_, released_, fault_, state_.motor(0).duty(),
              state_.motor(1).duty(), pressure_.raw, reason_);
  }

private:
  static uint8_t vsp(unsigned i) { return i ? 33 : 24; }
  static uint8_t en(unsigned i) { return i ? 37 : 26; }
  static uint8_t err(unsigned i) { return i ? 36 : 27; }
  static uint8_t run(unsigned i) { return i ? 32 : 25; }
  void sync() {
    for (unsigned i = 0; i < 2; ++i) {
      digitalWriteFast(en(i), LOW);
      analogWrite(vsp(i), saver_vacuum_bench::dutyToPwm(state_.motor(i).duty()));
      if (state_.motor(i).running())
        digitalWriteFast(en(i), HIGH);
    }
  }
  void sample() {
    pressure_ = saver_pressure_bench::convertPressure(analogRead(A8));
    sampleAt_ = millis();
  }
  void trip(const char *why) {
    stop();
    fault_ = true;
    reason_ = why;
    log_.emit("FAULT,%s,vacuum_outputs_off=1", why);
  }
  void observe() {
    const auto e = state_.observe(digitalRead(err(0)) == LOW, digitalRead(err(1)) == LOW, millis());
    if (e.faultMask) {
      sync();
      if (e.stopMask)
        trip("BLOWER_ERR_LOW");
    }
  }
  Log &log_;
  saver_vacuum_bench::CombinedState state_;
  saver_pressure_bench::PressureSample pressure_;
  uint8_t duty_ = 6;
  bool automatic_ = false, hold_ = false, fault_ = false, released_ = false;
  bool stable_ = false, releaseStable_ = false;
  float releaseBand_ = 0, offAtTarget_ = -22.5f;
  uint32_t maxOnMs_ = 0, stableMs_ = 0, releaseMs_ = 0;
  uint32_t sampleAt_ = 0, onAt_ = 0, stableAt_ = 0, releaseAt_ = 0;
  const char *reason_ = "NONE";
};
} // namespace selectable
#endif
