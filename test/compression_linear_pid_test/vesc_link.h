#ifndef SAVER_LINEAR_PID_VESC_LINK_H
#define SAVER_LINEAR_PID_VESC_LINK_H

#include <stddef.h>
#include <stdint.h>

// Standalone bench wire codec, NOT the Jetson JSON protocol. No UART I/O,
// scheduling, control loop, firmware configuration or automatic motor start.
// The bounded framing/values codec is reused from the single-motor bench.
// Wire references: vedderb/bldc comm/packet.c, comm/commands.c, datatypes.h.
namespace saver_pid_vesc {

constexpr uint8_t FW_VERSION = 0;
constexpr uint8_t GET_VALUES = 4;
constexpr uint8_t SET_CURRENT = 6;
constexpr uint8_t SET_RPM = 8;
constexpr uint32_t VALUES_STALE_MS = 300;
constexpr uint32_t PACKET_TIMEOUT_MS = 100;
constexpr size_t MAX_PAYLOAD = 512;

constexpr uint16_t crc16(const uint8_t *data, size_t length) {
  uint16_t crc = 0;
  for (size_t i = 0; i < length; ++i) {
    crc ^= static_cast<uint16_t>(data[i]) << 8;
    for (uint8_t bit = 0; bit < 8; ++bit)
      crc = static_cast<uint16_t>((crc << 1) ^ ((crc & 0x8000U) ? 0x1021U : 0U));
  }
  return crc;
}

// Only the one-byte requests and five-byte commands required by this bench.
struct Frame {
  uint8_t bytes[10] = {};
  size_t length = 0;
};

constexpr Frame request(uint8_t id) {
  Frame f;
  f.length = 6;
  f.bytes[0] = 2;
  f.bytes[1] = 1;
  f.bytes[2] = id;
  const uint16_t crc = crc16(&f.bytes[2], 1);
  f.bytes[3] = static_cast<uint8_t>(crc >> 8);
  f.bytes[4] = static_cast<uint8_t>(crc);
  f.bytes[5] = 3;
  return f;
}

constexpr Frame command(uint8_t id, int32_t scaled) {
  Frame f;
  f.length = 10;
  f.bytes[0] = 2;
  f.bytes[1] = 5;
  f.bytes[2] = id;
  const uint32_t value = static_cast<uint32_t>(scaled);
  for (size_t i = 0; i < 4; ++i)
    f.bytes[3 + i] = static_cast<uint8_t>(value >> (24 - 8 * i));
  const uint16_t crc = crc16(&f.bytes[2], 5);
  f.bytes[7] = static_cast<uint8_t>(crc >> 8);
  f.bytes[8] = static_cast<uint8_t>(crc);
  f.bytes[9] = 3;
  return f;
}

constexpr Frame rpmCommand(int32_t electricalRpm) {
  // COMM_SET_RPM takes signed electrical RPM (ERPM), not mechanical shaft RPM.
  // No scaling: comm/commands.c passes buffer_get_int32 to the speed controller.
  // Mechanical RPM conversion needs independently confirmed motor pole pairs.
  return command(SET_RPM, electricalRpm);
}
constexpr Frame releaseCommand() {
  // Zero commanded motor current is a release request, not braking, a verified
  // standstill, a holding command, or a replacement for hardware E-stop.
  return command(SET_CURRENT, 0);
}

enum class PacketResult : uint8_t { Pending, Ready, Invalid };

class PacketParser {
public:
  constexpr void reset() {
    state_ = 0;
    length_ = 0;
    used_ = 0;
    crc_ = 0;
  }
  constexpr bool expire(uint32_t now) {
    if (!state_ || static_cast<uint32_t>(now - started_) < PACKET_TIMEOUT_MS)
      return false;
    reset();
    return true;
  }
  constexpr PacketResult push(uint8_t byte, uint32_t now) {
    // An expired frame consumes/discards this byte; the caller must treat
    // Invalid as a link failure, not reuse buffered motion commands.
    if (expire(now))
      return PacketResult::Invalid;
    switch (state_) {
    case 0:
      if (byte != 2 && byte != 3)
        return PacketResult::Invalid;
      started_ = now;
      state_ = byte == 2 ? 1 : 2;
      break;
    case 1:
      length_ = byte;
      return beginPayload();
    case 2:
      length_ = static_cast<uint16_t>(byte) << 8;
      state_ = 3;
      break;
    case 3:
      length_ |= byte;
      if (length_ <= 255) {
        reset();
        return PacketResult::Invalid;
      }
      return beginPayload();
    case 4:
      payload_[used_++] = byte;
      if (used_ == length_)
        state_ = 5;
      break;
    case 5:
      crc_ = static_cast<uint16_t>(byte) << 8;
      state_ = 6;
      break;
    case 6:
      crc_ |= byte;
      state_ = 7;
      break;
    case 7: {
      const bool valid = byte == 3 && crc16(payload_, length_) == crc_;
      state_ = 0; // Payload remains available until the next frame.
      return valid ? PacketResult::Ready : PacketResult::Invalid;
    }
    default:
      reset();
      return PacketResult::Invalid;
    }
    return PacketResult::Pending;
  }
  // Read these only after Ready, before pushing the next frame.
  constexpr const uint8_t *data() const { return payload_; }
  constexpr size_t size() const { return length_; }

private:
  constexpr PacketResult beginPayload() {
    if (!length_ || length_ > MAX_PAYLOAD) {
      reset();
      return PacketResult::Invalid;
    }
    used_ = 0;
    state_ = 4;
    return PacketResult::Pending;
  }
  uint8_t payload_[MAX_PAYLOAD] = {};
  uint8_t state_ = 0;
  uint16_t length_ = 0, used_ = 0, crc_ = 0;
  uint32_t started_ = 0;
};

constexpr int32_t signed16(const uint8_t *p) {
  const uint32_t value = (static_cast<uint32_t>(p[0]) << 8) | p[1];
  return value & 0x8000U ? static_cast<int32_t>(value) - 65536 : static_cast<int32_t>(value);
}
constexpr int32_t signed32(const uint8_t *p) {
  const uint32_t value = (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
                         (static_cast<uint32_t>(p[2]) << 8) | p[3];
  return value & 0x80000000U ? -1 - static_cast<int32_t>(~value) : static_cast<int32_t>(value);
}

struct Values {
  int32_t fetDeciC = 0, motorCentiA = 0, inputCentiA = 0;
  int32_t dutyMilli = 0, erpm = 0, inputDeciV = 0;
  uint8_t fault = 0;
};

constexpr bool decodeValues(const uint8_t *p, size_t length, Values &out) {
  // VESC 5/6 GET_VALUES common prefix through fault_code, including command ID.
  // Motor temperature is omitted: its sensor/configuration is unknown.
  if (!p || length < 54 || p[0] != GET_VALUES)
    return false;
  Values v;
  v.fetDeciC = signed16(p + 1);
  v.motorCentiA = signed32(p + 5);
  v.inputCentiA = signed32(p + 9);
  v.dutyMilli = signed16(p + 21);
  v.erpm = signed32(p + 23);
  v.inputDeciV = signed16(p + 27);
  v.fault = p[53];
  // Structural plausibility, NOT a safe supply-voltage or motor-current limit.
  if (v.dutyMilli < -1000 || v.dutyMilli > 1000 || v.inputDeciV <= 0)
    return false;
  out = v;
  return true;
}

constexpr bool supportedFirmware(uint8_t major) { return major == 5 || major == 6; }

} // namespace saver_pid_vesc
#endif
