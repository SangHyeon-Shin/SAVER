#include "../ppg_diagnostics.h"
using namespace saver_max_bench;

constexpr bool led(const char *s, bool expected, uint8_t value = 60) {
  uint8_t parsed = 60;
  const bool ok = parseLedCode(s, parsed);
  return ok == expected && parsed == value;
}
static_assert(led("1", true, 1) && led("255", true, 255), "LED range inclusive");
static_assert(led("0", false) && led("256", false), "LED range bounded");
static_assert(led("-1", false) && led("+1", false), "No signed LED codes");
static_assert(led("1.5", false) && led("60x", false), "No fractional/suffix LED codes");
static_assert(led("", false) && led(nullptr, false), "No empty LED codes");
static_assert(led("99999999999999999999999", false), "No integer overflow");

constexpr OpticalCalibration captured(uint32_t openRed, uint32_t openIr, uint32_t fingerRed,
                                      uint32_t fingerIr, uint32_t start = 0) {
  OpticalCalibration cal;
  cal.start(CapturePhase::Open, start);
  for (uint32_t n = 0; n < CAPTURE_SAMPLES; ++n)
    cal.push(openRed, openIr, start + n * 40);
  cal.start(CapturePhase::Finger, start + 11000);
  for (uint32_t n = 0; n < CAPTURE_SAMPLES; ++n)
    cal.push(fingerRed, fingerIr, start + 11000 + n * 40);
  return cal;
}
constexpr auto valid = captured(10000, 18000, 200000, 220000);
static_assert(valid.open().complete && valid.finger().complete, "Two full captures");
static_assert(valid.proposalOk() && valid.proposedOff() > 18000 &&
                  valid.proposedOn() > valid.proposedOff() && valid.proposedOn() < 200000,
              "Hysteresis is strictly inside observed separation");
static_assert(valid.finger().ir.sum == 55000000 && valid.finger().ir.mean() == 220000,
              "Raw capture statistics");
static_assert(!captured(100, 250000, 200000, 220000).proposalOk(), "Both open channels considered");
static_assert(!captured(100, 200, 150, 50000).proposalOk(), "Both finger channels considered");
static_assert(!captured(100, 200, 200, 300).proposalOk(), "Overlap rejected");
static_assert(!captured(100, 200, 202, 300).proposalOk(), "Insufficient integer hysteresis gap");
static_assert(captured(100, 200, 203, 300).proposedOff() == 201 &&
                  captured(100, 200, 203, 300).proposedOn() == 202,
              "Smallest valid gap");
static_assert(!captured(100, 200, ADC_MAX, 220000).proposalOk(), "Red rail rejected");
static_assert(!captured(100, 200, 200000, ADC_MAX).proposalOk(), "IR rail rejected");
static_assert(!captured(100, 200, 0, 220000).proposalOk(), "Finger zero rejected");
static_assert(captured(0, 0, 100000, 110000).proposalOk(), "Uncovered zero is permissible");
static_assert(captured(100, 200, 100000, 110000, 0xFFFFF000U).proposalOk(), "Capture clock wrap");

constexpr bool incompleteAndCancel() {
  OpticalCalibration cal;
  if (cal.start(CapturePhase::None, 0) || cal.proposalOk())
    return false;
  cal.start(CapturePhase::Open, 0);
  for (unsigned n = 0; n < 249; ++n)
    cal.push(100, 200, n * 40);
  if (cal.open().complete || !cal.active() || cal.start(CapturePhase::Finger, 10000))
    return false;
  cal.cancel();
  return !cal.active() && !cal.open().complete && !cal.proposalOk();
}
static_assert(incompleteAndCancel(),
              "Only 250 fresh samples complete, cancel cannot promote partial data");
