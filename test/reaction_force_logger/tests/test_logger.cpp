#include "../logger_logic.h"
#include "../nau7802_reader.h"
using namespace reaction;
constexpr bool near(double a, double b) { return a > b - 0.00001 && a < b + 0.00001; }
constexpr bool same(const char *a, const char *b) {
  for (; *a && *b; ++a, ++b)
    if (*a != *b)
      return false;
  return *a == *b;
}
constexpr bool calibrated(int sign) {
  Calibration c;
  double result = 999;
  return c.tare(1000) && c.calibrate(1000 + sign * 20000, 2) &&
         c.force(1000 + sign * 10000, result) && near(result, STANDARD_G);
}
constexpr bool uncalibratedPreservesOutput() {
  Calibration c;
  double n = 123;
  return !c.force(0, n) && n == 123 && c.tare(123) && !c.force(123, n) && n == 123;
}
constexpr bool invalidCalibration(int test) {
  Calibration c;
  c.tare(1000);
  return test == 0   ? !c.calibrate(1000, 1)
         : test == 1 ? !c.calibrate(2000, 0)
         : test == 2 ? !c.calibrate(2000, 101)
         : test == 3 ? !c.calibrate(RAW_MAX, 1)
                     : !c.calibrate(2000, -1);
}
constexpr bool retareInvalidates() {
  Calibration c;
  c.tare(1);
  c.calibrate(1001, 1);
  c.tare(5);
  return c.zeroKnown && !c.confirmed && c.countsPerKg == 0;
}
constexpr bool negativeForce() {
  Calibration c;
  c.tare(1000);
  c.calibrate(2000, 1);
  double f = 0;
  return c.force(0, f) && near(f, -STANDARD_G);
}
constexpr bool railsInvalid() {
  Calibration c;
  c.tare(0);
  c.calibrate(1000, 1);
  double f = 23;
  return !c.force(RAW_MIN, f) && !c.force(RAW_MAX, f) && f == 23;
}
constexpr Window makeWindow(bool validCal) {
  Calibration c;
  if (validCal) {
    c.tare(0);
    c.calibrate(1000, 1);
  }
  Window w;
  w.add(1000, c);
  w.add(-1000, c);
  w.add(RAW_MAX, c);
  return w;
}
constexpr bool queueWrap() {
  ByteQueue<5> q;
  char out[5] = {};
  if (!q.push("abcd", 4))
    return false;
  q.consume(3);
  if (!q.push("XYZ", 3) || q.peek(out, 5) != 4)
    return false;
  return out[0] == 'd' && out[1] == 'X' && out[3] == 'Z' && q.used() == 4;
}
constexpr bool queueOverflowAtomic() {
  ByteQueue<4> q;
  char out[4] = {};
  q.push("abc", 3);
  return !q.push("de", 2) && q.used() == 3 && q.peek(out, 4) == 3 && out[2] == 'c';
}
constexpr bool queueConsumeClamp() {
  ByteQueue<4> q;
  q.push("abc", 3);
  q.consume(10);
  return q.free() == 4;
}
constexpr bool clockWrap() {
  MicroClock c;
  c.update(0xFFFFFFF0);
  return c.update(0x20) == 0x100000020ULL;
}
constexpr bool utcOk(const char *s, size_t n) {
  uint64_t e = 0;
  return parseUtc(s, n, e);
}
constexpr uint64_t epoch(const char *s) {
  uint64_t e = 0;
  parseUtc(s, 20, e);
  return e;
}
constexpr bool dateRoundTrip() {
  const auto d = dateParts(epoch("2024-02-29T23:59:59Z") + 1);
  return d.year == 2024 && d.month == 3 && d.day == 1 && d.hour == 0;
}
constexpr bool massOk(const char *s) {
  double v = 0;
  return parseMass(s, v);
}
constexpr bool lineCrlf() {
  CommandLine c;
  c.push('T', 0);
  c.push('\r', 1);
  return c.push('\n', 2) == LineResult::Ready && c.text()[0] == 'T' && c.text()[1] == ' ';
}
constexpr bool lineTimeout() {
  CommandLine c;
  c.push('C', UINT32_MAX - 10);
  return c.expire(989) && c.push('A', 990) == LineResult::Pending &&
         c.push('\n', 991) == LineResult::Pending && c.push('T', 992) == LineResult::Pending &&
         c.push('\n', 993) == LineResult::Ready && c.text()[0] == 'T';
}
constexpr bool lineOverflow() {
  CommandLine c;
  for (int i = 0; i < 79; ++i)
    c.push('a', 1);
  return c.push('x', 2) == LineResult::Invalid && c.push('\n', 3) == LineResult::Pending;
}

