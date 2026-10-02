#ifndef SAVER_COMPRESSION_VESC_BENCH_H
#define SAVER_COMPRESSION_VESC_BENCH_H

#include <stddef.h>
#include <stdint.h>

// Standalone bench protocol, NOT the Jetson JSON protocol.
// Wire facts checked against vedderb/bldc: comm/packet.c, comm/commands.c,
// datatypes.h and util/crc.c. Implementation below is local and bounded.
namespace saver_compression_bench {

constexpr uint8_t FW_VERSION = 0;
constexpr uint8_t GET_VALUES = 4;
constexpr uint8_t SET_DUTY = 5;
constexpr uint8_t SET_CURRENT = 6;
constexpr uint32_t VALUES_STALE_MS = 300;
constexpr uint32_t PACKET_TIMEOUT_MS = 100;
constexpr uint32_t LINE_TIMEOUT_MS = 1000;
constexpr uint32_t CLEAR_STABLE_MS = 250;
constexpr uint32_t ARM_TIMEOUT_MS = 10000;
constexpr uint32_t ABSOLUTE_MAX_RUN_MS =
    30000; // Existing bench duration ceiling, not medical timing.
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

// Transmitted requests have only 1 or 5 payload bytes; no config/flash/CAN writes.
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

constexpr Frame dutyCommand(int32_t percent) {
  // duty fraction * 100000 == integer percent * 1000.
  // Signed direction follows the VESC's current invert-direction configuration.
  // Fail closed before multiplying an invalid/extreme signed input.
  if (percent < -100 || percent > 100)
    return command(SET_CURRENT, 0);
  return command(SET_DUTY, static_cast<int32_t>(percent) * 1000);
}
constexpr Frame releaseCommand() { return command(SET_CURRENT, 0); }

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
  // Motor temperature is deliberately omitted: its sensor/configuration is unknown.
  if (length < 54 || p[0] != GET_VALUES)
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

constexpr bool parseUnsigned(const char *text, uint32_t &out) {
  if (!text || !*text)
    return false;
  uint32_t value = 0;
  for (const char *p = text; *p; ++p) {
    if (*p < '0' || *p > '9')
      return false;
    const uint32_t digit = static_cast<uint32_t>(*p - '0');
    if (value > (UINT32_MAX - digit) / 10U)
      return false;
    value = value * 10U + digit;
  }
  out = value;
  return true;
}

constexpr bool parseDutyPercent(const char *text, int32_t &out) {
  if (!text || !*text)
    return false;
  const bool negative = *text == '-';
  if (negative || *text == '+')
    ++text;
  uint32_t magnitude = 0;
  if (!parseUnsigned(text, magnitude) || magnitude > 100)
    return false;
  out = negative ? -static_cast<int32_t>(magnitude) : static_cast<int32_t>(magnitude);
  return true; // RUN rejects zero; SET 0 retains its stop meaning.
}

enum class LineResult : uint8_t { Pending, Ready, Invalid };
class CommandLine {
public:
  constexpr void reset() {
    length_ = 0;
    discard_ = false;
    bytes_[0] = '\0';
  }
  constexpr bool expire(uint32_t now) {
    if (!length_ || static_cast<uint32_t>(now - started_) < LINE_TIMEOUT_MS)
      return false;
    length_ = 0;
    discard_ = true;
    return true;
  }
  constexpr LineResult push(char c, uint32_t now) {
    if (c == '\n') {
      if (discard_) {
        reset();
        return LineResult::Pending;
      }
      bytes_[length_] = '\0';
      return length_ ? LineResult::Ready : LineResult::Pending;
    }
    if (discard_)
      return LineResult::Pending;
    if (c == '\r')
      c = ' '; // Accept CRLF without joining tokens/numbers across embedded CR.
    if ((static_cast<uint8_t>(c) < 32 && c != '\t') || static_cast<uint8_t>(c) > 126 ||
        length_ >= sizeof(bytes_) - 1) {
      length_ = 0;
      discard_ = true;
      return LineResult::Invalid;
    }
    if (!length_)
      started_ = now; // Total assembly timeout; a trickle cannot extend it.
    bytes_[length_++] = c;
    return LineResult::Pending;
  }
  constexpr char *data() { return bytes_; }

private:
  char bytes_[64] = {};
  size_t length_ = 0;
  uint32_t started_ = 0;
  bool discard_ = false;
};

struct Limits {
  bool setupConfirmed;
  uint32_t maxDutyPercent;
  uint32_t maxRunMs;
};
constexpr bool configured(Limits l) {
  return l.setupConfirmed && l.maxDutyPercent >= 1 && l.maxDutyPercent <= 100 &&
         l.maxRunMs >= 100 && l.maxRunMs <= ABSOLUTE_MAX_RUN_MS;
}
constexpr bool dutyAllowed(int32_t duty, Limits limits) {
  return configured(limits) && duty != 0 && duty >= -static_cast<int32_t>(limits.maxDutyPercent) &&
         duty <= static_cast<int32_t>(limits.maxDutyPercent);
}

// No I/O here. The sketch must queue release BEFORE logging rejection/faults.
class Control {
public:
  constexpr void stop() {
    armed_ = false;
    running_ = false;
    duty_ = 0;
    duration_ = 0;
  }
  constexpr void trip() {
    stop();
    fault_ = true;
  }
  constexpr void invalidate() {
    valuesSeen_ = false;
    stable_ = false;
    zeroStable_ = false;
    stop();
  }
  constexpr void observe(uint8_t fault, int32_t erpm, int32_t dutyMilli, uint32_t now) {
    const bool wasFresh = fresh(now);
    valuesSeen_ = true;
    lastValues_ = now;
    driverFault_ = fault;
    if (fault) {
      stable_ = false;
      zeroStable_ = false;
      trip();
    } else {
      if (!stable_ || !wasFresh) {
        stable_ = true;
        stableSince_ = now;
      }
      if (running_ || erpm != 0 || dutyMilli != 0)
        zeroStable_ = false;
      else if (!zeroStable_ || !wasFresh) {
        zeroStable_ = true;
        zeroSince_ = now;
      }
    }
  }
  constexpr bool fresh(uint32_t now) const {
    return valuesSeen_ && static_cast<uint32_t>(now - lastValues_) < VALUES_STALE_MS;
  }
  constexpr bool ready(uint32_t now) const {
    return fresh(now) && driverFault_ == 0 && stable_ &&
           static_cast<uint32_t>(now - stableSince_) >= CLEAR_STABLE_MS;
  }
  // VESC-reported zero only, NOT an independent proof of shaft standstill.
  // Reuse the existing 250 ms communication stability interval, no guessed RPM tolerance.
  constexpr bool reportedZeroStable(uint32_t now) const {
    return !running_ && fresh(now) && driverFault_ == 0 && zeroStable_ &&
           static_cast<uint32_t>(lastValues_ - zeroSince_) >= CLEAR_STABLE_MS;
  }
  constexpr const char *arm(Limits limits, bool linkReady, uint32_t now) {
    if (running_) {
      trip();
      return "ARM_WHILE_RUNNING";
    }
    stop();
    if (!configured(limits))
      return "CONFIG_LOCKED_READ_README";
    if (fault_)
      return "FAULT_LATCHED_USE_CLEAR_ERR";
    if (!linkReady || !ready(now))
      return "VESC_NOT_READY";
    if (!reportedZeroStable(now))
      return "VESC_NOT_REPORTING_ZERO_FOR_250MS";
    armed_ = true;
    armedAt_ = now;
    return nullptr;
  }
  constexpr const char *start(int32_t duty, uint32_t duration, Limits limits, bool linkReady,
                              uint32_t now) {
    if (running_) {
      trip();
      return "ALREADY_RUNNING";
    }
    if (!configured(limits) || !armed_ || fault_ || !linkReady || !ready(now) ||
        static_cast<uint32_t>(now - armedAt_) >= ARM_TIMEOUT_MS) {
      trip();
      return "RUN_REQUIRES_FRESH_ARM_AND_LINK";
    }
    if (!reportedZeroStable(now)) {
      trip();
      return "VESC_NOT_REPORTING_ZERO_FOR_250MS";
    }
    if (!dutyAllowed(duty, limits) || duration < 100 || duration > limits.maxRunMs) {
      trip();
      return "RUN_OUT_OF_RANGE";
    }
    duty_ = static_cast<int16_t>(duty);
    duration_ = duration;
    started_ = now;
    running_ = true;
    zeroStable_ = false; // A new drive invalidates the PREVIOUS run's reported stop evidence.
    return nullptr;
  }
  constexpr const char *set(int32_t duty, Limits limits, uint32_t now) {
    if (!duty) {
      stop();
      return nullptr;
    }
    if (!running_ || fault_ || !fresh(now) || !dutyAllowed(duty, limits)) {
      trip();
      return "SET_REJECTED";
    }
    if ((duty < 0) != (duty_ < 0)) {
      trip();
      return "DIRECTION_CHANGE_REQUIRES_STOP_AND_ARM";
    }
    duty_ = static_cast<int16_t>(duty);
    return nullptr; // Original deadline is unchanged.
  }
  constexpr const char *clear(bool linkReady, uint32_t now) {
    if (running_) {
      trip();
      return "CLEAR_REQUIRES_STOP";
    }
    stop();
    if (!linkReady || !ready(now))
      return "VESC_NOT_FAULT_FREE_FOR_250MS";
    fault_ = false;
    return nullptr; // Local latch only; never writes VESC configuration/reset.
  }
  constexpr const char *check(bool usbReady, bool linkReady, uint32_t now) {
    if (!armed_ && !running_)
      return nullptr;
    if (!usbReady || !linkReady || !fresh(now)) {
      trip();
      return "LINK_LOST";
    }
    if (!running_ && !reportedZeroStable(now)) {
      trip();
      return "MOTION_REPORTED_WHILE_ARMED";
    }
    if (running_ && static_cast<uint32_t>(now - started_) >= duration_) {
      stop();
      return "RUN_TIMEOUT";
    }
    if (!running_ && static_cast<uint32_t>(now - armedAt_) >= ARM_TIMEOUT_MS) {
      stop();
      return "ARM_TIMEOUT";
    }
    return nullptr;
  }
  constexpr bool armed() const { return armed_; }
  constexpr bool running() const { return running_; }
  constexpr bool fault() const { return fault_; }
  constexpr int16_t duty() const { return duty_; }
  constexpr uint32_t age(uint32_t now) const { return static_cast<uint32_t>(now - lastValues_); }
  constexpr uint32_t remaining(uint32_t now) const {
    const uint32_t elapsed = static_cast<uint32_t>(now - started_);
    return running_ && elapsed < duration_ ? duration_ - elapsed : 0;
  }

private:
  bool armed_ = false, running_ = false, fault_ = false, valuesSeen_ = false, stable_ = false;
  bool zeroStable_ = false;
  int16_t duty_ = 0;
  uint8_t driverFault_ = 0;
  uint32_t armedAt_ = 0, started_ = 0, duration_ = 0;
  uint32_t lastValues_ = 0, stableSince_ = 0, zeroSince_ = 0;
};

} // namespace saver_compression_bench
#endif
