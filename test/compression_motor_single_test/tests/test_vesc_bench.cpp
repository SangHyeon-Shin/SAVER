// These assertions evaluate the SAME code included by the Arduino sketch.
// -fsyntax-only executes the constexpr checks without uploading/running a motor.
#include "../vesc_bench.h"
using namespace saver_compression_bench;

constexpr Limits unlocked = {true, 10, 2000}; // Synthetic test fixture, NOT a motor recommendation.
constexpr Limits locked = {false, 0, 0};
constexpr uint8_t crcVector[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
static_assert(crc16(crcVector, 9) == 0x31C3, "CRC-16/XMODEM independent check vector");
static_assert(request(FW_VERSION).length == 6 && request(FW_VERSION).bytes[2] == 0 &&
                  request(FW_VERSION).bytes[3] == 0 && request(FW_VERSION).bytes[4] == 0,
              "Version request: 02 01 00 00 00 03");
static_assert(request(GET_VALUES).bytes[3] == 0x40 && request(GET_VALUES).bytes[4] == 0x84,
              "GET_VALUES CRC matches independent 0x4084 vector");
static_assert(dutyCommand(5).length == 10 && dutyCommand(5).bytes[2] == 5 &&
                  dutyCommand(5).bytes[5] == 0x13 && dutyCommand(5).bytes[6] == 0x88,
              "5 percent = 5000, big-endian duty scaling (not PWM pin write)");
constexpr bool sameFrame(Frame actual, const uint8_t *expected, size_t length) {
  if (actual.length != length)
    return false;
  for (size_t i = 0; i < length; ++i)
    if (actual.bytes[i] != expected[i])
      return false;
  return true;
}
// Golden frames calculated independently of the header's command()/crc16().
constexpr uint8_t plusFive[] = {2, 5, 5, 0, 0, 0x13, 0x88, 0x65, 0xF7, 3};
constexpr uint8_t minusFive[] = {2, 5, 5, 0xFF, 0xFF, 0xEC, 0x78, 0x0D, 0xD7, 3};
constexpr uint8_t plusHundred[] = {2, 5, 5, 0, 1, 0x86, 0xA0, 0x10, 0xB3, 3};
constexpr uint8_t minusHundred[] = {2, 5, 5, 0xFF, 0xFE, 0x79, 0x60, 0x4E, 0xC0, 3};
static_assert(sameFrame(dutyCommand(5), plusFive, sizeof(plusFive)),
              "Positive duty complete frame/CRC golden vector");
static_assert(sameFrame(dutyCommand(-5), minusFive, sizeof(minusFive)),
              "Negative duty complete two's-complement frame/CRC golden vector");
static_assert(sameFrame(dutyCommand(100), plusHundred, sizeof(plusHundred)) &&
                  sameFrame(dutyCommand(-100), minusHundred, sizeof(minusHundred)),
              "Signed wire extrema +/-100 percent, no unsigned wrapping");
static_assert(sameFrame(dutyCommand(101), releaseCommand().bytes, releaseCommand().length) &&
                  sameFrame(dutyCommand(-101), releaseCommand().bytes, releaseCommand().length) &&
                  sameFrame(dutyCommand(INT32_MIN), releaseCommand().bytes,
                            releaseCommand().length) &&
                  sameFrame(dutyCommand(INT32_MAX), releaseCommand().bytes,
                            releaseCommand().length),
              "Invalid duty cannot overflow or encode drive; releases current instead");
static_assert(releaseCommand().bytes[2] == 6 && releaseCommand().bytes[3] == 0 &&
                  releaseCommand().bytes[4] == 0 && releaseCommand().bytes[5] == 0 &&
                  releaseCommand().bytes[6] == 0,
              "STOP sets CURRENT 0, not braking current");
static_assert(command(SET_CURRENT, -1000).bytes[3] == 0xFF &&
                  command(SET_CURRENT, -1000).bytes[5] == 0xFC &&
                  command(SET_CURRENT, -1000).bytes[6] == 0x18,
              "Signed 32-bit encoding");
constexpr uint8_t negative[] = {0xFF, 0xFF, 0xFC, 0x18};
constexpr uint8_t minInt[] = {0x80, 0, 0, 0};
static_assert(signed32(negative) == -1000 && signed16(negative) == -1, "Signed decoding");
static_assert(signed32(minInt) == INT32_MIN, "No signed overflow at INT32_MIN");

constexpr bool frameRoundTrip(bool corruptCrc, bool corruptEnd) {
  PacketParser p;
  Frame f = dutyCommand(5);
  if (corruptCrc)
    f.bytes[7] ^= 1;
  if (corruptEnd)
    f.bytes[9] = 4;
  PacketResult result = PacketResult::Pending;
  for (size_t i = 0; i < f.length; ++i)
    result = p.push(f.bytes[i], 10);
  return result == PacketResult::Ready && p.size() == 5 && p.data()[0] == SET_DUTY;
}
static_assert(frameRoundTrip(false, false), "Short frame round trip");
static_assert(!frameRoundTrip(true, false), "CRC corruption rejected");
static_assert(!frameRoundTrip(false, true), "Bad trailer rejected");
constexpr bool malformedLength(uint8_t start, uint8_t high, uint8_t low) {
  PacketParser p;
  p.push(start, 0);
  PacketResult r = p.push(high, 0);
  if (start == 3)
    r = p.push(low, 0);
  return r == PacketResult::Invalid;
}
static_assert(malformedLength(2, 0, 0), "Zero payload length rejected");
static_assert(malformedLength(3, 2, 1), "Payload above 512 rejected");
static_assert(malformedLength(3, 0, 5), "Noncanonical short length in long header rejected");
constexpr bool longPacket() {
  uint8_t data[256] = {};
  data[0] = 42;
  PacketParser p;
  p.push(3, 0);
  p.push(1, 0);
  p.push(0, 0);
  for (size_t i = 0; i < 256; ++i)
    p.push(data[i], 0);
  const uint16_t crc = crc16(data, 256);
  p.push(static_cast<uint8_t>(crc >> 8), 0);
  p.push(static_cast<uint8_t>(crc), 0);
  return p.push(3, 0) == PacketResult::Ready && p.size() == 256 && p.data()[0] == 42;
}
static_assert(longPacket(), "Bounded 16-bit length frame support");
constexpr bool packetTimeout() {
  PacketParser p;
  p.push(2, UINT32_MAX - 50);
  p.push(5, UINT32_MAX - 49);
  if (p.expire(48))
    return false;
  if (!p.expire(49))
    return false;
  const Frame f = request(FW_VERSION);
  PacketResult r = PacketResult::Pending;
  for (size_t i = 0; i < f.length; ++i)
    r = p.push(f.bytes[i], 100);
  return r == PacketResult::Ready;
}
static_assert(packetTimeout(), "Packet timeout and recovery across millis rollover");

constexpr bool valuesDecode() {
  uint8_t bytes[78] = {};
  bytes[0] = GET_VALUES;
  bytes[1] = 1;
  bytes[2] = 0x2C; // 30 C
  bytes[7] = 0;
  bytes[8] = 150; // 1.50 A motor
  bytes[12] = 25; // 0.25 A input
  bytes[22] = 50; // 5% duty
  bytes[25] = 0x27;
  bytes[26] = 0x10; // 10000 eRPM, not shaft RPM
  bytes[28] = 240;  // 24 V
  Values v;
  return decodeValues(bytes, sizeof(bytes), v) && v.fetDeciC == 300 && v.motorCentiA == 150 &&
         v.inputCentiA == 25 && v.erpm == 10000 && v.inputDeciV == 240 && v.dutyMilli == 50 &&
         v.fault == 0;
}
static_assert(valuesDecode(), "Telemetry offsets/scales and future trailing fields");
constexpr bool valuesInvalid(bool lengthBad, bool typeBad, bool dutyBad, bool voltageBad) {
  uint8_t b[54] = {};
  b[0] = typeBad ? 50 : GET_VALUES;
  b[28] = voltageBad ? 0 : 240;
  if (dutyBad) {
    b[21] = 0x7F;
    b[22] = 0xFF;
  }
  Values v;
  v.erpm = 1234;
  return !decodeValues(b, lengthBad ? 53 : 54, v) && v.erpm == 1234;
}
static_assert(valuesInvalid(true, false, false, false), "Truncated values cannot read past buffer");
static_assert(valuesInvalid(false, true, false, false), "Wrong reply type rejected");
static_assert(valuesInvalid(false, false, true, false), "Impossible duty rejects layout");
static_assert(valuesInvalid(false, false, false, true), "Invalid supply voltage rejects values");
static_assert(supportedFirmware(5) && supportedFirmware(6) && !supportedFirmware(3) &&
                  !supportedFirmware(7),
              "Unknown firmware layouts blocked");

constexpr bool parses(const char *text, uint32_t expected) {
  uint32_t v = 123;
  return parseUnsigned(text, v) && v == expected;
}
constexpr bool refuses(const char *text) {
  uint32_t v = 123;
  return !parseUnsigned(text, v) && v == 123;
}
static_assert(parses("5", 5) && parses("0", 0) && parses("4294967295", UINT32_MAX),
              "Unsigned inputs");
static_assert(refuses("4294967296") && refuses("999999999999999999999"), "Overflow refused");
static_assert(refuses("-5") && refuses("+5") && refuses("5.0") && refuses("1e2"),
              "Strict integer grammar");
static_assert(refuses("NaN") && refuses("inf") && refuses("") && refuses(nullptr),
              "No malformed values");
constexpr bool parsesDuty(const char *text, int32_t expected) {
  int32_t value = 123;
  return parseDutyPercent(text, value) && value == expected;
}
constexpr bool refusesDuty(const char *text) {
  int32_t value = 123;
  return !parseDutyPercent(text, value) && value == 123;
}
static_assert(parsesDuty("5", 5) && parsesDuty("+5", 5) && parsesDuty("-5", -5),
              "Duty has explicit optional direction sign");
static_assert(parsesDuty("100", 100) && parsesDuty("-100", -100) && parsesDuty("+100", 100),
              "Full signed protocol magnitude accepted");
static_assert(parsesDuty("0", 0) && parsesDuty("-0", 0) && parsesDuty("+0", 0) &&
                  parsesDuty("-00005", -5),
              "Signed zero and leading zero forms are deterministic");
static_assert(refusesDuty("101") && refusesDuty("-101") && refusesDuty("2147483647") &&
                  refusesDuty("-2147483648") && refusesDuty("4294967296") &&
                  refusesDuty("-99999999999999999999999999"),
              "Out-of-range/overflow values preserve output on rejection");
static_assert(refusesDuty("+") && refusesDuty("-") && refusesDuty("--5") && refusesDuty("+-5") &&
                  refusesDuty("-+5") && refusesDuty("++5"),
              "Exactly one optional sign and at least one digit required");
static_assert(refusesDuty("5.0") && refusesDuty("-1e2") && refusesDuty("0x5") &&
                  refusesDuty("NaN") && refusesDuty("inf") && refusesDuty("") &&
                  refusesDuty(nullptr),
              "No floating/scientific/hex/missing duty forms");
static_assert(refusesDuty(" -5") && refusesDuty("-5 ") && refusesDuty("- 5") &&
                  refusesDuty("5\r0") && refusesDuty("5\n") && refusesDuty("-\t5"),
              "Token parser never ignores whitespace or embedded control characters");
constexpr bool lineEnding() {
  CommandLine l;
  l.push('R', 0);
  l.push('U', 1);
  l.push('N', 2);
  l.push('\r', 3);
  return l.push('\n', 4) == LineResult::Ready && l.data()[0] == 'R' && l.data()[3] == ' ' &&
         l.data()[4] == '\0';
}
static_assert(lineEnding(), "Monitor CRLF accepted");
constexpr bool carriageReturnSeparates() {
  CommandLine l;
  l.push('-', 0);
  l.push('5', 0);
  l.push('\r', 0);
  l.push('0', 0);
  return l.push('\n', 0) == LineResult::Ready && l.data()[0] == '-' && l.data()[1] == '5' &&
         l.data()[2] == ' ' && l.data()[3] == '0' && l.data()[4] == '\0';
}
static_assert(carriageReturnSeparates(), "Embedded CR cannot turn -5 CR 0 into -50");
constexpr bool partialLine() {
  CommandLine l;
  l.push('R', 0);
  l.push('U', 999);
  if (!l.expire(1000))
    return false;
  l.push('N', 1100);
  l.push('\n', 1100);
  l.push('S', 1100);
  return l.push('\n', 1100) == LineResult::Ready && l.data()[0] == 'S';
}
static_assert(partialLine(), "Total command timeout discards late suffix through newline");
constexpr bool invalidLine(bool overflow) {
  CommandLine l;
  LineResult r = LineResult::Pending;
  if (overflow)
    for (int i = 0; i < 64; ++i)
      r = l.push('X', 0);
  else
    r = l.push('\0', 0);
  if (r != LineResult::Invalid)
    return false;
  l.push('R', 0);
  l.push('U', 0);
  l.push('N', 0);
  return l.push('\n', 0) == LineResult::Pending;
}
static_assert(invalidLine(true), "Overlong command cannot execute a suffix");
static_assert(invalidLine(false), "Embedded NUL command rejected");

constexpr void goodLink(Control &c, uint32_t offset = 0) {
  c.observe(0, 0, 0, offset);
  c.observe(0, 0, 0, offset + 100);
  c.observe(0, 0, 0, offset + 250);
}
static_assert(!configured(locked), "Default configuration locked");
static_assert(!configured({true, 0, 1000}) && !configured({true, 101, 1000}) &&
                  !configured({true, 5, 0}) && !configured({true, 5, 30001}),
              "Unconfirmed/invalid limits lock drive");
constexpr bool noAutoStart() {
  Control c;
  if (c.armed() || c.running())
    return false;
  goodLink(c);
  return !c.running() && c.arm(locked, true, 250) != nullptr && !c.armed();
}
static_assert(noAutoStart(), "Boot and successful link do not arm or run");
constexpr bool normalRun() {
  Control c;
  goodLink(c);
  if (c.arm(unlocked, true, 250) || c.start(5, 1000, unlocked, true, 250))
    return false;
  c.observe(0, 0, 0, 400);
  if (c.set(6, unlocked, 400) || c.remaining(400) != 850)
    return false;
  c.observe(0, 0, 0, 1200);
  if (c.check(true, true, 1249))
    return false;
  return c.check(true, true, 1250) != nullptr && !c.running() && !c.armed();
}
static_assert(normalRun(), "ARM/RUN/SET and original deadline; timeout disarms");
constexpr bool requestedProfileLocked() {
  const Limits limits = {false, 5, 5000};
  Control c;
  goodLink(c);
  return !configured(limits) && c.arm(limits, true, 250) != nullptr && !c.armed() &&
         c.start(5, 5000, limits, true, 250) != nullptr && !c.running();
}
static_assert(requestedProfileLocked(),
              "5 percent / 5000 ms stays locked without setup confirmation");
constexpr bool requestedProfileDeadline() {
  // Synthetic confirmation/link only; this does not verify physical VESC setup.
  const Limits limits = {true, 5, 5000};
  Control c;
  goodLink(c);
  if (c.arm(limits, true, 250) || c.start(5, 5000, limits, true, 250) || !c.running() ||
      c.duty() != 5 || c.remaining(250) != 5000)
    return false;
  for (uint32_t now = 350; now <= 5150; now += 100) {
    c.observe(0, 0, 0, now);
    if (c.check(true, true, now))
      return false;
  }
  if (c.check(true, true, 5249) || !c.running() || c.remaining(5249) != 1)
    return false;
  return c.check(true, true, 5250) != nullptr && !c.running() && !c.armed() && !c.fault() &&
         c.duty() == 0 && c.remaining(5250) == 0;
}
static_assert(requestedProfileDeadline(), "5 percent run stops at 5000 ms, not 4999 ms");
constexpr bool requestedProfileRejectsRange(int32_t duty, uint32_t duration) {
  // Test-only confirmation; never enables the sketch's physical bench configuration.
  const Limits limits = {true, 5, 5000};
  Control c;
  goodLink(c);
  if (c.arm(limits, true, 250))
    return false;
  return c.start(duty, duration, limits, true, 250) != nullptr && !c.running() && !c.armed() &&
         c.fault();
}
static_assert(requestedProfileRejectsRange(6, 5000),
              "Requested profile rejects duty above 5 percent");
static_assert(requestedProfileRejectsRange(5, 5001),
              "Requested profile rejects duration above 5000 ms");
constexpr bool noArm() {
  Control c;
  goodLink(c);
  return c.start(5, 1000, unlocked, true, 250) != nullptr && !c.running();
}
static_assert(noArm(), "RUN without ARM refused");
constexpr bool duplicateRun() {
  Control c;
  goodLink(c);
  c.arm(unlocked, true, 250);
  c.start(5, 1000, unlocked, true, 250);
  return c.start(5, 2000, unlocked, true, 250) != nullptr && !c.running() && c.fault();
}
static_assert(duplicateRun(), "Repeated RUN stops/latches instead of extending duration");
constexpr bool rejectsRange(int32_t duty, uint32_t ms) {
  Control c;
  goodLink(c);
  c.arm(unlocked, true, 250);
  return c.start(duty, ms, unlocked, true, 250) != nullptr && !c.running();
}
static_assert(rejectsRange(0, 1000) && rejectsRange(11, 1000),
              "Default zero/above duty ceiling rejected");
static_assert(rejectsRange(5, 99) && rejectsRange(5, 2001), "Duration range rejected");
constexpr bool disconnected(bool usb, bool vesc) {
  Control c;
  goodLink(c);
  c.arm(unlocked, true, 250);
  c.start(5, 1000, unlocked, true, 250);
  return c.check(usb, vesc, 251) != nullptr && !c.running() && c.fault();
}
static_assert(disconnected(false, true), "USB disconnect stops and latches");
static_assert(disconnected(true, false), "VESC disconnect stops and latches");
constexpr bool stale() {
  Control c;
  goodLink(c);
  c.arm(unlocked, true, 250);
  c.start(5, 1000, unlocked, true, 250);
  if (c.check(true, true, 549))
    return false;
  return c.check(true, true, 550) != nullptr && !c.running() && c.fault();
}
static_assert(stale(), "300ms missing telemetry stops, even if TX keeps succeeding");
constexpr bool faultAndClear() {
  Control c;
  goodLink(c);
  c.arm(unlocked, true, 250);
  c.start(5, 1000, unlocked, true, 250);
  c.observe(1, 0, 0, 300);
  if (c.running() || !c.fault() || c.clear(true, 300) == nullptr)
    return false;
  c.observe(0, 0, 0, 350);
  c.observe(0, 0, 0, 450);
  if (c.clear(true, 599) == nullptr)
    return false;
  c.observe(0, 0, 0, 600);
  return !c.clear(true, 600) && !c.fault() && !c.running() && !c.armed();
}
static_assert(faultAndClear(),
              "Driver fault latches; stable recovery plus manual clear; no restart");
constexpr bool staleClear() {
  Control c;
  goodLink(c);
  c.trip();
  if (c.clear(true, 600) == nullptr)
    return false;
  c.observe(0, 0, 0, 600);
  return c.clear(true, 600) != nullptr;
}
static_assert(staleClear(), "A telemetry gap resets fault-free stability interval");
constexpr bool armExpiry() {
  Control c;
  goodLink(c);
  c.arm(unlocked, true, 250);
  c.observe(0, 0, 0, 10250);
  return c.start(5, 1000, unlocked, true, 10250) != nullptr && !c.running();
}
static_assert(armExpiry(), "Stale ARM cannot cause delayed start");
constexpr bool stopZero() {
  Control c;
  goodLink(c);
  c.arm(unlocked, true, 250);
  c.start(5, 1000, unlocked, true, 250);
  return !c.set(0, locked, 300) && !c.running() && !c.armed();
}
static_assert(stopZero(), "SET 0 always stops/disarms");
constexpr bool rollover() {
  Control c;
  const uint32_t start = UINT32_MAX - 300;
  goodLink(c, start);
  c.arm(unlocked, true, start + 250);
  if (c.start(5, 1000, unlocked, true, start + 250))
    return false;
  c.observe(0, 0, 0, 900);
  return c.remaining(948) == 1 && c.check(true, true, 949) != nullptr && !c.running();
}
static_assert(rollover(), "Run duration across millis rollover");
constexpr bool noReconnectStart() {
  Control c;
  goodLink(c);
  c.arm(unlocked, true, 250);
  c.start(5, 1000, unlocked, true, 250);
  c.trip();
  c.invalidate();
  goodLink(c, 1000);
  return c.fault() && !c.armed() && !c.running();
}
static_assert(noReconnectStart(), "Recovery never restores old drive request or clears latch");

constexpr bool textEquals(const char *a, const char *b) {
  if (!a || !b)
    return a == b;
  for (size_t i = 0;; ++i) {
    if (a[i] != b[i])
      return false;
    if (!a[i])
      return true;
  }
}
constexpr bool reverseRun() {
  Control c;
  goodLink(c);
  if (c.arm(unlocked, true, 250) || c.start(-5, 1000, unlocked, true, 250) || !c.running() ||
      c.duty() != -5 || c.reportedZeroStable(250))
    return false;
  c.observe(0, -1200, -50, 400);
  if (c.set(-6, unlocked, 400) || c.duty() != -6 || c.remaining(400) != 850)
    return false;
  c.observe(0, -1200, -60, 1200);
  if (c.check(true, true, 1249) || c.remaining(1249) != 1)
    return false;
  return textEquals(c.check(true, true, 1250), "RUN_TIMEOUT") && !c.running() && !c.armed() &&
         !c.fault() && c.duty() == 0;
}
static_assert(reverseRun(), "Negative RUN/SET preserves sign and original timeout deadline");
static_assert(rejectsRange(-11, 1000) && rejectsRange(INT32_MIN, 1000) &&
                  rejectsRange(INT32_MAX, 1000),
              "Magnitude limits reject reverse overflow and signed extrema without abs overflow");
static_assert(requestedProfileRejectsRange(-6, 5000),
              "Synthetic five-percent profile also bounds negative magnitude");
constexpr bool directionChangeBlocked(int32_t initial, int32_t opposite) {
  Control c;
  goodLink(c);
  if (c.arm(unlocked, true, 250) || c.start(initial, 1000, unlocked, true, 250))
    return false;
  c.observe(0, initial * 100, initial * 10, 300);
  const char *error = c.set(opposite, unlocked, 300);
  return textEquals(error, "DIRECTION_CHANGE_REQUIRES_STOP_AND_ARM") && !c.running() &&
         !c.armed() && c.fault() && c.duty() == 0;
}
static_assert(directionChangeBlocked(5, -5), "SET cannot reverse a positive drive");
static_assert(directionChangeBlocked(-5, 5), "SET cannot reverse a negative drive");
constexpr bool noSingleSampleStandstill() {
  Control c;
  c.observe(0, 0, 0, 0);
  return !c.reportedZeroStable(250) && !c.reportedZeroStable(299) &&
         c.arm(unlocked, true, 250) != nullptr && !c.armed();
}
static_assert(noSingleSampleStandstill(),
              "One zero telemetry sample ageing is not stability evidence");
constexpr bool observedZeroInterval() {
  Control c;
  c.observe(0, 0, 0, 0);
  c.observe(0, 0, 0, 249);
  if (c.reportedZeroStable(250))
    return false;
  c.observe(0, 0, 0, 250);
  return c.reportedZeroStable(250) && c.reportedZeroStable(549) && !c.reportedZeroStable(550);
}
static_assert(observedZeroInterval(), "250 ms observation span plus fresh latest sample required");
constexpr bool nonzeroBreaksInterval(int32_t erpm, int32_t dutyMilli) {
  Control c;
  goodLink(c);
  if (!c.reportedZeroStable(250))
    return false;
  c.observe(0, erpm, dutyMilli, 300);
  if (c.reportedZeroStable(300))
    return false;
  c.observe(0, 0, 0, 350);
  c.observe(0, 0, 0, 599);
  if (c.reportedZeroStable(599))
    return false;
  c.observe(0, 0, 0, 600);
  return c.reportedZeroStable(600);
}
static_assert(nonzeroBreaksInterval(1, 0) && nonzeroBreaksInterval(-1, 0),
              "Either direction of nonzero reported eRPM invalidates zero evidence");
static_assert(nonzeroBreaksInterval(0, 1) && nonzeroBreaksInterval(0, -1),
              "Nonzero reported duty invalidates zero evidence even at zero eRPM");
constexpr bool zeroGapResets() {
  Control c;
  goodLink(c);
  c.observe(0, 0, 0, 550);
  if (c.reportedZeroStable(550))
    return false;
  c.observe(0, 0, 0, 799);
  if (c.reportedZeroStable(799))
    return false;
  c.observe(0, 0, 0, 800);
  return c.reportedZeroStable(800);
}
static_assert(zeroGapResets(), "A stale telemetry gap resets zero-speed observation interval");
constexpr bool zeroFaultInvalidation() {
  Control c;
  goodLink(c);
  c.observe(1, 0, 0, 300);
  if (c.reportedZeroStable(300) || !c.fault())
    return false;
  goodLink(c, 350);
  if (!c.reportedZeroStable(600))
    return false;
  c.invalidate();
  return !c.reportedZeroStable(600) && !c.armed() && !c.running();
}
static_assert(zeroFaultInvalidation(), "Driver fault and link invalidation discard zero evidence");
constexpr bool idleStopRetainsEvidence() {
  Control c;
  goodLink(c);
  c.stop();
  c.stop();
  return c.reportedZeroStable(250) && c.arm(unlocked, true, 250) == nullptr && c.armed();
}
static_assert(idleStopRetainsEvidence(), "Repeated idle release requests do not prevent fresh ARM");
constexpr bool noMotionWhileArmed(int32_t erpm, int32_t dutyMilli) {
  Control c;
  goodLink(c);
  if (c.arm(unlocked, true, 250))
    return false;
  c.observe(0, erpm, dutyMilli, 300);
  return textEquals(c.check(true, true, 300), "MOTION_REPORTED_WHILE_ARMED") && !c.armed() &&
         !c.running() && c.fault();
}
static_assert(noMotionWhileArmed(1, 0) && noMotionWhileArmed(0, -1),
              "A newly moving/displaced-duty report invalidates an idle ARM");
constexpr bool startRechecksZero() {
  Control c;
  goodLink(c);
  if (c.arm(unlocked, true, 250))
    return false;
  c.observe(0, -1, 0, 300);
  return c.start(-5, 1000, unlocked, true, 300) != nullptr && !c.running() && !c.armed() &&
         c.fault();
}
static_assert(startRechecksZero(), "RUN rechecks reported zero, not only earlier ARM result");
constexpr bool coastThenReverse() {
  Control c;
  goodLink(c);
  if (c.arm(unlocked, true, 250) || c.start(5, 1000, unlocked, true, 250))
    return false;
  c.observe(0, 1200, 50, 300);
  c.stop();
  c.observe(0, 500, 0, 350);
  if (c.arm(unlocked, true, 350) == nullptr || c.armed())
    return false;
  c.observe(0, 0, 0, 400);
  c.observe(0, 0, 0, 649);
  if (c.arm(unlocked, true, 649) == nullptr || c.armed())
    return false;
  c.observe(0, 0, 0, 650);
  if (c.arm(unlocked, true, 650) || c.start(-5, 1000, unlocked, true, 650))
    return false;
  return c.running() && c.duty() == -5 && !c.fault();
}
static_assert(coastThenReverse(), "Reverse requires STOP, zero reports over 250 ms, and new ARM");
constexpr bool runZeroReportsNotReused() {
  Control c;
  goodLink(c);
  if (c.arm(unlocked, true, 250) || c.start(-5, 1000, unlocked, true, 250))
    return false;
  c.observe(0, 0, 0, 300);
  c.observe(0, 0, 0, 550);
  c.stop();
  if (c.reportedZeroStable(550))
    return false;
  c.observe(0, 0, 0, 600);
  c.observe(0, 0, 0, 850);
  return c.reportedZeroStable(850);
}
static_assert(runZeroReportsNotReused(),
              "Reports while drive-commanded cannot seed post-stop interval");
constexpr bool reverseZeroSetStops() {
  Control c;
  goodLink(c);
  c.arm(unlocked, true, 250);
  c.start(-5, 1000, unlocked, true, 250);
  return !c.set(0, locked, 300) && !c.running() && !c.armed() && c.duty() == 0;
}
static_assert(reverseZeroSetStops(), "SET 0 releases reverse drive even when configuration locks");
constexpr bool reverseNeedsNewArm() {
  Control c;
  goodLink(c);
  c.arm(unlocked, true, 250);
  c.start(5, 1000, unlocked, true, 250);
  c.stop();
  goodLink(c, 300);
  return c.start(-5, 1000, unlocked, true, 550) != nullptr && !c.running() && !c.armed();
}
static_assert(reverseNeedsNewArm(),
              "STOP consumes ARM; zero reports alone never authorize reversal");
constexpr bool reverseRollover() {
  Control c;
  const uint32_t start = UINT32_MAX - 300;
  goodLink(c, start);
  if (c.arm(unlocked, true, start + 250) || c.start(-5, 1000, unlocked, true, start + 250))
    return false;
  c.observe(0, -1000, -50, 900);
  return c.remaining(948) == 1 && c.check(true, true, 949) != nullptr && !c.running();
}
static_assert(reverseRollover(),
              "Reverse duration and zero-stability interval survive millis rollover");

int main() { return 0; } // If built natively, all preceding assertions must already have passed.
