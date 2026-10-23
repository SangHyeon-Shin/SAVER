#ifndef SAVER_MAX30102_OPTICAL_WINDOW_H
#define SAVER_MAX30102_OPTICAL_WINDOW_H

#include <stdint.h>

namespace saver_max_bench {

// EDIT only after repeating covered/uncovered optical bench measurements.
// Engineering heuristics for this module/LED setting, NOT medical thresholds.
// User's 2026-09-07 log: uncovered RED ~11k / IR ~18k (later both ~1k),
// covered RED ~200k / IR ~220k. Both channels must pass the entry threshold.
constexpr uint32_t CONTACT_ON_RAW = 50000;
constexpr uint32_t CONTACT_OFF_RAW = 30000;
constexpr uint32_t CONTACT_SETTLE_MS = 500;  // Continuous bright samples before entry.
constexpr uint32_t CONTACT_MAX_GAP_MS = 200; // Drop estimates if samples stop arriving.
constexpr uint32_t ADC_MAX = 0x3FFFF;
constexpr uint16_t WINDOW_SAMPLES = 200; // Eight seconds for repeatability checks.
constexpr uint16_t STEP_SAMPLES = 25;
static_assert(CONTACT_OFF_RAW < CONTACT_ON_RAW && CONTACT_ON_RAW < ADC_MAX,
              "Contact thresholds must have hysteresis and be inside ADC range");
static_assert(CONTACT_SETTLE_MS > 0 && CONTACT_MAX_GAP_MS > 0 && STEP_SAMPLES > 0 &&
                  STEP_SAMPLES < WINDOW_SAMPLES,
              "Contact and window settings must be nonzero and bounded");

enum class ContactState : uint8_t { None, Settling, Candidate };

// Pure C++ state/window policy: the exact implementation is also exercised by
// compile-time tests. No Arduino I/O, heap, medical decisions or static state.
class OpticalWindow {
public:
  // Runtime optical thresholds only; not a physiological calibration.
  constexpr bool configureContact(uint32_t on, uint32_t off) {
    if (off >= on || on >= ADC_MAX)
      return false;
    reset();
    onRaw_ = on;
    offRaw_ = off;
    return true;
  }
  constexpr uint32_t onRaw() const { return onRaw_; }
  constexpr uint32_t offRaw() const { return offRaw_; }

  constexpr void reset() {
    state_ = ContactState::None;
    seenSample_ = false;
    clearEstimates();
  }

  constexpr void expire(uint32_t now) {
    if (seenSample_ && (uint32_t)(now - lastSampleMs_) >= CONTACT_MAX_GAP_MS)
      reset();
  }

  // True means a NEW full window is ready for the estimator, not a valid pulse.
  constexpr bool push(uint32_t red, uint32_t ir, uint32_t now) {
    expire(now);
    seenSample_ = true;
    lastSampleMs_ = now;
    if (red <= offRaw_ || ir <= offRaw_ || red >= ADC_MAX || ir >= ADC_MAX) {
      // No release debounce: even a single low/rail sample revokes old results.
      state_ = ContactState::None;
      clearEstimates();
      return false;
    }
    if (state_ != ContactState::Candidate) {
      clearEstimates();
      if (red < onRaw_ || ir < onRaw_) {
        state_ = ContactState::None;
        return false;
      }
      if (state_ == ContactState::None) {
        state_ = ContactState::Settling;
        settlingSinceMs_ = now;
      }
      if ((uint32_t)(now - settlingSinceMs_) < CONTACT_SETTLE_MS)
        return false;
      state_ = ContactState::Candidate;
    }
    if (count_ >= WINDOW_SAMPLES)
      clearEstimates(); // Defensive bound if caller skipped calculation.
    red_[count_] = red;
    ir_[count_] = ir;
    return ++count_ == WINDOW_SAMPLES;
  }

  constexpr void completeCalculation(int32_t hr, bool hrValid, int32_t spo2, bool spo2Valid) {
    if (!contactReady() || count_ != WINDOW_SAMPLES) {
      clearEstimates();
      return;
    }
    // These are algorithm result flags, NOT independently verified physiology.
    hrOk_ = hrValid && hr > 0;
    spo2Ok_ = spo2Valid && spo2 >= 0 && spo2 <= 100;
    if (hrOk_)
      hr_ = hr;
    if (spo2Ok_)
      spo2_ = spo2;
    for (uint16_t i = 0; i < WINDOW_SAMPLES - STEP_SAMPLES; ++i) {
      red_[i] = red_[i + STEP_SAMPLES];
      ir_[i] = ir_[i + STEP_SAMPLES];
    }
    count_ = WINDOW_SAMPLES - STEP_SAMPLES;
  }

  constexpr bool contactReady() const { return state_ == ContactState::Candidate; }
  constexpr ContactState contactState() const { return state_; }
  constexpr const char *contactName() const {
    return state_ == ContactState::Candidate  ? "CANDIDATE"
           : state_ == ContactState::Settling ? "SETTLING"
                                              : "NONE";
  }
  constexpr uint16_t count() const { return count_; }
  constexpr bool hrOk() const { return contactReady() && hrOk_; }
  constexpr bool spo2Ok() const { return contactReady() && spo2Ok_; }
  constexpr int32_t hr() const { return hr_; }     // Only expose when hrOk().
  constexpr int32_t spo2() const { return spo2_; } // Only expose when spo2Ok().
  constexpr uint32_t *redData() { return red_; }
  constexpr uint32_t *irData() { return ir_; }

private:
  constexpr void clearEstimates() {
    count_ = 0;
    hrOk_ = false;
    spo2Ok_ = false;
    // Old buffers/numbers are inaccessible as valid data; all 200 samples must
    // be overwritten before another calculation. No fake numeric zero emitted.
  }

  ContactState state_ = ContactState::None;
  uint32_t onRaw_ = CONTACT_ON_RAW, offRaw_ = CONTACT_OFF_RAW;
  bool seenSample_ = false, hrOk_ = false, spo2Ok_ = false;
  uint32_t settlingSinceMs_ = 0, lastSampleMs_ = 0;
  uint16_t count_ = 0;
  uint32_t red_[WINDOW_SAMPLES] = {}, ir_[WINDOW_SAMPLES] = {};
  int32_t hr_ = 0, spo2_ = 0;
};

} // namespace saver_max_bench
#endif
