// Compile-time unit tests of the SAME policy included by the Teensy sketch.
// No sensor, Arduino runtime, estimator, upload, or executable run is involved.
// arm-none-eabi-g++ -std=c++14 -Wall -Wextra -Werror -fsyntax-only this_file.cpp
#include "../optical_window.h"

using saver_max_bench::OpticalWindow;
using saver_max_bench::ContactState;
using namespace saver_max_bench;

constexpr bool makeEstimate(OpticalWindow& w, uint32_t& now,
                            uint32_t red = 200000, uint32_t ir = 220000) {
  for (unsigned i = 0; i < 120; ++i, now += 40U) {
    if (w.push(red, ir, now)) {
      w.completeCalculation(80, true, 97, true); // Deliberately optimistic mock result.
      return w.hrOk() && w.spo2Ok();
    }
  }
  return false;
}

constexpr bool startup() {
  OpticalWindow w;
  w.completeCalculation(80, true, 100, true);
  return !w.contactReady() && !w.hrOk() && !w.spo2Ok() && w.count() == 0;
}
static_assert(startup(), "No values at startup, even if estimator supplies numbers");

constexpr bool uncoveredNoise() {
  OpticalWindow w;
  for (uint32_t i = 0; i < 1500; ++i) {
    if (w.push(10700 + (i * 71) % 1000, 18200 + (i * 137) % 1000, i * 40)) return false;
    if (w.hrOk() || w.spo2Ok() || w.count() != 0) return false;
  }
  return !w.contactReady();
}
static_assert(uncoveredNoise(), "60 s of no-finger noise cannot start an estimate");

constexpr bool entryNeedsBothChannels() {
  OpticalWindow w;
  for (uint32_t t = 0; t < 4000; t += 40) w.push(200000, 40000, t);
  if (w.contactReady() || w.count() != 0) return false;
  for (uint32_t t = 4000; t < 8000; t += 40) w.push(40000, 220000, t);
  return !w.contactReady() && w.count() == 0;
}
static_assert(entryNeedsBothChannels(), "One bright channel is insufficient for entry");

constexpr bool settlingAndFreshWindow() {
  OpticalWindow w;
  for (uint32_t t = 0; t < 500; t += 100) {
    if (w.push(200000, 220000, t) || w.count() != 0 || w.hrOk()) return false;
  }
  if (w.push(200000, 220000, 500) || !w.contactReady() || w.count() != 1) return false;
  for (uint32_t i = 1; i < 99; ++i) {
    if (w.push(200000 + i, 220000 + i, 500 + i * 40) || w.hrOk()) return false;
  }
  return w.push(200099, 220099, 4460) && w.count() == 100 && !w.hrOk();
}
static_assert(settlingAndFreshWindow(), "Settle 500 ms, then collect 100 entirely new samples");

constexpr bool flickeringEntry() {
  OpticalWindow w;
  for (uint32_t t = 0; t < 5000; t += 40) {
    w.push(t % 400 < 200 ? 200000 : 40000, 220000, t);
    if (w.contactReady() || w.count() != 0) return false;
  }
  return true;
}
static_assert(flickeringEntry(), "Interrupted entry cannot accumulate settling time");

constexpr bool hysteresis() {
  OpticalWindow w;
  uint32_t now = 0;
  if (!makeEstimate(w, now)) return false;
  w.push(40000, 40000, now + 40);
  if (!w.contactReady() || !w.hrOk()) return false;
  w.push(CONTACT_OFF_RAW, 220000, now + 80);
  return !w.contactReady() && !w.hrOk() && !w.spo2Ok() && w.count() == 0;
}
static_assert(hysteresis(), "Hysteresis retains contact in the middle, revokes at off threshold");

constexpr bool removal(uint32_t red, uint32_t ir) {
  OpticalWindow w;
  uint32_t now = 0;
  if (!makeEstimate(w, now)) return false;
  w.push(red, ir, now + 40);
  return !w.contactReady() && !w.hrOk() && !w.spo2Ok() && w.count() == 0;
}
static_assert(removal(1000, 1000), "First removal sample suppresses both numbers");
static_assert(removal(200000, 1000), "IR-only loss suppresses both numbers");
static_assert(removal(1000, 220000), "RED-only loss suppresses both numbers");
static_assert(removal(0, 220000), "Zero ADC sample revokes old results");
static_assert(removal(ADC_MAX, 220000), "RED rail revokes old results");
static_assert(removal(200000, ADC_MAX), "IR rail revokes old results");

