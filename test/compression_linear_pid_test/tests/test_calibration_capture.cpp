#include "../calibration_capture.h"

using saver_depth_bench::CalibrationCapture;

constexpr CalibrationCapture capture(int raw, float point = 10.0f, uint32_t start = 0) {
  CalibrationCapture result;
  result.start(point, start);
  for (uint16_t i = 0; i < CalibrationCapture::SAMPLE_COUNT; ++i)
    result.sample(raw, start + static_cast<uint32_t>(i) * 5U);
  return result;
}

constexpr bool invalidStart(float point) {
  CalibrationCapture result = capture(1200);
  return !result.start(point, 1000) && !result.active() && !result.ready() && !result.timedOut() &&
         result.count() == 0;
}

constexpr bool invalidDoesNotCount() {
  CalibrationCapture result;
  result.start(20, 0);
  result.sample(1000, 1);
  result.sample(-1, 2);
  result.sample(4096, 3);
  result.sample(2000, 4);
  return result.active() && !result.ready() && result.rawInvalidSeen() && result.count() == 2 &&
         result.minRaw() == 1000 && result.maxRaw() == 2000 && result.meanRaw() == 1500.0f &&
         !result.railSeen();
}

constexpr bool invalidHistoryRetained() {
  CalibrationCapture result;
  result.start(0, 0);
  result.sample(-123, 0);
  for (unsigned i = 0; i < CalibrationCapture::SAMPLE_COUNT; ++i)
    result.sample(1000, i * 5U);
  return result.ready() && result.rawInvalidSeen() && result.count() == 200 &&
         result.meanRaw() == 1000.0f;
}

constexpr bool incomplete() {
  CalibrationCapture result;
  result.start(0, 0);
  for (unsigned i = 0; i < CalibrationCapture::SAMPLE_COUNT - 1U; ++i)
    result.sample(1000, i * 5U);
  return result.active() && !result.ready() && result.count() == 199;
}

constexpr bool fullRangeStatistics() {
  CalibrationCapture result;
  result.start(75, 0);
  for (unsigned i = 0; i < CalibrationCapture::SAMPLE_COUNT; ++i)
    result.sample(i % 2U ? 4095 : 0, i * 5U);
  return result.ready() && result.minRaw() == 0 && result.maxRaw() == 4095 &&
         result.meanRaw() == 2047.5f && result.railSeen() && !result.rawInvalidSeen();
}

constexpr bool readyIgnoresFurtherData() {
  CalibrationCapture result = capture(2000);
  result.sample(4095, 1100);
  result.sample(-1, 1200);
  result.expire(10000);
  return result.ready() && !result.active() && !result.timedOut() && result.count() == 200 &&
         result.meanRaw() == 2000.0f && !result.railSeen() && !result.rawInvalidSeen();
}

constexpr bool deadline(uint32_t start) {
  CalibrationCapture result;
  result.start(10, start);
  result.sample(1300, start + 1999U);
  if (!result.active() || result.timedOut() || result.count() != 1)
    return false;
  result.sample(1400, start + 2000U);
  result.sample(-1, start + 2001U);
  return !result.active() && !result.ready() && result.timedOut() && result.count() == 1 &&
         result.meanRaw() == 1300.0f && !result.rawInvalidSeen();
}

constexpr bool expireWithoutSamples() {
  CalibrationCapture result;
  result.start(10, 500);
  result.expire(2499);
  if (!result.active())
    return false;
  result.expire(2500);
  return result.timedOut() && !result.ready() && !result.active() && result.count() == 0;
}

constexpr bool cancelClears() {
  CalibrationCapture result;
  result.start(50, 0);
  result.sample(4095, 0);
  result.sample(-1, 1);
  result.expire(2000);
  result.cancel();
  result.sample(1234, 2001);
  return !result.active() && !result.ready() && !result.timedOut() && !result.railSeen() &&
         !result.rawInvalidSeen() && result.count() == 0;
}

