#ifndef SAVER_SELECTABLE_VACUUM_H
#define SAVER_SELECTABLE_VACUUM_H
#include "bench_io.h"
#include "src/pressure_conversion.h"

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
  }
  void enter() {
    stop();
    zero_.clear();
    sample();
    observe();
  }
  void stop() {
    state_.stop(3, true);
    sync();
  }
  void tick() {
    observe();
    if (uint32_t(millis() - sampleAt_) >= 10)
      sample();
    if (!pressure_.ok && (state_.armed() || state_.runningMask())) {
      stop();
      zero_.clear();
      log_.emit("FAULT,PRESSURE_UNAVAILABLE,reason=%s", pressure_.reason);
    }
  }
  bool command(const Command &c) {
    const char *why = nullptr;
    if (c.is("ARM", 1)) {
      sample();
      observe();
      why = state_.arm(pressure_.ok);
      sync();
    } else if (c.is("CLEAR_ERR", 1) || c.is("CLEAR_ERR1", 1) || c.is("CLEAR_ERR2", 1)) {
      const uint8_t mask = c.is("CLEAR_ERR1", 1) ? 1 : c.is("CLEAR_ERR2", 1) ? 2 : 3;
      observe();
      why = state_.clear(mask, millis());
      sync();
    } else if (c.is("ZERO", 1)) {
      stop();
      sample();
      if (!zero_.capture(pressure_.raw))
        why = "PRESSURE_UNAVAILABLE";
    } else if (c.is("UNZERO", 1)) {
      zero_.clear();
    } else if (c.count && (!strcmp(c.tokens[0], "RUN1") || !strcmp(c.tokens[0], "RUN2") ||
                           !strcmp(c.tokens[0], "RUNBOTH"))) {
      const uint8_t mask = !strcmp(c.tokens[0], "RUN1") ? 1 : !strcmp(c.tokens[0], "RUN2") ? 2 : 3;
      uint32_t a = 0, b = 0;
      why = saver_vacuum_bench::parseRunDuties(mask, c.count > 1 ? c.tokens[1] : nullptr,
                                               c.count > 2 ? c.tokens[2] : nullptr,
                                               c.count > 3 ? c.tokens[3] : nullptr, 6, a, b);
      sample();
      observe();
      if (!why)
        why = state_.start(mask, a, b, pressure_.ok);
      if (why)
        stop();
      else
        sync();
    } else if (c.is("SET1", 2) || c.is("SET2", 2)) {
      uint32_t duty = 0;
      sample();
      observe();
      if (!pressure_.ok)
        why = "PRESSURE_UNAVAILABLE";
      else if (!saver_vacuum_bench::parseUnsigned(c.tokens[1], duty))
        why = "BAD_SET_DUTY";
      else
        why = state_.set(c.is("SET1", 2) ? 0 : 1, duty);
      if (why)
        stop();
      else
        sync();
    } else
      return false;
    if (why) {
      stop();
      log_.emit("ERROR,%s", why);
    } else
      log_.emit("OK,%s,mode=VACUUM", c.tokens[0]);
    return true;
  }
  void report() {
    char b[384];
    const int n = snprintf(
        b, sizeof(b),
        "VACUUM,armed=%u,m1_pct=%u,m2_pct=%u,err1=%d,err2=%d,err_latch1=%u,err_latch2=%u,"
        "run1=%d,run2=%d,adc_raw=%d,pressure_ok=%u,reason=%s",
        state_.armed(), state_.motor(0).duty(), state_.motor(1).duty(), digitalRead(err(0)),
        digitalRead(err(1)), state_.motor(0).fault(), state_.motor(1).fault(), digitalRead(run(0)),
        digitalRead(run(1)), pressure_.raw, pressure_.ok, pressure_.reason);
    size_t used = n > 0 ? size_t(n) : 0;
    if (pressure_.ok && used < sizeof(b))
      used += snprintf(b + used, sizeof(b) - used, ",pressure_Pa=%.1f,sensor_V=%.4f",
                       double(pressure_.pressurePa), double(pressure_.sensorV));
    if (pressure_.ok && zero_.active() && used < sizeof(b))
      used += snprintf(b + used, sizeof(b) - used, ",relative_Pa=%.1f",
                       double(zero_.relativePa(pressure_)));
    if (used < sizeof(b) - 1) {
      b[used] = '\n';
      b[used + 1] = 0;
      log_.data(b);
    }
  }
  bool active() const { return state_.runningMask() != 0; }

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
    if (!pressure_.ok)
      zero_.clear();
  }
  void observe() {
    const auto event =
        state_.observe(digitalRead(err(0)) == LOW, digitalRead(err(1)) == LOW, millis());
    if (event.faultMask) {
      sync();
      log_.emit("FAULT,ERR_LOW,motor_mask=%u,latched=1", event.faultMask);
    }
  }
  Log &log_;
  saver_vacuum_bench::CombinedState state_;
  saver_pressure_bench::PressureSample pressure_;
  saver_pressure_bench::RelativePressureZero zero_;
  uint32_t sampleAt_ = 0;
};
} // namespace selectable
#endif
