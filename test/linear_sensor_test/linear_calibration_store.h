#ifndef SAVER_LINEAR_CALIBRATION_STORE_H
#define SAVER_LINEAR_CALIBRATION_STORE_H

#include "linear_calibration.h"

namespace saver_linear {
namespace storage {
// Standalone linear test writes; main firmware reads this same record.
// Reserve EEPROM bytes 4096..4135 on Teensy 4.1. Other sketches must not overwrite it.
constexpr int ADDRESS = 4096;
constexpr size_t SIZE = 40;
constexpr uint8_t COMMITTED = 0xA5;
constexpr uint8_t PIN = 40, ADC_BITS = 12;
// Installation profile: nominal 75mm, 3.3V supply, ADC averaging 16.
// Change the profile/version if wiring, ADC configuration or interpretation changes.
constexpr uint32_t PROFILE = 0x75003310UL;
struct Record {
  uint8_t bytes[SIZE] = {};
};
enum class LoadResult { Loaded, Empty, Invalid, NoSpace };

constexpr void put(Record &r, size_t offset, uint32_t value, size_t count) {
  for (size_t i = 0; i < count; ++i)
    r.bytes[offset + i] = static_cast<uint8_t>(value >> (8 * i));
}
constexpr uint32_t get(const Record &r, size_t offset, size_t count) {
  uint32_t value = 0;
  for (size_t i = 0; i < count; ++i)
    value |= static_cast<uint32_t>(r.bytes[offset + i]) << (8 * i);
  return value;
}
// CRC-32/ISO-HDLC over bytes 1..35. Marker is separate, committed last.
constexpr uint32_t checksum(const Record &r) {
  uint32_t crc = 0xFFFFFFFFUL;
  for (size_t i = 1; i < SIZE - 4; ++i) {
    crc ^= r.bytes[i];
    for (uint8_t bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ ((crc & 1U) ? 0xEDB88320UL : 0U);
  }
  return ~crc;
}

constexpr bool decode(const Record &r, LinearCalibration &out) {
  out.reset(); // Never retain stale calibration on a failed load.
  if (r.bytes[0] != COMMITTED || get(r, 1, 4) != 0x434E4C53UL || r.bytes[5] != 1 ||
      r.bytes[6] != PIN || r.bytes[7] != ADC_BITS || get(r, 8, 4) != PROFILE ||
      get(r, 36, 4) != checksum(r))
    return false;
  // Explicit little-endian fixed-point fields; no struct padding/float ABI dependency.
  const CalPoint zero{get(r, 12, 4) / 1000.0f, static_cast<int>(get(r, 16, 2)),
                      static_cast<int>(get(r, 18, 2)), static_cast<uint16_t>(get(r, 20, 2))};
  const CalPoint span{get(r, 28, 4) / 1000.0f, static_cast<int>(get(r, 22, 2)),
                      static_cast<int>(get(r, 24, 2)), static_cast<uint16_t>(get(r, 26, 2))};
  const float distanceMm = get(r, 32, 4) / 1000000.0f;
  LinearCalibration candidate;
  if (!candidate.setZero(zero) || !candidate.setSpan(span, distanceMm))
    return false;
  out = candidate;
  return true;
}

constexpr bool encode(const LinearCalibration &cal, Record &r) {
  r = Record{};
  if (!cal.ready())
    return false;
  r.bytes[0] = COMMITTED;
  put(r, 1, 0x434E4C53UL, 4); // "SLNC"
  r.bytes[5] = 1;
  r.bytes[6] = PIN;
  r.bytes[7] = ADC_BITS;
  put(r, 8, PROFILE, 4);
  const CalPoint z = cal.zero(), s = cal.span();
  put(r, 12, static_cast<uint32_t>(static_cast<double>(z.mean) * 1000 + .5), 4);
  put(r, 16, z.low, 2);
  put(r, 18, z.high, 2);
  put(r, 20, z.count, 2);
  put(r, 22, s.low, 2);
  put(r, 24, s.high, 2);
  put(r, 26, s.count, 2);
  put(r, 28, static_cast<uint32_t>(static_cast<double>(s.mean) * 1000 + .5), 4);
  put(r, 32, static_cast<uint32_t>(static_cast<double>(cal.distance()) * 1000000 + .5), 4);
  put(r, 36, checksum(r), 4);
  LinearCalibration checked;
  return decode(r, checked);
}

// Memory interface: length(), read(address), update(address, value).
// Bounded synchronous writes ONLY for calibration commands, never per ADC sample.
template <class Memory> constexpr bool available(Memory &memory) {
  return memory.length() >= ADDRESS + static_cast<int>(SIZE);
}
template <class Memory> constexpr LoadResult load(Memory &memory, LinearCalibration &cal) {
  cal.reset();
  if (!available(memory))
    return LoadResult::NoSpace;
  const uint8_t marker = memory.read(ADDRESS);
  if (marker == 0 || marker == 0xFF)
    return LoadResult::Empty;
  Record r;
  for (size_t i = 0; i < SIZE; ++i)
    r.bytes[i] = memory.read(ADDRESS + static_cast<int>(i));
  return decode(r, cal) ? LoadResult::Loaded : LoadResult::Invalid;
}
template <class Memory> constexpr bool invalidate(Memory &memory) {
  if (!available(memory))
    return false;
  memory.update(ADDRESS, 0);
  return memory.read(ADDRESS) == 0;
}
template <class Memory> constexpr bool save(Memory &memory, LinearCalibration &cal) {
  Record expected;
  if (!encode(cal, expected) || !invalidate(memory))
    return false;
  for (size_t i = 1; i < SIZE; ++i) {
    const int address = ADDRESS + static_cast<int>(i);
    memory.update(address, expected.bytes[i]);
    if (memory.read(address) != expected.bytes[i])
      return false; // Marker stays invalid; do not activate a partial record.
  }
  memory.update(ADDRESS, COMMITTED);
  Record actual;
  for (size_t i = 0; i < SIZE; ++i) {
    actual.bytes[i] = memory.read(ADDRESS + static_cast<int>(i));
    if (actual.bytes[i] != expected.bytes[i]) {
      invalidate(memory);
      return false;
    }
  }
  LinearCalibration restored;
  if (!decode(actual, restored)) {
    invalidate(memory);
    return false;
  }
  cal = restored; // Same normalized values now and after power cycling.
  return true;
}
} // namespace storage
} // namespace saver_linear
#endif