constexpr bool restartClears() {
  CalibrationCapture result = capture(4095, 75);
  if (!result.start(2.5f, 10000) || !result.active() || result.ready() || result.count() != 0 ||
      result.railSeen() || result.timedOut() || result.rawInvalidSeen())
    return false;
  result.sample(1234, 10001);
  return result.pointMm() == 2.5f && result.count() == 1 && result.meanRaw() == 1234.0f;
}

constexpr bool timeoutRestart() {
  CalibrationCapture result;
  result.start(1, 0);
  result.expire(2000);
  return result.start(2, 2100) && result.active() && !result.timedOut() && !result.ready() &&
         result.count() == 0;
}

static_assert(!CalibrationCapture{}.active() && !CalibrationCapture{}.ready(), "Idle at boot");
static_assert(CalibrationCapture{}.count() == 0 && !CalibrationCapture{}.timedOut(),
              "No fake data");
static_assert(CalibrationCapture::SAMPLE_COUNT == 200, "One second at caller's 200 Hz rate");
static_assert(CalibrationCapture::TIMEOUT_MS == 2000, "Bounded diagnostic collection");
static_assert(capture(1234).ready() && !capture(1234).active(), "Completes exactly 200 samples");
static_assert(capture(1234).count() == 200, "Valid count");
static_assert(capture(1234).meanRaw() == 1234.0f, "Mean uses valid data only");
static_assert(capture(1234).minRaw() == 1234 && capture(1234).maxRaw() == 1234, "Constant extrema");
static_assert(capture(1234, 0).pointMm() == 0.0f, "Diagnostic point at zero allowed");
static_assert(capture(1234, 75).pointMm() == 75.0f,
              "Diagnostic point at full nominal span allowed");
static_assert(capture(4095, 0).ready() && capture(0, 75).ready(),
              "Reverse wiring not assumed invalid");
static_assert(invalidStart(-0.1f), "Negative coordinate rejected");
static_assert(invalidStart(75.1f), "Above nominal span rejected");
static_assert(invalidStart(__builtin_nanf("")), "NaN rejected and previous capture cleared");
static_assert(invalidStart(__builtin_inff()), "Infinity rejected");
static_assert(invalidStart(-__builtin_inff()), "Negative infinity rejected");
static_assert(invalidDoesNotCount(), "Invalid ADC does not contaminate mean or extrema");
static_assert(invalidHistoryRetained(), "Later valid samples cannot erase invalid ADC history");
static_assert(incomplete(), "199 samples are not complete");
static_assert(capture(0).railSeen(), "Low rail retained in diagnostic capture");
static_assert(capture(1).railSeen(), "Low near-rail matches depth conversion guard");
static_assert(capture(4094).railSeen(), "High near-rail matches depth conversion guard");
static_assert(capture(4095).railSeen(), "High rail retained in diagnostic capture");
static_assert(!capture(2).railSeen() && !capture(4093).railSeen(), "Interior ADC codes");
static_assert(capture(4095).meanRaw() == 4095.0f, "Sum cannot overflow at maximum ADC");
static_assert(fullRangeStatistics(), "Wide variation reported, not mislabeled stable");
static_assert(readyIgnoresFurtherData(), "Completion freezes sample count, flags and statistics");
static_assert(deadline(0), "Deadline wins over new ADC sample");
static_assert(deadline(UINT32_MAX - 500U), "Deadline handles unsigned millis wrap");
static_assert(capture(1234, 10, UINT32_MAX - 500U).ready(), "Normal capture across millis wrap");
static_assert(expireWithoutSamples(), "Missing data times out without synthetic samples");
static_assert(cancelClears(), "Cancel clears all flags, data and active work");
static_assert(restartClears(), "Restart after ready clears old statistics and flags");
static_assert(timeoutRestart(), "Explicit restart allowed after timeout");

int main() { return 0; }
