#include "../linear_reading.h"
#include "../linear_calibration.h"

using namespace saver_linear;

constexpr bool near(float actual, float expected) {
  return actual > expected - 0.0001f && actual < expected + 0.0001f;
}
static_assert(convert(0, 3.3f).raw_ok,
              "Real ADC zero is valid, not a missing measurement substitute");
static_assert(near(convert(0, 3.3f).adc_V, 0.0f), "Zero voltage");
static_assert(near(convert(4095, 3.3f).adc_V, 3.3f), "Full scale");
static_assert(near(convert(2048, 3.3f).adc_V, 1.650403f), "12-bit conversion");
static_assert(near(convert(2048, 3.2f).adc_V, 1.600391f), "Reference adjustment");
static_assert(!convert(-1, 3.3f).raw_ok, "Failed read");
static_assert(!convert(4096, 3.3f).raw_ok, "Out of range");
static_assert(!convert(1, 0.0f).raw_ok, "Bad reference");
static_assert(!convert(1, 5.0f).raw_ok, "Do not use sensor supply as ADC reference");
static_assert(convert(0, 3.3f).rail && convert(1, 3.3f).rail, "Lower rail diagnostic");
static_assert(convert(4094, 3.3f).rail && convert(4095, 3.3f).rail, "Upper rail diagnostic");
static_assert(!convert(2, 3.3f).rail && !convert(4093, 3.3f).rail, "Interior");
static_assert(!elapsed(99, 0, 100) && elapsed(100, 0, 100), "Period boundary");
static_assert(elapsed(20, 0xfffffff0U, 36), "millis rollover");
static_assert(matches(" READ ", "READ"), "Whitespace");
static_assert(!matches("READ NOW", "READ") && !matches("REA", "READ"), "Exact command");

constexpr bool ordinaryLine() {
  CommandLine line;
  line.feed('r', 0);
  line.feed('e', 1);
  line.feed('a', 2);
  line.feed('d', 3);
  line.feed('\r', 4);
  return line.feed('\n', 5) == LineEvent::Ready && matches(line.text(), "READ");
}
static_assert(ordinaryLine(), "Case fold and CRLF");

constexpr bool embeddedCr() {
  CommandLine line;
  line.feed('R', 0);
  line.feed('E', 0);
  line.feed('\r', 0);
  line.feed('A', 0);
  line.feed('D', 0);
  line.feed('\n', 0);
  return !matches(line.text(), "READ");
}
static_assert(embeddedCr(), "Do not concatenate tokens across CR");

constexpr bool timeout() {
  CommandLine line;
  line.feed('R', 0);
  line.feed('E', 900);
  if (line.expire(1000) != LineEvent::Timeout)
    return false;
  line.feed('A', 1001);
  line.feed('D', 1002);
  if (line.feed('\n', 1003) != LineEvent::None)
    return false;
  line.feed('R', 1004);
  return line.feed('\n', 1005) == LineEvent::Ready && matches(line.text(), "R");
}
static_assert(timeout(), "Total timeout and discard suffix until LF, then recover");

constexpr bool deadlineOnNewline() {
  CommandLine line;
  line.feed('R', 0);
  return line.feed('\n', 1000) == LineEvent::Timeout;
}
static_assert(deadlineOnNewline(), "A late terminator must not execute");

constexpr bool invalidByte() {
  CommandLine line;
  line.feed('R', 0);
  if (line.feed('\0', 1) != LineEvent::Invalid)
    return false;
  line.feed('E', 2);
  return line.feed('\n', 3) == LineEvent::None;
}
static_assert(invalidByte(), "Embedded NUL cannot truncate to executable command");

constexpr bool overflow() {
  CommandLine line;
  for (int i = 0; i < 31; ++i)
    if (line.feed('A', i) != LineEvent::None)
      return false;
  if (line.feed('A', 31) != LineEvent::Overflow)
    return false;
  return line.feed('\n', 32) == LineEvent::None;
}
static_assert(overflow(), "Buffer limit and no suffix execution");

constexpr bool resetPartial() {
  CommandLine line;
  line.feed('R', 0);
  line.reset();
  return line.feed('\n', 1) == LineEvent::None;
}
static_assert(resetPartial(), "USB reconnect discards partial line");

constexpr bool lineRollover() {
  CommandLine line;
  line.feed('R', 0xfffffff0U);
  return line.expire(983) == LineEvent::None && line.expire(984) == LineEvent::Timeout;
}
static_assert(lineRollover(), "Command timeout survives millis rollover");

