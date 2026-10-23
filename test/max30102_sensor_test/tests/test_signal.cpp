#include "../ppg_signal.h"
using namespace saver_max_bench;

struct Fixture {
  uint32_t red[WINDOW_SAMPLES] = {}, ir[WINDOW_SAMPLES] = {};
};
constexpr int triangle(unsigned i, unsigned period) {
  const unsigned phase = i % period;
  return phase < period / 2 ? int(phase * 4000 / (period / 2)) - 2000
                            : 2000 - int((phase - period / 2) * 4000 / (period - period / 2));
}
constexpr Fixture wave(unsigned period, bool drift = false, bool anti = false) {
  Fixture f;
  for (unsigned i = 0; i < WINDOW_SAMPLES; ++i) {
    const int v = triangle(i, period), ramp = drift ? int(i * 75) : 0;
    f.red[i] = uint32_t(190000 + v + ramp);
    f.ir[i] = uint32_t(210000 + (anti ? -2 * v : 2 * v) + ramp);
  }
  return f;
}
constexpr PulseQuality inspect(Fixture f) {
  return inspectPulse(f.red, f.ir, WINDOW_SAMPLES);
}
constexpr auto normal = wave(20);
constexpr auto normalQuality = inspect(normal);
static_assert(normalQuality.accepted && normalQuality.hrBpm >= 74 && normalQuality.hrBpm <= 76,
              "Synthetic 25Hz / 20-sample waveform estimates 75 BPM, not medical evidence");
constexpr auto slow = inspect(wave(25));
static_assert(slow.accepted && slow.hrBpm >= 59 && slow.hrBpm <= 61, "Synthetic 60 BPM");
constexpr auto fast = inspect(wave(10));
static_assert(fast.accepted && fast.hrBpm >= 148 && fast.hrBpm <= 152, "Synthetic 150 BPM not clamped");
constexpr auto drifting = inspect(wave(20, true));
static_assert(drifting.accepted && drifting.hrBpm >= 74 && drifting.hrBpm <= 76,
              "Linear baseline drift must not become pulse peaks");
static_assert(!inspect(wave(20, false, true)).accepted, "Anticorrelated channels rejected");

constexpr Fixture flatOrRamp(bool ramp) {
  Fixture f;
  for (unsigned i = 0; i < WINDOW_SAMPLES; ++i) {
    f.red[i] = 190000 + (ramp ? i * 100 : 0);
    f.ir[i] = 210000 + (ramp ? i * 200 : 0);
  }
  return f;
}
static_assert(inspect(flatOrRamp(false)).reason == SignalReason::Flat, "Flat reflection not a pulse");
static_assert(inspect(flatOrRamp(true)).reason == SignalReason::Flat, "Ramp-only signal rejected");
static_assert(!inspectPulse(nullptr, nullptr, WINDOW_SAMPLES).accepted, "Null inputs bounded");
static_assert(!inspectPulse(normal.red, normal.ir, 100).accepted, "Incomplete window rejected");
constexpr bool rail(bool zero) {
  auto f = normal;
  f.ir[60] = zero ? 0 : ADC_MAX;
  return inspect(f).reason == SignalReason::BadInput;
}
static_assert(rail(true) && rail(false), "Zero and saturation rejected");

constexpr bool noise() {
  Fixture f;
  uint32_t state = 12345;
  for (unsigned i = 0; i < WINDOW_SAMPLES; ++i) {
    state = state * 1664525U + 1013904223U;
    f.red[i] = 190000 + (state >> 16) % 4000;
    state = state * 1664525U + 1013904223U;
    f.ir[i] = 210000 + (state >> 16) % 4000;
  }
  return !inspect(f).accepted;
}
static_assert(noise(), "Independent synthetic noise rejected");
constexpr bool changedPeriod() {
  auto f = normal;
  for (unsigned i = 100; i < WINDOW_SAMPLES; ++i) {
    const int v = triangle(i, 30);
    f.red[i] = uint32_t(190000 + v);
    f.ir[i] = uint32_t(210000 + 2 * v);
  }
  return !inspect(f).accepted;
}
static_assert(changedPeriod(), "Window with conflicting periods rejected");
static_assert(!inspect(wave(90)).accepted, "Too few repeats in finite window rejected, not BPM clamped");

constexpr bool confirmation() {
  PulseConfirmation c;
  if (c.accept(normalQuality) || c.count() != 1 || !c.accept(normalQuality))
    return false;
  auto bad = normalQuality;
  bad.accepted = false;
  if (c.accept(bad) || c.count())
    return false;
  return !c.accept(normalQuality) && c.accept(normalQuality);
}
static_assert(confirmation(), "Two consistent windows, failure immediately resets confirmation");
constexpr bool changedAndReset() {
  PulseConfirmation c;
  c.accept(normalQuality);
  c.accept(normalQuality);
  auto other = normalQuality;
  other.ir.samples = 30;
  if (c.accept(other) || c.count() != 1)
    return false;
  c.reset(); // Removal, STOP, reconnect, CAL/LED changes use this reset.
  return c.count() == 0 && !c.accept(normalQuality);
}
static_assert(changedAndReset(), "Period jump/session reset cannot reuse previous confirmation");
static_assert(legacyPeriodAgrees(75, normalQuality) && !legacyPeriodAgrees(150, normalQuality) &&
                  !legacyPeriodAgrees(0, normalQuality),
              "Wrong/doubled library period cannot authorize SpO2");

constexpr bool privateCopiesAndOverflow() {
  uint32_t redCopy[100] = {}, irCopy[100] = {};
  auto f = wave(20, true);
  const auto before = f;
  const unsigned shift = prepareLegacyWindow(f.red, f.ir, redCopy, irCopy, 100);
  if (shift >= 32)
    return false;
  for (unsigned i = 0; i < WINDOW_SAMPLES; ++i)
    if (f.red[i] != before.red[i] || f.ir[i] != before.ir[i])
      return false;
  for (unsigned i = 0; i < 100; ++i)
    if (redCopy[i] != (f.red[i + 100] >> shift) || irCopy[i] != (f.ir[i + 100] >> shift))
      return false;
  return shift > 0;
}
static_assert(privateCopiesAndOverflow(), "Common scale avoids large products without mutating raw data");
static_assert(prepareLegacyWindow(nullptr, nullptr, nullptr, nullptr, 100) == 32,
              "Legacy copy rejects null inputs");
static_assert(!correlate(nullptr, nullptr, 0).available, "Unavailable correlation is not fake zero");