constexpr bool gapTimeout() {
  OpticalCalibration cal;
  cal.start(CapturePhase::Open, 0);
  if (cal.expire(14999) != CaptureEvent::None || cal.expire(15000) != CaptureEvent::Timeout)
    return false;
  cal.start(CapturePhase::Open, 0);
  cal.push(10, 20, 40);
  if (cal.expire(239) != CaptureEvent::None || cal.push(10, 20, 240) != CaptureEvent::Gap)
    return false;
  return !cal.active() && !cal.open().complete && cal.open().red.count == 1;
}
static_assert(gapTimeout(), "Idle timeout / late sample must invalidate a capture");
constexpr bool resetPair() {
  auto cal = valid;
  cal.start(CapturePhase::Open, 25000);
  if (cal.finger().complete || cal.open().complete || cal.proposalOk())
    return false;
  cal.reset();
  return !cal.active() && !cal.proposalOk() && cal.open().red.count == 0;
}
static_assert(resetPair(), "New open capture and LED/reset cannot reuse an old pair");

constexpr bool contactConfig() {
  OpticalWindow window;
  if (window.configureContact(1, 1) || window.configureContact(ADC_MAX, 10))
    return false;
  if (window.onRaw() != CONTACT_ON_RAW || !window.configureContact(200, 100))
    return false;
  for (uint32_t n = 0; n < 120; ++n) {
    if (window.push(1000, 1200, n * 40))
      window.completeCalculation(80, true, 97, true);
  }
  if (!window.hrOk())
    return false;
  window.configureContact(300, 150);
  if (window.hrOk() || window.count())
    return false;
  window.reset();
  return window.onRaw() == 300 && window.offRaw() == 150;
}
static_assert(contactConfig(),
              "Threshold changes clear old estimates; reset retains selected thresholds");

constexpr uint32_t red[] = {10, 20, 30, 40};
constexpr uint32_t ir[] = {100, 120, 140, 160};
constexpr uint32_t flat[] = {50, 50, 50, 50};
constexpr uint32_t anti[] = {160, 140, 120, 100};
constexpr auto metrics = measureWindow(red, ir, 4);
static_assert(metrics.redMean == 25 && metrics.irMean == 130 && metrics.redPp == 30 &&
                  metrics.irPp == 60,
              "DC and peak-to-peak");
static_assert(metrics.redVariance == 125 && metrics.irVariance == 500 &&
                  metrics.covariance == 250 && metrics.correlationOk,
              "Centered variance and covariance");
static_assert(measureWindow(red, anti, 4).covariance == -250, "Negative covariance retained");
static_assert(!measureWindow(red, flat, 4).correlationOk && measureWindow(flat, flat, 4).redPp == 0,
              "Flat channel invalid correlation, not zero correlation");
static_assert(!measureWindow(nullptr, nullptr, 0).correlationOk, "Empty metrics invalid");

constexpr bool lineTimeout() {
  CommandLine line;
  line.push('L', 0);
  line.push('E', 1);
  if (line.expire(1000) != LineEvent::None || line.expire(1001) != LineEvent::Timeout)
    return false;
  line.push('D', 1002);
  line.push(' ', 1003);
  line.push('1', 1004);
  if (line.push('\n', 1005) != LineEvent::Invalid)
    return false;
  line.push('H', 1010);
  return line.push('\n', 1011) == LineEvent::Ready;
}
static_assert(lineTimeout(), "Expired suffix discarded through newline, next command usable");
constexpr bool lineOverflow() {
  CommandLine line;
  for (unsigned n = 0; n < 60; ++n)
    line.push('A', n);
  return line.push('\n', 70) == LineEvent::Invalid;
}
static_assert(lineOverflow(), "Oversized input rejected");
constexpr bool lineInvalid() {
  CommandLine line;
  line.push('A', 0);
  line.push('\0', 1);
  line.push('B', 2);
  if (line.push('\n', 3) != LineEvent::Invalid)
    return false;
  line.push('L', 4);
  line.reset();
  return line.push('\n', 5) == LineEvent::None;
}
static_assert(lineInvalid(),
              "Embedded control bytes invalid; session reset discards partial command");
constexpr bool lineWrap() {
  CommandLine line;
  line.push('L', 0xFFFFFF00U);
  return line.expire(0x000002E8U) == LineEvent::Timeout;
}
static_assert(lineWrap(), "Command timeout survives millis wrap");