// A transport mock tests the REAL driver's FSM, not a duplicated implementation.
struct Bus {
  uint8_t regs[32] = {};
  bool nack = false, powerReady = true, dataReady = true, calStuck = false, calError = false;
  bool shortRead = false, badReadback = false;
  uint32_t sampleReads = 0;
  constexpr bool write(uint8_t reg, uint8_t value) {
    if (nack)
      return false;
    regs[reg] = value;
    return true;
  }
  constexpr bool read(uint8_t reg, uint8_t *p, size_t n) {
    if (nack)
      return false;
    if (reg == 0x12) {
      ++sampleReads;
      if (shortRead || n != 3)
        return false;
      p[0] = 0xFF;
      p[1] = 0xFF;
      p[2] = 0xFE;
    } else if (reg == 0)
      p[0] = (powerReady ? 8 : 0) | (dataReady ? 32 : 0);
    else if (reg == 2 && (regs[2] & 4) && !calStuck)
      p[0] = 0x70 | (calError ? 8 : 0);
    else
      p[0] = badReadback && reg == 1 ? 0 : regs[reg];
    return true;
  }
};
constexpr void boot(Nau7802Reader &r, Bus &b) {
  r.begin(0);
  int32_t raw = 0;
  for (uint32_t ms = 0; ms < 400 && !r.ready() && !r.failed(); ++ms)
    r.tick(b, ms, raw);
}
constexpr bool driverReady() {
  Nau7802Reader r;
  Bus b;
  boot(r, b);
  return r.ready() && b.sampleReads == 8;
}
constexpr bool freshOnly() {
  Nau7802Reader r;
  Bus b;
  boot(r, b);
  int32_t raw = 123;
  const uint32_t n = b.sampleReads;
  b.dataReady = false;
  return r.tick(b, 300, raw) == Nau7802Reader::Result::None && raw == 123 && b.sampleReads == n;
}
constexpr bool repeatedValueIsNewSample() {
  Nau7802Reader r;
  Bus b;
  boot(r, b);
  int32_t raw = 0;
  return r.tick(b, 300, raw) == Nau7802Reader::Result::Sample && raw == -2 &&
         r.tick(b, 303, raw) == Nau7802Reader::Result::Sample && raw == -2;
}
constexpr bool driverError(int kind) {
  Nau7802Reader r;
  Bus b;
  if (kind == 0)
    b.nack = true;
  if (kind == 1)
    b.powerReady = false;
  if (kind == 2)
    b.calError = true;
  if (kind == 3)
    b.badReadback = true;
  if (kind == 4)
    b.shortRead = true;
  boot(r, b);
  return r.failed();
}
constexpr bool driverTimeout(bool calibration) {
  Nau7802Reader r;
  Bus b;
  b.calStuck = calibration;
  boot(r, b);
  b.dataReady = false;
  int32_t raw = 77;
  r.tick(b, 1600, raw);
  return r.failed() && raw == 77 &&
         same(r.error(), calibration ? "AFE_CAL_TIMEOUT" : "SAMPLE_TIMEOUT");
}
constexpr bool reconnect() {
  Nau7802Reader r;
  Bus b;
  b.nack = true;
  boot(r, b);
  b.nack = false;
  boot(r, b);
  return r.ready();
}
constexpr bool shortDoesNotReturnZero() {
  Nau7802Reader r;
  Bus b;
  boot(r, b);
  b.shortRead = true;
  int32_t raw = 456;
  return r.tick(b, 300, raw) == Nau7802Reader::Result::Error && raw == 456;
}

