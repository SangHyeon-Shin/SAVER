#ifndef SAVER_PPG_DIAGNOSTIC_H
#define SAVER_PPG_DIAGNOSTIC_H
#include "src/ppg_signal.h"
#include <spo2_algorithm.h>
namespace selectable {
// Opt-in BENCH display only. Bypasses optical-contact thresholds, channel/
// period agreement and repeated-window confirmation; never normal telemetry.
// Keep transport integrity, fresh contiguous samples, zero/rail rejection,
// arithmetic bounds and the library's own result flags. No invented physiology.
class PpgDiagnostic {
public:
  void enable(bool on) {
    enabled_ = on;
    reset();
  }
  bool enabled() const { return enabled_; }
  void reset() {
    count_ = 0;
    seen_ = calculated_ = hrOk_ = spo2Ok_ = false;
    reason_ = enabled_ ? "COLLECTING_100_SAMPLES" : "OFF";
  }
  void expire(uint32_t now) {
    if (seen_ && uint32_t(now - sampleAt_) >= saver_max_bench::CONTACT_MAX_GAP_MS)
      reset();
  }
  void push(uint32_t red, uint32_t ir, uint32_t now) {
    if (!enabled_)
      return;
    expire(now);
    if (!red || !ir || red >= saver_max_bench::ADC_MAX || ir >= saver_max_bench::ADC_MAX) {
      reset();
      reason_ = "ZERO_OR_RAIL_INPUT";
      return;
    }
    seen_ = true;
    sampleAt_ = now;
    red_[count_] = red;
    ir_[count_++] = ir;
    if (count_ != SAMPLES)
      return;
    // Same common power-of-two scaling as the normal legacy window. Protect
    // the library's signed AC*DC products without calibrating or clamping SpO2.
    uint32_t redScaled[SAMPLES], irScaled[SAMPLES];
    uint32_t rmin = saver_max_bench::ADC_MAX, imin = rmin, rmax = 0, imax = 0;
    for (unsigned i = 0; i < SAMPLES; ++i) {
      if (red_[i] < rmin)
        rmin = red_[i];
      if (ir_[i] < imin)
        imin = ir_[i];
      if (red_[i] > rmax)
        rmax = red_[i];
      if (ir_[i] > imax)
        imax = ir_[i];
    }
    const uint32_t dc = rmax > imax ? rmax : imax;
    const uint32_t ac = rmax - rmin > imax - imin ? rmax - rmin : imax - imin;
    unsigned shift = 0;
    while (2ULL * (dc >> shift) * ((ac >> shift) + 1U) > 2147483647ULL)
      ++shift;
    for (unsigned i = 0; i < SAMPLES; ++i) {
      redScaled[i] = red_[i] >> shift;
      irScaled[i] = ir_[i] >> shift;
    }
    int8_t hrFlag = 0, spo2Flag = 0;
    int32_t hr = 0, spo2 = 0;
    maxim_heart_rate_and_oxygen_saturation(irScaled, SAMPLES, redScaled, &spo2, &spo2Flag, &hr,
                                           &hrFlag);
    calculated_ = true;
    calculatedAt_ = now;
    hrOk_ = hrFlag && hr > 0;
    spo2Ok_ = spo2Flag && spo2 >= 0 && spo2 <= 100;
    if (hrOk_)
      hr_ = hr;
    if (spo2Ok_)
      spo2_ = spo2;
    reason_ = hrOk_ || spo2Ok_ ? "UNVALIDATED_CANDIDATE" : "ALGORITHM_NO_ESTIMATE";
    for (unsigned i = 0; i < SAMPLES - STEP; ++i) {
      red_[i] = red_[i + STEP];
      ir_[i] = ir_[i + STEP];
    }
    count_ = SAMPLES - STEP;
  }
  bool fresh(uint32_t now) const {
    return enabled_ && seen_ && uint32_t(now - sampleAt_) < saver_max_bench::CONTACT_MAX_GAP_MS;
  }
  bool heartRate(uint32_t now, int32_t &value) const {
    value = hr_;
    return fresh(now) && calculated_ && hrOk_;
  }
  bool saturation(uint32_t now, int32_t &value) const {
    value = spo2_;
    return fresh(now) && calculated_ && spo2Ok_;
  }
  unsigned samples() const { return count_; }
  uint32_t resultAge(uint32_t now) const { return now - calculatedAt_; }
  const char *reason() const { return reason_; }

private:
  static constexpr unsigned SAMPLES = 100, STEP = 25; // Library's 25 Hz / 4 s window.
  bool enabled_ = false, seen_ = false, calculated_ = false, hrOk_ = false, spo2Ok_ = false;
  unsigned count_ = 0;
  uint32_t sampleAt_ = 0, calculatedAt_ = 0;
  uint32_t red_[SAMPLES] = {}, ir_[SAMPLES] = {};
  int32_t hr_ = 0, spo2_ = 0;
  const char *reason_ = "OFF";
};
} // namespace selectable
#endif
