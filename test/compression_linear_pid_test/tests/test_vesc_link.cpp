// C++14 constant evaluation checks the SAME codec used by the Arduino sketch.
// These are synthetic wire fixtures, not confirmed motor operating limits.
// No serial port, GPIO, board, or motor is accessed by this test.
#include "../vesc_link.h"
using namespace saver_pid_vesc;

constexpr uint8_t crcVector[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
static_assert(crc16(crcVector, 9) == 0x31C3, "CRC-16/XMODEM independent check vector");
static_assert(request(FW_VERSION).length == 6 && request(FW_VERSION).bytes[0] == 2 &&
                  request(FW_VERSION).bytes[1] == 1 && request(FW_VERSION).bytes[2] == 0 &&
                  request(FW_VERSION).bytes[3] == 0 && request(FW_VERSION).bytes[4] == 0 &&
                  request(FW_VERSION).bytes[5] == 3,
              "Version request: 02 01 00 00 00 03");
static_assert(request(GET_VALUES).bytes[3] == 0x40 && request(GET_VALUES).bytes[4] == 0x84,
              "GET_VALUES CRC independent check vector");
static_assert(rpmCommand(1000).length == 10 && rpmCommand(1000).bytes[0] == 2 &&
                  rpmCommand(1000).bytes[1] == 5 && rpmCommand(1000).bytes[2] == 8 &&
                  rpmCommand(1000).bytes[3] == 0 && rpmCommand(1000).bytes[4] == 0 &&
                  rpmCommand(1000).bytes[5] == 0x03 && rpmCommand(1000).bytes[6] == 0xE8 &&
                  rpmCommand(1000).bytes[9] == 3,
              "Positive ERPM: signed int32 big-endian, no duty/shaft-RPM scaling");
static_assert(rpmCommand(-1000).bytes[3] == 0xFF && rpmCommand(-1000).bytes[4] == 0xFF &&
                  rpmCommand(-1000).bytes[5] == 0xFC && rpmCommand(-1000).bytes[6] == 0x18,
              "Negative ERPM sign and byte order");
static_assert(rpmCommand(0).bytes[2] == 8 && rpmCommand(0).bytes[3] == 0 &&
                  rpmCommand(0).bytes[4] == 0 && rpmCommand(0).bytes[5] == 0 &&
                  rpmCommand(0).bytes[6] == 0,
              "Zero ERPM remains a speed request, not a release");
static_assert(releaseCommand().bytes[2] == 6 && releaseCommand().bytes[3] == 0 &&
                  releaseCommand().bytes[4] == 0 && releaseCommand().bytes[5] == 0 &&
                  releaseCommand().bytes[6] == 0,
              "Release requests CURRENT 0, never current brake or holding");
constexpr uint8_t negative[] = {0xFF, 0xFF, 0xFC, 0x18};
constexpr uint8_t minInt[] = {0x80, 0, 0, 0};
constexpr uint8_t maxInt[] = {0x7F, 0xFF, 0xFF, 0xFF};
constexpr uint8_t minShort[] = {0x80, 0};
static_assert(signed32(negative) == -1000 && signed16(negative) == -1, "Signed decoders");
static_assert(signed32(minInt) == INT32_MIN && signed32(maxInt) == INT32_MAX &&
                  signed16(minShort) == -32768,
              "Signed decoder extrema without overflow or implementation-defined cast");
static_assert(rpmCommand(INT32_MIN).bytes[3] == 0x80 && rpmCommand(INT32_MIN).bytes[6] == 0 &&
                  rpmCommand(INT32_MAX).bytes[3] == 0x7F && rpmCommand(INT32_MAX).bytes[6] == 0xFF,
              "Transport preserves extrema; the control layer must impose motion limits");

constexpr PacketResult pushFrame(PacketParser &p, Frame f, uint32_t now) {
  PacketResult result = PacketResult::Pending;
  for (size_t i = 0; i < f.length; ++i) {
    result = p.push(f.bytes[i], now);
    if (i + 1 < f.length && result != PacketResult::Pending)
      return PacketResult::Invalid;
  }
  return result;
}
constexpr bool rpmRoundTrip(int32_t erpm) {
  PacketParser p;
  return pushFrame(p, rpmCommand(erpm), 10) == PacketResult::Ready && p.size() == 5 &&
         p.data()[0] == SET_RPM && signed32(p.data() + 1) == erpm;
}
static_assert(rpmRoundTrip(1000) && rpmRoundTrip(-1000) && rpmRoundTrip(0),
              "Positive/negative/zero ERPM short-frame round trip");
static_assert(rpmRoundTrip(INT32_MIN) && rpmRoundTrip(INT32_MAX), "Signed extrema survive framing");
constexpr bool corruptFrame(bool corruptCrc, bool corruptEnd) {
  PacketParser p;
  Frame f = rpmCommand(-1000);
  if (corruptCrc)
    f.bytes[7] ^= 1;
  if (corruptEnd)
    f.bytes[9] = 4;
  return pushFrame(p, f, 10) == PacketResult::Invalid &&
         pushFrame(p, releaseCommand(), 11) == PacketResult::Ready && p.data()[0] == SET_CURRENT &&
         signed32(p.data() + 1) == 0;
}
static_assert(corruptFrame(true, false), "CRC corruption rejected; next whole frame recoverable");
static_assert(corruptFrame(false, true), "Bad trailer rejected; next whole frame recoverable");
constexpr bool malformedLength(uint8_t start, uint8_t high, uint8_t low) {
  PacketParser p;
  p.push(start, 0);
  PacketResult r = p.push(high, 0);
  if (start == 3)
    r = p.push(low, 0);
  return r == PacketResult::Invalid && p.size() == 0;
}
static_assert(malformedLength(2, 0, 0), "Zero payload length rejected");
static_assert(malformedLength(3, 2, 1), "Payload above 512 bytes rejected");
static_assert(malformedLength(3, 0, 255), "Noncanonical short length in long header rejected");
constexpr bool badStart() {
  PacketParser p;
  return p.push(4, 0) == PacketResult::Invalid &&
         pushFrame(p, request(FW_VERSION), 1) == PacketResult::Ready;
}
static_assert(badStart(), "Noise/unsupported frame header rejected with bounded recovery");
constexpr bool payloadLength(size_t length) {
  uint8_t data[MAX_PAYLOAD] = {};
  data[0] = GET_VALUES;
  data[length - 1] = 42;
  PacketParser p;
  p.push(length > 255 ? 3 : 2, 0);
  if (length > 255)
    p.push(static_cast<uint8_t>(length >> 8), 0);
  p.push(static_cast<uint8_t>(length), 0);
  for (size_t i = 0; i < length; ++i)
    if (p.push(data[i], 0) != PacketResult::Pending)
      return false;
  const uint16_t crc = crc16(data, length);
  p.push(static_cast<uint8_t>(crc >> 8), 0);
  p.push(static_cast<uint8_t>(crc), 0);
  return p.push(3, 0) == PacketResult::Ready && p.size() == length && p.data()[0] == GET_VALUES &&
         p.data()[length - 1] == 42;
}
static_assert(payloadLength(255), "Maximum short payload supported");
static_assert(payloadLength(256), "Minimum canonical long payload supported");
static_assert(payloadLength(512), "Maximum bounded long payload supported");

constexpr bool partialTimeout(uint32_t start) {
  PacketParser p;
  p.push(2, start);
  p.push(5, start + 1);
  // A late but not-yet-expired byte must not extend the assembly deadline.
  p.push(SET_RPM, start + PACKET_TIMEOUT_MS - 1);
  if (p.expire(start + PACKET_TIMEOUT_MS - 1))
    return false;
  if (!p.expire(start + PACKET_TIMEOUT_MS) || p.size() != 0)
    return false;
  if (p.expire(start + PACKET_TIMEOUT_MS + 1))
    return false;
  return pushFrame(p, releaseCommand(), start + PACKET_TIMEOUT_MS + 2) == PacketResult::Ready;
}
static_assert(partialTimeout(0), "Partial packet expires at 100ms even with a slow trickle");
static_assert(partialTimeout(UINT32_MAX - 50), "Partial timeout/recovery across millis wrap");
constexpr bool implicitTimeout() {
  PacketParser p;
  p.push(2, 0);
  p.push(5, 1);
  return p.push(SET_RPM, 100) == PacketResult::Invalid && p.size() == 0 &&
         pushFrame(p, request(GET_VALUES), 101) == PacketResult::Ready;
}
static_assert(implicitTimeout(), "push itself rejects expired partial packets");
constexpr bool incompleteNeverReady() {
  const Frame f = rpmCommand(1000);
  for (size_t prefix = 1; prefix < f.length; ++prefix) {
    PacketParser p;
    for (size_t i = 0; i < prefix; ++i)
      if (p.push(f.bytes[i], 0) != PacketResult::Pending)
        return false;
    if (!p.expire(PACKET_TIMEOUT_MS))
      return false;
  }
  return true;
}
static_assert(incompleteNeverReady(), "Every incomplete prefix times out, never becomes Ready");
constexpr bool repeatedFrames() {
  PacketParser p;
  const Frame f = rpmCommand(-1000);
  // This codec validates bytes only. It never executes requests or implements ACKs.
  return pushFrame(p, f, 0) == PacketResult::Ready && pushFrame(p, f, 1) == PacketResult::Ready &&
         p.size() == 5;
}
static_assert(repeatedFrames(), "Back-to-back packets have no stale parser state");

constexpr void write16(uint8_t *p, int32_t value) {
  const uint32_t bits = static_cast<uint32_t>(value);
  p[0] = static_cast<uint8_t>(bits >> 8);
  p[1] = static_cast<uint8_t>(bits);
}
constexpr void write32(uint8_t *p, int32_t value) {
  const uint32_t bits = static_cast<uint32_t>(value);
  for (size_t i = 0; i < 4; ++i)
    p[i] = static_cast<uint8_t>(bits >> (24 - 8 * i));
}
constexpr bool valuesDecode(bool negativeValues, uint8_t fault) {
  uint8_t bytes[78] = {};
  bytes[0] = GET_VALUES;
  const int32_t sign = negativeValues ? -1 : 1;
  write16(bytes + 1, sign * 300);   // 30 C magnitude.
  write32(bytes + 5, sign * 150);   // 1.50 A motor current magnitude.
  write32(bytes + 9, sign * 25);    // 0.25 A input current magnitude.
  write16(bytes + 21, sign * 50);   // 5% duty magnitude, diagnostics only.
  write32(bytes + 23, sign * 1000); // ERPM, not mechanical RPM.
  write16(bytes + 27, 240);         // 24 V fixture, not a supply recommendation.
  bytes[53] = fault;
  bytes[77] = 0xAB; // Future trailing fields do not move the common prefix.
  Values v;
  return decodeValues(bytes, sizeof(bytes), v) && v.fetDeciC == sign * 300 &&
         v.motorCentiA == sign * 150 && v.inputCentiA == sign * 25 && v.dutyMilli == sign * 50 &&
         v.erpm == sign * 1000 && v.inputDeciV == 240 && v.fault == fault;
}
static_assert(valuesDecode(false, 0), "Telemetry common prefix offsets, scales, and trailing data");
static_assert(valuesDecode(true, 0), "Negative telemetry survives signed decoding");
constexpr bool allFaultCodes() {
  for (unsigned int code = 0; code <= 255; ++code)
    if (!valuesDecode(false, static_cast<uint8_t>(code)))
      return false;
  return true;
}
static_assert(allFaultCodes(), "All nonzero fault codes are preserved, never hidden or coerced");
constexpr bool invalidValues(uint8_t type, size_t length, int32_t duty, int32_t voltage) {
  uint8_t bytes[54] = {};
  bytes[0] = type;
  write16(bytes + 21, duty);
  write16(bytes + 27, voltage);
  Values v;
  v.erpm = 1234;
  v.fault = 42;
  return !decodeValues(bytes, length, v) && v.erpm == 1234 && v.fault == 42;
}
static_assert(invalidValues(GET_VALUES, 53, 0, 240),
              "Truncation rejected without modifying output");
static_assert(invalidValues(FW_VERSION, 54, 0, 240), "Wrong packet type rejected");
static_assert(invalidValues(GET_VALUES, 54, 1001, 240) && invalidValues(GET_VALUES, 54, -1001, 240),
              "Structurally impossible duty rejects layout");
static_assert(invalidValues(GET_VALUES, 54, 0, 0) && invalidValues(GET_VALUES, 54, 0, -1),
              "Nonpositive input voltage rejects layout");
constexpr bool nullValues() {
  Values v;
  return !decodeValues(nullptr, 54, v) && !decodeValues(nullptr, 0, v);
}
static_assert(nullValues(), "Null payload rejected without dereference");
static_assert(supportedFirmware(5) && supportedFirmware(6) && !supportedFirmware(0) &&
                  !supportedFirmware(4) && !supportedFirmware(7),
              "Only reviewed firmware major layouts accepted");
static_assert(VALUES_STALE_MS == 300 && PACKET_TIMEOUT_MS == 100,
              "Existing bench link timing retained; caller enforces telemetry freshness");

int main() { return 0; } // Native build optional; all behavioral tests run during compilation.
