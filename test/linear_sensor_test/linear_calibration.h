#ifndef SAVER_LINEAR_CALIBRATION_H
#define SAVER_LINEAR_CALIBRATION_H
// Shared position conversion; no motor commands or automatic homing.

#include "linear_reading.h"
#include <float.h>

namespace saver_linear {
constexpr bool finiteNumber(float v) { return v >= -FLT_MAX && v <= FLT_MAX; }
constexpr bool usablePositionRaw(int raw) { return raw > 5 && raw < 4090; }
// Whole unsigned decimal token, no exponents, NaN/Inf, signs or trailing tokens.
constexpr bool parseMm(const char *text, float &out) {
  if (!text || !*text)
    return false;
  float value = 0, scale = 1;
  bool digit = false, dot = false;
  for (; *text; ++text) {
    if (*text == '.' && !dot) {
      dot = true;
      continue;
    }
    if (*text < '0' || *text > '9')
      return false;
    digit = true;
    if (dot) {
      scale *= .1f;
      value += (*text - '0') * scale;
    } else
      value = value * 10 + (*text - '0');
    if (!finiteNumber(value) || value > 75)
      return false;
  }
  if (!digit)
    return false;
  out = value;
  return true;
}

struct CalPoint {
  float mean = 0;
  int low = ADC_MAX, high = 0;
  uint16_t count = 0;
};

// Engineering collection settings, not sensor accuracy or a medical safety limit.
class PointCapture {
public:
  static constexpr uint32_t PERIOD_MS = 10, TIMEOUT_MS = 2000;
  static constexpr uint16_t SAMPLES = 50;
  constexpr void start(uint32_t now) {
    point_ = CalPoint{};
    sum_ = 0;
    started_ = last_ = now;
    active_ = true;
    error_ = nullptr;
  }
  constexpr void cancel() { active_ = false; }
  constexpr bool expire(uint32_t now) {
    if (!active_ || !elapsed(now, started_, TIMEOUT_MS))
      return false;
    active_ = false;
    error_ = "CAPTURE_TIMEOUT";
    return true;
  }
  constexpr bool due(uint32_t now) const { return active_ && elapsed(now, last_, PERIOD_MS); }
  constexpr bool add(int raw, uint32_t now) {
    if (expire(now) || !due(now))
      return false;
    last_ = now; // No catch-up burst or reuse of stale samples.
    if (!usablePositionRaw(raw)) {
      active_ = false;
      error_ = "CAPTURE_ADC_RAIL_OR_INVALID";
      return false;
    }
    sum_ += raw;
    if (raw < point_.low)
      point_.low = raw;
    if (raw > point_.high)
      point_.high = raw;
    ++point_.count;
    if (point_.count < SAMPLES)
      return false;
    point_.mean = static_cast<float>(sum_) / point_.count;
    active_ = false;
    return true;
  }
  constexpr bool active() const { return active_; }
  constexpr const char *error() const { return error_; }
  constexpr CalPoint point() const { return point_; }

private:
  CalPoint point_;
  uint32_t sum_ = 0, started_ = 0, last_ = 0;
  bool active_ = false;
  const char *error_ = nullptr;
};

class LinearCalibration {
public:
  constexpr void reset() { zeroKnown_ = ready_ = false; }
  constexpr void invalidateScale() { ready_ = false; }
  constexpr bool setZero(const CalPoint &p) {
    reset();
    if (!validPoint(p))
      return false;
    zero_ = p;
    zeroKnown_ = true;
    return true;
  }
  constexpr bool setSpan(const CalPoint &p, float distanceMm) {
    ready_ = false;
    if (!zeroKnown_ || !validPoint(p) || !finiteNumber(distanceMm) || distanceMm < 5 ||
        distanceMm > 75)
      return false;
    const float delta = p.mean - zero_.mean;
    if ((delta > -30 && delta < 30) || !(p.high < zero_.low || zero_.high < p.low))
      return false;
    span_ = p;
    distance_ = distanceMm;
    ready_ = true;
    return true;
  }
  constexpr bool position(int raw, float &mm) const {
    if (!ready_ || !usablePositionRaw(raw))
      return false;
    mm = (raw - zero_.mean) * distance_ / (span_.mean - zero_.mean);
    return finiteNumber(mm);
  }
  constexpr bool within(float mm) const {
    return ready_ && finiteNumber(mm) && mm >= 0 && mm <= distance_;
  }
  constexpr bool zeroKnown() const { return zeroKnown_; }
  constexpr bool ready() const { return ready_; }
  constexpr CalPoint zero() const { return zero_; }
  constexpr CalPoint span() const { return span_; }
  constexpr float distance() const { return distance_; }
  constexpr float mmPerCount() const { return ready_ ? distance_ / (span_.mean - zero_.mean) : 0; }

private:
  static constexpr bool validPoint(const CalPoint &p) {
    return p.count == PointCapture::SAMPLES && usablePositionRaw(p.low) &&
           usablePositionRaw(p.high) && p.low <= p.high && finiteNumber(p.mean) &&
           p.mean >= p.low && p.mean <= p.high;
  }
  CalPoint zero_, span_;
  float distance_ = 0;
  bool zeroKnown_ = false, ready_ = false;
};
} // namespace saver_linear
#endif
