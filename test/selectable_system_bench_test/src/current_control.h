// Arduino-free port of test/lm/sketch_sep16a: position PID -> motor current.
// Values are bench tuning, NOT validated force/current safety limits.
#ifndef SAVER_CURRENT_CONTROL_H
#define SAVER_CURRENT_CONTROL_H
#include "depth_control.h"

namespace saver_current {
using saver_depth::clamp;
using saver_depth::finite;
struct Gains {
  float kp, ki, kd; // A/mm, A/(mm*s), A/(mm/s)
  float integralLimitMmS, velocityTauS;
};
constexpr bool validGains(const Gains &g) {
  return finite(g.kp) && finite(g.ki) && finite(g.kd) && finite(g.integralLimitMmS) &&
         finite(g.velocityTauS) && g.kp > 0 && g.kp <= 5 && g.ki >= 0 && g.ki <= 3 && g.kd >= 0 &&
         g.kd <= .5f && g.integralLimitMmS > 0 && g.velocityTauS > 0;
}
class PositionFilter {
public:
  constexpr void reset() { ready_ = false; }
  constexpr float step(float mm, float dt, float tau) {
    if (!ready_) {
      value_ = mm;
      ready_ = true;
    } else
      value_ += dt / (tau + dt) * (mm - value_);
    return value_;
  }

private:
  float value_ = 0;
  bool ready_ = false;
};
class Pid {
public:
  constexpr void reset(float mm) {
    previous_ = mm;
    integral_ = velocity_ = p_ = i_ = d_ = output_ = 0;
    valid_ = finite(mm);
  }
  constexpr float step(float reference, float measured, float dt, const Gains &g, float limitA) {
    if (!finite(reference) || !finite(measured) || !finite(dt) || dt <= 0 || dt >= .020f ||
        !validGains(g) || !finite(limitA) || limitA <= 0 || !valid_) {
      valid_ = false;
      output_ = 0;
      return output_;
    }
    const float error = reference - measured;
    const float velocity = (measured - previous_) / dt;
    previous_ = measured;
    velocity_ += dt / (g.velocityTauS + dt) * (velocity - velocity_);
    p_ = g.kp * error;
    d_ = -g.kd * velocity_;
    // Freeze integration pushing further into current saturation. Ki=0 clears history.
    const float candidate = g.ki > 0
                                ? static_cast<float>(clamp(integral_ + error * dt,
                                                           -g.integralLimitMmS, g.integralLimitMmS))
                                : 0;
    const float proposed = p_ + g.ki * candidate + d_;
    if (g.ki == 0 || !((proposed > limitA && error > 0) || (proposed < -limitA && error < 0)))
      integral_ = candidate;
    i_ = g.ki * integral_;
    output_ = static_cast<float>(clamp(p_ + i_ + d_, -limitA, limitA));
    valid_ = finite(output_) && finite(velocity_) && finite(p_) && finite(i_) && finite(d_);
    if (!valid_)
      output_ = 0;
    return output_;
  }
  constexpr bool valid() const { return valid_; }
  constexpr float p() const { return p_; }
  constexpr float i() const { return i_; }
  constexpr float d() const { return d_; }
  constexpr float velocity() const { return velocity_; }

private:
  float previous_ = 0, integral_ = 0, velocity_ = 0, p_ = 0, i_ = 0, d_ = 0, output_ = 0;
  bool valid_ = false;
};

// ZERO sets the manually positioned working origin. CAL measures positive travel
// from it, not absolute sensor position. No automatic homing, no EEPROM writes.
class Calibration {
public:
  static constexpr bool usableRaw(int raw) { return raw > 5 && raw < 4090; }
  constexpr void reset() { zeroKnown_ = calibrated_ = false; }
  constexpr bool zero(int raw) {
    reset();
    if (!usableRaw(raw))
      return false;
    zeroRaw_ = raw;
    zeroKnown_ = true;
    return true;
  }
  constexpr bool calibrate(int raw, float knownMm) {
    calibrated_ = false; // Failed replacement cannot leave an old scale enabled.
    const int delta = raw - zeroRaw_;
    const int span = delta < 0 ? -delta : delta;
    if (!zeroKnown_ || !usableRaw(raw) || !finite(knownMm) || knownMm < 5 || knownMm > 60 ||
        span < 30)
      return false;
    const float fullSpan = span * 75.0f / knownMm;
    if (fullSpan < 100 || fullSpan > 10000)
      return false;
    calRaw_ = raw;
    knownMm_ = knownMm;
    calibrated_ = true;
    return true;
  }
  constexpr bool position(int raw, float &mm) const {
    if (!calibrated_ || !usableRaw(raw))
      return false;
    mm = (raw - zeroRaw_) * knownMm_ / (calRaw_ - zeroRaw_);
    return finite(mm); // Linear estimate; software travel and target coverage checked separately.
  }
  constexpr bool covers(float depth) const {
    return calibrated_ && finite(depth) && depth > 0 && depth < knownMm_;
  }
  constexpr bool zeroKnown() const { return zeroKnown_; }
  constexpr bool calibrated() const { return calibrated_; }
  constexpr int zeroRaw() const { return zeroRaw_; }
  constexpr int calRaw() const { return calRaw_; }
  constexpr float knownMm() const { return knownMm_; }

private:
  bool zeroKnown_ = false, calibrated_ = false;
  int zeroRaw_ = 0, calRaw_ = 0;
  float knownMm_ = 0;
};
} // namespace saver_current
#endif