static_assert(SENSOR_SPS == 320 && SUMMARY_US == 50000, "rate contract");
static_assert(decode24(0, 0, 0) == 0, "real zero");
static_assert(decode24(0x7F, 0xFF, 0xFF) == RAW_MAX, "positive signed limit");
static_assert(decode24(0x80, 0, 0) == RAW_MIN, "negative signed limit");
static_assert(decode24(0xFF, 0xFF, 0xFF) == -1, "sign extension");
static_assert(!rail(0) && rail(RAW_MIN) && rail(RAW_MAX), "rails");
static_assert(calibrated(1), "positive calibration");
static_assert(calibrated(-1), "negative calibration direction");
static_assert(uncalibratedPreservesOutput(), "unavailable never replaced with zero");
static_assert(invalidCalibration(0), "zero span");
static_assert(invalidCalibration(1), "zero mass");
static_assert(invalidCalibration(2), "capacity");
static_assert(invalidCalibration(3), "rail calibration");
static_assert(invalidCalibration(4), "negative mass");
static_assert(retareInvalidates(), "retare invalidates scale");
static_assert(negativeForce(), "negative force preserved");
static_assert(railsInvalid(), "invalid force omitted");
static_assert(separated(100, 110, 200, 210), "positive span excludes tare noise");
static_assert(separated(100, 110, -200, -190), "negative span excludes tare noise");
static_assert(!separated(100, 110, 105, 115), "overlapping calibration rejected");
static_assert(!separated(100, 110, 110, 120), "touching calibration rejected");
static_assert(makeWindow(false).count == 3 && makeWindow(false).validCount == 2, "window counts");
static_assert(makeWindow(false).forceCount == 0 && makeWindow(false).rails == 1, "uncal summary");
static_assert(makeWindow(true).forceCount == 2 && near(makeWindow(true).forceSum, 0),
              "force summary");
static_assert(near(makeWindow(true).forceMin, -STANDARD_G), "summary min");
static_assert(near(makeWindow(true).forceMax, STANDARD_G), "summary peak");
static_assert(queueWrap(), "ring wrap FIFO");
static_assert(queueOverflowAtomic(), "ring overflow no partial line");
static_assert(queueConsumeClamp(), "ring consumption bound");
static_assert(clockWrap(), "micros wrap extension");
static_assert(utcOk("2026-09-27T00:00:00Z", 20), "UTC valid");
static_assert(!utcOk("2026-09-27T00:00:00Zx", 21), "UTC trailing junk");
static_assert(!utcOk("2026-02-29T00:00:00Z", 20), "UTC invalid date");
static_assert(utcOk("2024-02-29T00:00:00Z", 20), "UTC leap date");
static_assert(!utcOk("2026-09-27T24:00:00Z", 20), "UTC invalid hour");
static_assert(!utcOk("2026-09-27T00:00:60Z", 20), "unsupported leap second");
static_assert(!utcOk("2026-09-27 00:00:00Z", 20), "UTC strict separator");
static_assert(epoch("2020-01-01T00:00:00Z") == 1577836800ULL, "UTC epoch");
static_assert(dateRoundTrip(), "UTC rollover");
static_assert(massOk("2.5") && massOk("100"), "mass parse");
static_assert(!massOk("0") && !massOk("-1") && !massOk("101"), "mass range");
static_assert(!massOk("nan") && !massOk("1e2") && !massOk("2kg"), "mass strict parse");
static_assert(!massOk("") && !massOk("."), "mass empty");
static_assert(lineCrlf(), "CRLF");
static_assert(lineTimeout(), "timeout and wrap recovery");
static_assert(lineOverflow(), "oversized input rejected");
static_assert(driverReady(), "driver config and discard");
static_assert(freshOnly(), "CR=0 never sample");
static_assert(repeatedValueIsNewSample(), "constant load still real samples");
static_assert(driverError(0), "NACK");
static_assert(driverError(1), "power timeout");
static_assert(driverError(2), "AFE failure");
static_assert(driverError(3), "config readback failure");
static_assert(driverError(4), "short read startup");
static_assert(driverTimeout(true), "AFE deadline");
static_assert(driverTimeout(false), "data deadline");
static_assert(reconnect(), "sensor recovery");
static_assert(shortDoesNotReturnZero(), "short read is not valid zero");
