#ifndef SAVER_PPG_SIGNAL_H
#define SAVER_PPG_SIGNAL_H

#include "optical_window.h"
#include <stddef.h>
#include <stdint.h>

namespace saver_max_bench {

// Provisional BENCH signal-processing heuristics, NOT medical limits/calibration.
// The window must contain >=3 repeats; its finite length limits detectable periods.
// No physiological BPM range, previous-value smoothing, or SpO2 offset is applied.
constexpr unsigned FIFO_SAMPLE_HZ = 25;
constexpr unsigned BASELINE_RADIUS = FIFO_SAMPLE_HZ; // Centered 51-point local mean.
constexpr unsigned FILTER_EDGE = 4;                 // Ignore smoothing end effects.
constexpr double MIN_AC_VARIANCE = 1.0;             // ADC-code quantization floor.
constexpr double MIN_PERIOD_CORR2 = 0.36;           // Positive correlation >=0.6, provisional.
constexpr double MIN_CHANNEL_CORR2 = 0.64;
constexpr double PERIOD_REL_TOL = 0.10;             // Sampling/shape agreement, not BPM limit.
constexpr unsigned CONFIRM_WINDOWS = 2;
static_assert(WINDOW_SAMPLES == 200 && STEP_SAMPLES == FIFO_SAMPLE_HZ,
              "Bench pulse estimator expects eight seconds, advanced by one second");

enum class SignalReason : uint8_t {
  Waiting, BadInput, Flat, NoPeriod, WeakPeriod, ChannelMismatch, UnstablePeriod, Periodic
};

constexpr const char *signalReasonName(SignalReason reason) {
  return reason == SignalReason::BadInput         ? "BAD_INPUT"
         : reason == SignalReason::Flat          ? "FLAT_OR_QUANTIZED"
         : reason == SignalReason::NoPeriod      ? "NO_REPEATABLE_PERIOD"
         : reason == SignalReason::WeakPeriod    ? "WEAK_PERIOD"
         : reason == SignalReason::ChannelMismatch ? "CHANNEL_MISMATCH"
         : reason == SignalReason::UnstablePeriod  ? "UNSTABLE_PERIOD"
         : reason == SignalReason::Periodic       ? "PERIODIC_CANDIDATE"
                                                   : "WAITING_WINDOW";
}

constexpr double absolute(double v) { return v < 0 ? -v : v; }
constexpr bool periodsAgree(double a, double b) {
  return a > 0 && b > 0 && absolute(a - b) <= 1.0 + PERIOD_REL_TOL * (a < b ? a : b);
}

struct Correlation {
  bool available = false;
  double squaredSigned = 0; // Sign(covariance) * correlation^2, NOT a clinical score.
};

// Center each overlapping portion independently, so a DC offset is not periodicity.
constexpr Correlation correlate(const double *a, const double *b, unsigned count) {
  if (!a || !b || count < 2)
    return {};
  double sa = 0, sb = 0, aa = 0, bb = 0, ab = 0;
  for (unsigned i = 0; i < count; ++i) {
    sa += a[i];
    sb += b[i];
    aa += a[i] * a[i];
    bb += b[i] * b[i];
    ab += a[i] * b[i];
  }
  const double va = aa - sa * sa / count, vb = bb - sb * sb / count;
  if (va / count < MIN_AC_VARIANCE || vb / count < MIN_AC_VARIANCE)
    return {};
  const double cov = ab - sa * sb / count;
  const double c2 = cov * cov / (va * vb);
  return {true, cov < 0 ? -c2 : c2};
}

// Linear detrend -> centered local-baseline subtraction -> three-point smoothing.
// Raw arrays are never changed. All work/buffers are bounded; no heap or I/O.
constexpr void filterChannel(const uint32_t *raw, double *out) {
  double detrended[WINDOW_SAMPLES] = {}, highpass[WINDOW_SAMPLES] = {};
  double mean = 0, slopeNumerator = 0, slopeDenominator = 0;
  const double center = (WINDOW_SAMPLES - 1) / 2.0;
  for (unsigned i = 0; i < WINDOW_SAMPLES; ++i) {
    mean += raw[i];
    const double x = i - center;
    slopeNumerator += x * raw[i];
    slopeDenominator += x * x;
  }
  mean /= WINDOW_SAMPLES;
  const double slope = slopeNumerator / slopeDenominator;
  for (unsigned i = 0; i < WINDOW_SAMPLES; ++i)
    detrended[i] = raw[i] - mean - slope * (i - center);
  for (unsigned i = 0; i < WINDOW_SAMPLES; ++i) {
    const unsigned lo = i > BASELINE_RADIUS ? i - BASELINE_RADIUS : 0;
    const unsigned hi = i + BASELINE_RADIUS + 1 < WINDOW_SAMPLES
                            ? i + BASELINE_RADIUS + 1 : WINDOW_SAMPLES;
    double baseline = 0;
    for (unsigned j = lo; j < hi; ++j)
      baseline += detrended[j];
    highpass[i] = detrended[i] - baseline / (hi - lo);
  }
  for (unsigned i = 0; i < WINDOW_SAMPLES; ++i) {
    const unsigned lo = i ? i - 1 : 0;
    const unsigned hi = i + 2 < WINDOW_SAMPLES ? i + 2 : WINDOW_SAMPLES;
    double sum = 0;
    for (unsigned j = lo; j < hi; ++j)
      sum += highpass[j];
    out[i] = sum / (hi - lo);
  }
}

struct Period {
  bool found = false;
  double samples = 0, corr2 = 0;
};

// First positive autocorrelation maximum AFTER a nonpositive trough. Do not skip
// a weak first period to accept a stronger multiple (which can halve apparent HR).
constexpr Period findPeriod(const double *ac, unsigned count, unsigned minRepeats) {
  if (!ac || count <= 2 * FILTER_EDGE + 6 || minRepeats < 2)
    return {};
  ac += FILTER_EDGE;
  count -= 2 * FILTER_EDGE;
  const unsigned maxLag = count / minRepeats;
  double previous = 1.0, beforePrevious = 1.0;
  bool crossedZero = false;
  for (unsigned lag = 1; lag <= maxLag + 1; ++lag) {
    const auto c = correlate(ac, ac + lag, count - lag);
    if (!c.available)
      return {};
    const double current = c.squaredSigned;
    if (crossedZero && previous > 0 && previous > beforePrevious && previous >= current) {
      const double curvature = beforePrevious - 2 * previous + current;
      const double fraction = curvature < 0 ? 0.5 * (beforePrevious - current) / curvature : 0;
      return {true, double(lag - 1) + (absolute(fraction) <= 0.5 ? fraction : 0), previous};
    }
    if (current <= 0)
      crossedZero = true;
    beforePrevious = previous;
    previous = current;
  }
  return {};
}

struct PulseQuality {
  bool accepted = false;
  SignalReason reason = SignalReason::Waiting;
  Period red, ir;
  Correlation channels;
  int32_t hrBpm = 0; // Only expose when accepted AND confirmation has passed.
};

constexpr PulseQuality inspectPulse(const uint32_t *red, const uint32_t *ir, unsigned count) {
  PulseQuality q;
  if (!red || !ir || count != WINDOW_SAMPLES) {
    q.reason = SignalReason::BadInput;
    return q;
  }
  for (unsigned i = 0; i < count; ++i) {
    if (red[i] == 0 || ir[i] == 0 || red[i] >= ADC_MAX || ir[i] >= ADC_MAX) {
      q.reason = SignalReason::BadInput;
      return q;
    }
  }
  double redAc[WINDOW_SAMPLES] = {}, irAc[WINDOW_SAMPLES] = {};
  filterChannel(red, redAc);
  filterChannel(ir, irAc);
  q.channels = correlate(redAc + FILTER_EDGE, irAc + FILTER_EDGE, count - 2 * FILTER_EDGE);
  if (!q.channels.available) {
    q.reason = SignalReason::Flat;
    return q;
  }
  q.red = findPeriod(redAc, count, 3);
  q.ir = findPeriod(irAc, count, 3);
  if (!q.red.found || !q.ir.found) {
    q.reason = SignalReason::NoPeriod;
    return q;
  }
  if (q.red.corr2 < MIN_PERIOD_CORR2 || q.ir.corr2 < MIN_PERIOD_CORR2) {
    q.reason = SignalReason::WeakPeriod;
    return q;
  }
  if (q.channels.squaredSigned < MIN_CHANNEL_CORR2 ||
      !periodsAgree(q.red.samples, q.ir.samples)) {
    q.reason = SignalReason::ChannelMismatch;
    return q;
  }
  // Independent first/second-half periods must agree with the whole-window result.
  for (unsigned half = 0; half < 2; ++half) {
    const auto rp = findPeriod(redAc + half * (count / 2), count / 2, 2);
    const auto ip = findPeriod(irAc + half * (count / 2), count / 2, 2);
    if (!rp.found || !ip.found || rp.corr2 < MIN_PERIOD_CORR2 || ip.corr2 < MIN_PERIOD_CORR2 ||
        !periodsAgree(rp.samples, q.red.samples) || !periodsAgree(ip.samples, q.ir.samples)) {
      q.reason = SignalReason::UnstablePeriod;
      return q;
    }
  }
  q.hrBpm = int32_t(FIFO_SAMPLE_HZ * 60.0 / q.ir.samples + 0.5);
  q.accepted = true;
  q.reason = SignalReason::Periodic;
  return q;
}

class PulseConfirmation {
public:
  constexpr void reset() { count_ = 0; previousPeriod_ = 0; }
  constexpr bool accept(const PulseQuality &q) {
    if (!q.accepted) {
      reset();
      return false;
    }
    if (!periodsAgree(q.ir.samples, previousPeriod_))
      count_ = 0;
    previousPeriod_ = q.ir.samples;
    if (count_ < CONFIRM_WINDOWS)
      ++count_;
    return count_ == CONFIRM_WINDOWS;
  }
  constexpr unsigned count() const { return count_; }

private:
  unsigned count_ = 0;
  double previousPeriod_ = 0;
};

// Maxim library uses 32-bit signed AC*DC products. Preserve ratios with a COMMON
// power-of-two scale on private four-second copies; never change raw/logged data.
// This is arithmetic protection, not a new SpO2 calibration or accuracy claim.
constexpr unsigned prepareLegacyWindow(const uint32_t *red, const uint32_t *ir,
                                       uint32_t *redOut, uint32_t *irOut, unsigned count) {
  if (!red || !ir || !redOut || !irOut || count != WINDOW_SAMPLES / 2)
    return 32;
  const unsigned offset = WINDOW_SAMPLES - count;
  uint32_t rmin = ADC_MAX, imin = ADC_MAX, rmax = 0, imax = 0;
  for (unsigned i = 0; i < count; ++i) {
    const uint32_t r = red[offset + i], v = ir[offset + i];
    if (r < rmin) rmin = r;
    if (v < imin) imin = v;
    if (r > rmax) rmax = r;
    if (v > imax) imax = v;
  }
  const uint32_t maxDc = rmax > imax ? rmax : imax;
  const uint32_t maxAc = rmax - rmin > imax - imin ? rmax - rmin : imax - imin;
  unsigned shift = 0;
  while (2ULL * (maxDc >> shift) * ((maxAc >> shift) + 1U) > 2147483647ULL)
    ++shift;
  for (unsigned i = 0; i < count; ++i) {
    redOut[i] = red[offset + i] >> shift;
    irOut[i] = ir[offset + i] >> shift;
  }
  return shift;
}

constexpr bool legacyPeriodAgrees(int32_t legacyHr, const PulseQuality &q) {
  return q.accepted && legacyHr > 0 &&
         periodsAgree(FIFO_SAMPLE_HZ * 60.0 / legacyHr, q.ir.samples);
}

} // namespace saver_max_bench
#endif