constexpr bool recontactIsNewData() {
  OpticalWindow w;
  uint32_t now = 0;
  if (!makeEstimate(w, now)) return false;
  now += 40;
  w.push(1000, 1000, now);
  for (unsigned i = 0; i < 112; ++i) {
    now += 40;
    if (w.push(170000, 180000, now) || w.hrOk() || w.spo2Ok()) return false;
  }
  now += 40;
  if (!w.push(170000, 180000, now)) return false;
  for (unsigned i = 0; i < 100; ++i) {
    if (w.redData()[i] != 170000 || w.irData()[i] != 180000) return false;
  }
  w.completeCalculation(90, true, 96, true);
  return w.hrOk() && w.spo2Ok() && w.hr() == 90 && w.count() == 75;
}
static_assert(recontactIsNewData(), "Removal and recontact cannot reuse previous samples/results");

constexpr bool stopOrDisconnect() {
  OpticalWindow w;
  uint32_t now = 0;
  if (!makeEstimate(w, now)) return false;
  w.reset(); // Shared path for STOP, START, RETRY and sensor I/O failure.
  w.completeCalculation(80, true, 99, true);
  if (w.hrOk() || w.spo2Ok() || w.count() != 0) return false;
  w.push(200000, 220000, now + 40);
  return w.contactState() == ContactState::Settling && w.count() == 0;
}
static_assert(stopOrDisconnect(), "Explicit reset invalidates both contact and window");

constexpr bool staleAndWraparound(uint32_t now) {
  OpticalWindow w;
  if (!makeEstimate(w, now)) return false;
  w.expire(now + CONTACT_MAX_GAP_MS - 1);
  if (!w.hrOk()) return false;
  w.expire(now + CONTACT_MAX_GAP_MS);
  if (w.hrOk() || w.spo2Ok() || w.count() != 0 || w.contactReady()) return false;
  w.push(200000, 220000, now + CONTACT_MAX_GAP_MS + 40);
  return w.contactState() == ContactState::Settling && w.count() == 0;
}
static_assert(staleAndWraparound(0), "Expire without new data, even before I2C reconnect timeout");
static_assert(staleAndWraparound(0xFFFFFF00U), "millis rollover during entry/window is safe");
static_assert(staleAndWraparound(0xFFFFEE60U), "millis rollover during stale expiry is safe");

constexpr bool gapOnNextSample() {
  OpticalWindow w;
  uint32_t now = 0;
  if (!makeEstimate(w, now)) return false;
  w.push(200000, 220000, now + CONTACT_MAX_GAP_MS);
  return !w.hrOk() && !w.spo2Ok() && w.count() == 0 &&
         w.contactState() == ContactState::Settling;
}
static_assert(gapOnNextSample(), "Late bright sample must not revive a stale result");

constexpr bool invalidAlgorithmResults() {
  OpticalWindow w;
  uint32_t now = 0;
  if (!makeEstimate(w, now)) return false;
  for (unsigned i = 0; i < 25; ++i) { now += 40; w.push(200000, 220000, now); }
  w.completeCalculation(80, false, 97, false);
  if (w.hrOk() || w.spo2Ok()) return false;
  for (unsigned i = 0; i < 25; ++i) { now += 40; w.push(200000, 220000, now); }
  w.completeCalculation(80, true, 101, true);
  return w.hrOk() && !w.spo2Ok(); // Never clamp an out-of-range estimate to 100%.
}
static_assert(invalidAlgorithmResults(), "Algorithm failure cannot keep previous valid numbers");

constexpr bool rollingWindow() {
  OpticalWindow w;
  for (uint32_t t = 0; t < 500; t += 100) w.push(200000, 220000, t);
  for (uint32_t i = 0; i < 100; ++i) w.push(200000 + i, 220000 + i, 500 + i * 40);
  w.completeCalculation(80, true, 97, true);
  if (w.count() != 75 || w.redData()[0] != 200025 || w.irData()[74] != 220099) return false;
  for (uint32_t i = 0; i < 24; ++i) {
    if (w.push(201000 + i, 221000 + i, 4500 + i * 40)) return false;
  }
  return w.push(201024, 221024, 5460) && w.count() == 100 && w.redData()[99] == 201024;
}
static_assert(rollingWindow(), "Normal overlap retains 75 samples and requests calculation every 25");

constexpr bool skippedCalculationIsBounded() {
  OpticalWindow w;
  for (uint32_t t = 0; t < 10000; t += 40) w.push(200000, 220000, t);
  return w.count() <= 100 && !w.hrOk() && !w.spo2Ok();
}
static_assert(skippedCalculationIsBounded(), "Missing estimator call cannot overrun buffers");
