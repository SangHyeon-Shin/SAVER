// Compile-time tests of the SAME logic included by the Arduino sketch.
// No GPIO, motor, USB, ISR scheduling, or executable execution is involved.
#include "../bench_logic.h"
using namespace saver_vacuum_bench;

constexpr bool parseCase(const char* text, bool expected, uint32_t value = 0) {
  uint32_t result = 123;
  const bool ok = parseUnsigned(text, result);
  return ok == expected && (ok ? result == value : result == 123);
}
static_assert(parseCase("6", true, 6), "Default duty parses");
static_assert(parseCase("15000", true, 15000), "Default duration parses");
static_assert(parseCase("4294967295", true, UINT32_MAX), "Maximum uint32 parses");
static_assert(parseCase("4294967296", false), "Overflow is rejected, not wrapped");
static_assert(parseCase("99999999999999999999999", false), "Huge input rejected");
static_assert(parseCase("-1", false), "Negative values rejected");
static_assert(parseCase("+6", false), "Unsigned grammar is digits only");
static_assert(parseCase("6x", false), "Suffix rejected");
static_assert(parseCase("", false), "Empty input rejected");
static_assert(parseCase(nullptr, false), "Missing required argument rejected");
static_assert(dutyToPwm(0) == 0 && dutyToPwm(6) == 15 && dutyToPwm(100) == 255,
              "8-bit PWM rounding matches single-motor sketch");

constexpr bool runTiming(uint32_t now) {
  RunState s;
  if (s.running() || s.duty() != 0 || s.fault()) return false;
  s.observeErr(false, now);
  if (s.start(6, 15000, now) || !s.running() || s.remaining(now) != 15000) return false;
  if (s.expire(now + 14999) || s.remaining(now + 14999) != 1) return false;
  return s.expire(now + 15000) && !s.running() && s.duty() == 0 && s.remaining(now + 15000) == 0;
}
static_assert(runTiming(0), "Explicit RUN then 15-second stop, no autostart");
static_assert(runTiming(0xFFFFFF00U), "Run timeout survives millis rollover");

constexpr bool runBounds(uint32_t duty, uint32_t duration, bool accepted) {
  RunState s;
  const bool ok = s.start(duty, duration, 0) == nullptr;
  return ok == accepted && s.running() == accepted && (accepted || s.duty() == 0);
}
static_assert(runBounds(1, 100, true), "Lower bench bounds accepted");
static_assert(runBounds(100, 30000, true), "Upper bench bounds accepted");
static_assert(runBounds(0, 15000, false), "RUN zero duty rejected");
static_assert(runBounds(101, 15000, false), "RUN over 100 rejected");
static_assert(runBounds(6, 99, false), "Duration below minimum rejected");
static_assert(runBounds(6, 30001, false), "Duration above maximum rejected");

constexpr bool repeatRun() {
  RunState s;
  s.start(6, 15000, 0);
  return s.start(6, 15000, 1000) != nullptr && !s.running() && s.duty() == 0;
}
static_assert(repeatRun(), "Repeated RUN stops rather than extending operation");

constexpr bool setDoesNotExtend() {
  RunState s;
  if (s.set(6) == nullptr || s.running()) return false;
  s.start(6, 15000, 0);
  if (s.set(8) || s.duty() != 8 || s.remaining(10000) != 5000) return false;
  if (!s.expire(15000)) return false;
  s.start(6, 15000, 20000);
  return s.set(0) == nullptr && !s.running() && s.duty() == 0;
}
static_assert(setDoesNotExtend(), "SET requires RUN, keeps deadline, SET zero stops");

constexpr bool faultPolicy(uint32_t now) {
  RunState s;
  s.start(6, 15000, now);
  if (!s.observeErr(true, now + 1) || s.running() || !s.fault()) return false;
  if (s.observeErr(true, now + 2)) return false; // Report a new latch once.
  if (s.start(6, 15000, now + 3) == nullptr || s.running()) return false;
  if (s.set(0) || !s.fault()) return false; // STOP cannot silently clear an ERR.
  s.observeErr(false, now + 10);
  if (s.clearErr(now + 259) == nullptr || !s.fault()) return false;
  if (s.clearErr(now + 260) || s.fault() || s.running()) return false;
  return s.start(6, 15000, now + 300) == nullptr && s.running();
}
static_assert(faultPolicy(0), "ERR stops/latches; stable HIGH plus CLEAR_ERR needed");
static_assert(faultPolicy(0xFFFFFF80U), "ERR clear timing survives rollover");

