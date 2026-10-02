#ifndef SAVER_PPG_DIAGNOSTICS_H
#define SAVER_PPG_DIAGNOSTICS_H

#include "optical_window.h"
#include <stddef.h>
#include <stdint.h>

namespace saver_max_bench {
// Engineering acquisition limits, not medical thresholds. 250 / 25 Hz = 10 s.
constexpr uint16_t CAPTURE_SAMPLES = 250;
constexpr uint32_t CAPTURE_TIMEOUT_MS = 15000;

struct ChannelStats {
  uint16_t count = 0;
  uint16_t zeroCount = 0, highCount = 0;
  uint32_t minimum = ADC_MAX, maximum = 0;
  uint64_t sum = 0;
  constexpr void push(uint32_t raw) {
    ++count;
    if (raw == 0)
      ++zeroCount;
    if (raw >= ADC_MAX)
      ++highCount;
    if (raw < minimum)
      minimum = raw;
    if (raw > maximum)
      maximum = raw;
    sum += raw;
  }
  constexpr double mean() const { return count ? double(sum) / count : 0.0; }
};

struct CaptureStats {
  ChannelStats red, ir;
  bool complete = false;
};
enum class CapturePhase : uint8_t { None, Open, Finger };
enum class CaptureEvent : uint8_t { None, Complete, Gap, Timeout };

class OpticalCalibration {
public:
  constexpr void reset() { *this = OpticalCalibration{}; }
  constexpr void cancel() { phase_ = CapturePhase::None; }
  constexpr bool start(CapturePhase phase, uint32_t now) {
    if (active() || phase == CapturePhase::None)
      return false;
    phase_ = phase;
    if (phase == CapturePhase::Open) {
      open_ = CaptureStats{};
      // A new open measurement starts a new pair; never mix old finger data.
      finger_ = CaptureStats{};
    } else {
      finger_ = CaptureStats{};
    }
    startedMs_ = lastMs_ = now;
    seen_ = false;
    return true;
  }
  constexpr CaptureEvent expire(uint32_t now) {
    if (!active())
      return CaptureEvent::None;
    if (uint32_t(now - startedMs_) >= CAPTURE_TIMEOUT_MS) {
      cancel();
      return CaptureEvent::Timeout;
    }
    if (seen_ && uint32_t(now - lastMs_) >= CONTACT_MAX_GAP_MS) {
      cancel();
      return CaptureEvent::Gap;
    }
    return CaptureEvent::None;
  }
  // Call exactly once per newly read FIFO pair, NOT once per printed DATA row.
  constexpr CaptureEvent push(uint32_t red, uint32_t ir, uint32_t now) {
    const CaptureEvent expired = expire(now);
    if (expired != CaptureEvent::None || !active())
      return expired;
    seen_ = true;
    lastMs_ = now;
    CaptureStats &stats = phase_ == CapturePhase::Open ? open_ : finger_;
    stats.red.push(red);
    stats.ir.push(ir);
    if (stats.red.count == CAPTURE_SAMPLES) {
      stats.complete = true;
      cancel();
      return CaptureEvent::Complete;
    }
    return CaptureEvent::None;
  }
  constexpr bool active() const { return phase_ != CapturePhase::None; }
  constexpr CapturePhase phase() const { return phase_; }
  constexpr const CaptureStats &open() const { return open_; }
  constexpr const CaptureStats &finger() const { return finger_; }
  constexpr uint32_t openMax() const {
    return open_.red.maximum > open_.ir.maximum ? open_.red.maximum : open_.ir.maximum;
  }
  constexpr uint32_t fingerMin() const {
    return finger_.red.minimum < finger_.ir.minimum ? finger_.red.minimum : finger_.ir.minimum;
  }
  constexpr bool proposalOk() const {
    return !active() && open_.complete && finger_.complete && open_.red.highCount == 0 &&
           open_.ir.highCount == 0 && finger_.red.highCount == 0 && finger_.ir.highCount == 0 &&
           finger_.red.zeroCount == 0 && finger_.ir.zeroCount == 0 && fingerMin() > openMax() &&
           fingerMin() - openMax() >= 3U;
  }
  // Split the observed empty interval into thirds to provide hysteresis.
  // This is only an optical separation heuristic; a reflector may also pass.
  constexpr uint32_t proposedOff() const {
    return proposalOk() ? openMax() + (fingerMin() - openMax()) / 3U : 0;
  }
  constexpr uint32_t proposedOn() const {
    return proposalOk() ? openMax() + 2U * (fingerMin() - openMax()) / 3U : 0;
  }

private:
  CaptureStats open_, finger_;
  CapturePhase phase_ = CapturePhase::None;
  uint32_t startedMs_ = 0, lastMs_ = 0;
  bool seen_ = false;
};

// Strict unsigned decimal parsing: no sign, fractional value, suffix or wrap.
constexpr bool parseLedCode(const char *text, uint8_t &result) {
  if (!text || !*text)
    return false;
  uint32_t value = 0;
  for (size_t i = 0; text[i]; ++i) {
    if (text[i] < '0' || text[i] > '9')
      return false;
    value = value * 10U + unsigned(text[i] - '0');
    if (value > 255)
      return false;
  }
  if (value == 0)
    return false;
  result = uint8_t(value);
  return true;
}

enum class LineEvent : uint8_t { None, Ready, Invalid, Timeout };
class CommandLine {
public:
  constexpr void reset() {
    length_ = 0;
    discard_ = false;
  }
  constexpr LineEvent expire(uint32_t now) {
    if (length_ && !discard_ && uint32_t(now - lastMs_) >= 1000U) {
      discard_ = true; // Discard all suffixes until newline after a timeout.
      return LineEvent::Timeout;
    }
    return LineEvent::None;
  }
  constexpr LineEvent push(char c, uint32_t now) {
    // Caller also polls expire while idle; this prevents an expired suffix.
    expire(now);
    lastMs_ = now;
    if (c == '\n') {
      const LineEvent event = discard_  ? LineEvent::Invalid
                              : length_ ? LineEvent::Ready
                                        : LineEvent::None;
      text_[length_] = '\0';
      length_ = 0;
      discard_ = false;
      return event;
    }
    if (c == '\r' || c == '\t')
      c = ' '; // Never concatenate across a CR.
    if (c < 32 || c > 126 || length_ >= sizeof text_ - 1)
      discard_ = true;
    if (!discard_)
      text_[length_++] = (c >= 'a' && c <= 'z') ? char(c - 'a' + 'A') : c;
    return LineEvent::None;
  }
  char *text() { return text_; }

private:
  char text_[48] = {};
  size_t length_ = 0;
  uint32_t lastMs_ = 0;
  bool discard_ = false;
};

struct WindowMetrics {
  double redMean = 0, irMean = 0;
  double redVariance = 0, irVariance = 0, covariance = 0;
  uint32_t redPp = 0, irPp = 0;
  bool correlationOk = false;
};
// DC-removed population variance/covariance; no pass/fail medical thresholds.
constexpr WindowMetrics measureWindow(const uint32_t *red, const uint32_t *ir, uint16_t count) {
  WindowMetrics m;
  if (!count)
    return m;
  ChannelStats r, i;
  for (uint16_t n = 0; n < count; ++n) {
    r.push(red[n]);
    i.push(ir[n]);
  }
  m.redMean = r.mean();
  m.irMean = i.mean();
  m.redPp = r.maximum - r.minimum;
  m.irPp = i.maximum - i.minimum;
  for (uint16_t n = 0; n < count; ++n) {
    const double dr = red[n] - m.redMean, di = ir[n] - m.irMean;
    m.redVariance += dr * dr;
    m.irVariance += di * di;
    m.covariance += dr * di;
  }
  m.redVariance /= count;
  m.irVariance /= count;
  m.covariance /= count;
  m.correlationOk = m.redVariance > 0 && m.irVariance > 0;
  return m;
}
} // namespace saver_max_bench
#endif
