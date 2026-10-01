// Diagnostic ADC sampling only. A finished capture does NOT calibrate, detect
// sensor presence, certify stability, or enable the motor/control loop.
#ifndef SAVER_CALIBRATION_CAPTURE_H
#define SAVER_CALIBRATION_CAPTURE_H

#include "depth_control.h"

namespace saver_depth_bench {

class CalibrationCapture {
public:
  static constexpr uint16_t SAMPLE_COUNT = 200;
  static constexpr uint32_t TIMEOUT_MS = 2000;

  constexpr bool start(float pointMm, uint32_t nowMs) {
    cancel(); // An invalid replacement request must not leave an old capture active.
    if (!saver_depth::finite(pointMm) || pointMm < 0.0f || pointMm > saver_depth::NOMINAL_SPAN_MM)
      return false;
    pointMm_ = pointMm;
    startedMs_ = nowMs;
    active_ = true;
    return true;
  }

  constexpr void cancel() {
    pointMm_ = 0.0f;
    startedMs_ = 0;
    sumRaw_ = 0;
    count_ = 0;
    minRaw_ = saver_depth::ADC_MAX;
    maxRaw_ = 0;
    active_ = ready_ = timedOut_ = railSeen_ = rawInvalidSeen_ = false;
  }

  // Call once for each newly acquired ADC sample, not repeatedly for stale data.
  // The caller supplies the acquisition timestamp; this helper does not schedule I/O.
  constexpr void sample(int raw, uint32_t nowMs) {
    expire(nowMs); // At the deadline, even the final sample is too late.
    if (!active_)
      return;
    if (raw < 0 || raw > saver_depth::ADC_MAX) {
      rawInvalidSeen_ = true;
      return; // Invalid data must not be replaced with a fictitious zero sample.
    }
    if (raw <= 1 || raw >= saver_depth::ADC_MAX - 1)
      railSeen_ = true; // End codes remain diagnostic samples, never certified positions.
    if (raw < minRaw_)
      minRaw_ = raw;
    if (raw > maxRaw_)
      maxRaw_ = raw;
    sumRaw_ += static_cast<uint32_t>(raw);
    ++count_;
    if (count_ == SAMPLE_COUNT) {
      active_ = false;
      ready_ = true; // Collection complete; NOT a claim of measurement quality.
    }
  }

  constexpr void expire(uint32_t nowMs) {
    if (active_ && static_cast<uint32_t>(nowMs - startedMs_) >= TIMEOUT_MS) {
      active_ = false;
      timedOut_ = true;
    }
  }

  constexpr bool active() const { return active_; }
  constexpr bool ready() const { return ready_; }
  constexpr bool timedOut() const { return timedOut_; }
  constexpr float pointMm() const { return pointMm_; }
  constexpr uint16_t count() const { return count_; }
  // Statistics are meaningful ONLY when count() > 0; omit them otherwise.
  constexpr int minRaw() const { return minRaw_; }
  constexpr int maxRaw() const { return maxRaw_; }
  constexpr float meanRaw() const {
    return count_ > 0 ? static_cast<float>(sumRaw_) / count_ : 0.0f;
  }
  constexpr bool railSeen() const { return railSeen_; }
  constexpr bool rawInvalidSeen() const { return rawInvalidSeen_; }

private:
  float pointMm_ = 0.0f;
  uint32_t startedMs_ = 0, sumRaw_ = 0;
  uint16_t count_ = 0;
  int minRaw_ = saver_depth::ADC_MAX, maxRaw_ = 0;
  bool active_ = false, ready_ = false, timedOut_ = false;
  bool railSeen_ = false, rawInvalidSeen_ = false;
};

} // namespace saver_depth_bench
#endif