constexpr bool decimal(const char *text, float expected) {
  float mm = 99;
  return parseMm(text, mm) && near(mm, expected);
}
constexpr bool badDecimal(const char *text) {
  float mm = 99;
  return !parseMm(text, mm) && mm == 99;
}
constexpr CalPoint point(int raw) { return {static_cast<float>(raw), raw - 1, raw + 1, 50}; }
constexpr bool twoPoints(int first, int second) {
  LinearCalibration cal;
  float mm = 123;
  if (cal.position(first, mm) || mm != 123 || !cal.setZero(point(first)) || cal.ready())
    return false;
  if (!cal.setSpan(point(second), 50) || !cal.ready())
    return false;
  if (!cal.position(first, mm) || !near(mm, 0))
    return false;
  if (!cal.position((first + second) / 2, mm) || !near(mm, 25))
    return false;
  return cal.position(second, mm) && near(mm, 50) && cal.within(mm) && !cal.within(51);
}
constexpr bool zeroClears() {
  LinearCalibration cal;
  return cal.setZero(point(1000)) && cal.setSpan(point(3000), 50) && cal.setZero(point(1200)) &&
         !cal.ready();
}
constexpr bool invalidSpan(float distance, CalPoint p) {
  LinearCalibration cal;
  cal.setZero(point(1000));
  cal.setSpan(point(3000), 50);
  return !cal.setSpan(p, distance) && !cal.ready() && cal.zeroKnown();
}
constexpr bool noSyntheticPosition(int raw) {
  LinearCalibration cal;
  cal.setZero(point(1000));
  cal.setSpan(point(3000), 50);
  float mm = 123;
  return !cal.position(raw, mm) && mm == 123;
}
constexpr bool captureComplete(uint32_t start) {
  PointCapture c;
  c.start(start);
  for (uint32_t i = 1; i <= PointCapture::SAMPLES; ++i) {
    if (c.add(1000 + (i % 2), start + i * 10) != (i == PointCapture::SAMPLES))
      return false;
  }
  return !c.active() && !c.error() && c.point().count == 50 && c.point().low == 1000 &&
         c.point().high == 1001 && near(c.point().mean, 1000.5f);
}
constexpr bool captureTimeout(uint32_t start) {
  PointCapture c;
  c.start(start);
  return !c.expire(start + 1999) && c.expire(start + 2000) && !c.add(1000, start + 2000) &&
         !c.active() && c.error();
}
constexpr bool capturePacing() {
  PointCapture c;
  c.start(0);
  c.add(1000, 10);
  c.add(1000, 10);
  c.add(1000, 19);
  return c.point().count == 1 && !c.due(19) && c.due(20);
}
constexpr bool captureBad(int raw) {
  PointCapture c;
  c.start(0);
  return !c.add(raw, 10) && !c.active() && c.error() && c.point().count == 0;
}
constexpr bool cancelCapture() {
  PointCapture c;
  c.start(0);
  c.add(1000, 10);
  c.cancel();
  if (c.active() || c.add(1000, 20) || c.point().count != 1)
    return false;
  c.start(30);
  return c.point().count == 0 && c.active() && !c.error();
}
constexpr bool resetCalibration() {
  LinearCalibration c;
  c.setZero(point(1000));
  c.setSpan(point(3000), 50);
  c.reset();
  return !c.ready() && !c.zeroKnown();
}
static_assert(decimal("50", 50) && decimal("25.5", 25.5f) && decimal("0", 0),
              "Numeric mm arguments");
static_assert(decimal("75", 75) && decimal(".5", .5f),
              "Parser bounds separate from CAL span minimum");
static_assert(badDecimal("75.1") && badDecimal("-1") && badDecimal("+1"),
              "Distance range and signs");
static_assert(badDecimal("NAN") && badDecimal("INF") && badDecimal("1E2"),
              "Reject special numbers");
static_assert(badDecimal("50X") && badDecimal("25 30") && badDecimal("."),
              "Whole numeric token only");
static_assert(badDecimal(nullptr) && badDecimal(""), "Missing argument");
static_assert(badDecimal("9999999999999999999999999999"), "No numeric overflow");
static_assert(twoPoints(1000, 3000), "Forward two point calibration and midpoint");
static_assert(twoPoints(3000, 1000), "Reverse ADC direction calibration");
static_assert(zeroClears(), "New ZERO invalidates previous scale");
static_assert(invalidSpan(0, point(3000)), "Zero span distance rejected");
static_assert(invalidSpan(4.99f, point(3000)), "Minimum5mm not a motion command");
static_assert(invalidSpan(75.1f, point(3000)), "Maximum nominal sensor travel75mm");
static_assert(invalidSpan(__builtin_nanf(""), point(3000)), "Nonfinite calibration rejected");
static_assert(invalidSpan(50, point(1000)), "Identical points rejected");
static_assert(invalidSpan(50, point(1029)), "Too little ADC separation rejected");
static_assert(invalidSpan(50, CalPoint{1040, 1000, 1080, 50}),
              "Overlapping collection ranges rejected");
static_assert(invalidSpan(50, CalPoint{3000, 2999, 3001, 49}), "Incomplete collection rejected");
static_assert(invalidSpan(50, CalPoint{3000, 2999, 3001, 0}), "Empty capture rejected");
static_assert(invalidSpan(50, CalPoint{3005, 2999, 3001, 50}), "Mean outside range rejected");
static_assert(noSyntheticPosition(0) && noSyntheticPosition(4095), "No mm substituted for rails");
static_assert(noSyntheticPosition(-1) && noSyntheticPosition(4096), "Invalid ADC has no position");
static_assert(noSyntheticPosition(5) && noSyntheticPosition(4090), "Five-code position guard");
static_assert(captureComplete(0), "50 independent samples mean/min/max");
static_assert(captureComplete(UINT32_MAX - 100), "Collection across millis rollover");
static_assert(captureTimeout(0), "Two second collection deadline");
static_assert(captureTimeout(UINT32_MAX - 100), "Timeout across millis rollover");
static_assert(capturePacing(), "No duplicate/catch-up samples");
static_assert(captureBad(0) && captureBad(4095), "Rail aborts collection");
static_assert(captureBad(-1) && captureBad(4096), "Invalid raw aborts collection");
static_assert(cancelCapture(), "STOP/USB cancellation and clean restart");
static_assert(resetCalibration(), "CAL_RESET removes zero and span");

int main() { return 0; }