constexpr bool errReassertion() {
  RunState s;
  s.observeErr(true, 0); s.observeErr(false, 10); s.observeErr(true, 200);
  s.observeErr(false, 201);
  return s.clearErr(450) != nullptr && s.fault() && s.clearErr(451) == nullptr;
}
static_assert(errReassertion(), "Another LOW restarts the stable-inactive interval");

constexpr bool clearWhileRunning() {
  RunState s;
  s.observeErr(false, 0); s.start(6, 15000, 300);
  return s.clearErr(500) != nullptr && !s.running();
}
static_assert(clearWhileRunning(), "CLEAR_ERR never keeps an active run going");

constexpr bool stopPreservesFault() {
  RunState s;
  s.observeErr(true, 0); s.stop(); // Same policy for STOP/disconnect/malformed input.
  return !s.running() && s.duty() == 0 && s.fault();
}
static_assert(stopPreservesFault(), "External stop does not erase latched ERR");

constexpr bool validLine() {
  CommandLine line;
  const char* text = "run 6 15000\r\n";
  LineResult r = LineResult::Pending;
  for (size_t i = 0; text[i]; ++i) r = line.push(text[i], (uint32_t)i);
  return r == LineResult::Ready && line.data()[0] == 'r' && line.data()[10] == '0';
}
static_assert(validLine(), "CRLF produces one complete line");

constexpr bool staleLine(uint32_t now) {
  CommandLine line;
  line.push('R', now); line.push('U', now); line.push('N', now);
  line.push('\r', now + 999); // CR cannot postpone the deadline indefinitely.
  if (line.expire(now + 999) || !line.expire(now + 1000)) return false;
  if (line.push('\n', now + 1001) == LineResult::Ready) return false;
  line.push('S', now + 1002);
  return line.push('\n', now + 1003) == LineResult::Ready;
}
static_assert(staleLine(0), "Incomplete RUN expires; late newline cannot start motor");
static_assert(staleLine(0xFFFFFFF0U), "Command timeout survives rollover");

constexpr bool invalidLine(char bad) {
  CommandLine line;
  line.push('R', 0); line.push('U', 0); line.push('N', 0);
  if (line.push(bad, 0) != LineResult::Invalid) return false;
  line.push('R', 1); line.push('U', 1); line.push('N', 1);
  return line.push('\n', 1) != LineResult::Ready;
}
static_assert(invalidLine('\0'), "Embedded NUL cannot truncate and execute RUN");
static_assert(invalidLine((char)0xFF), "Non-ASCII corruption rejected");

constexpr bool tooLong() {
  CommandLine line;
  for (unsigned i = 0; i < 63; ++i)
    if (line.push('X', 0) != LineResult::Pending) return false;
  return line.push('X', 0) == LineResult::Invalid && line.push('\n', 0) != LineResult::Ready;
}
static_assert(tooLong(), "Overflow rejects immediately without waiting for newline");

constexpr bool resetPartial() {
  CommandLine line;
  line.push('R', 0); line.push('U', 0); line.push('N', 0);
  line.reset(); // Sketch uses this on USB disconnect/reconnect.
  return line.push('\n', 1) != LineResult::Ready;
}
static_assert(resetPartial(), "New USB session cannot finish the old RUN fragment");

constexpr bool highErrDoesNotLatch() {
  RunState s;
  for (uint32_t now = 0; now < 1000; now += 10)
    if (s.observeErr(false, now) || s.fault()) return false;
  return s.start(10, 15000, 1000) == nullptr && s.running() && s.duty() == 10;
}
static_assert(highErrDoesNotLatch(), "Inactive ERR never latches or requires FG to start");

constexpr bool bootLowRemainsProtected() {
  RunState s;
  if (!s.observeErr(true, 0) || !s.fault()) return false;
  s.observeErr(false, 100);
  if (s.start(10, 15000, 500) == nullptr || s.running()) return false;
  return s.clearErr(500) == nullptr && !s.fault() && !s.running();
}
static_assert(bootLowRemainsProtected(), "Removing FG does not bypass boot ERR or auto-restart");
