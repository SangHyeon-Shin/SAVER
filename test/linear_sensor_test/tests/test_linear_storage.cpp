#include "../linear_calibration_store.h"
#include "../../../teensy/src/saver_linear/linear_sensor_reader.h"

using namespace saver_linear;
namespace st = saver_linear::storage;

// Models byte-level EEPROM update/readback, NOT physical flash brownout behavior.
struct FakeMemory {
  uint8_t bytes[st::SIZE] = {};
  int capacity = st::ADDRESS + static_cast<int>(st::SIZE);
  int writes = 0, budget = 1000;
  bool outOfBounds = false;
  constexpr FakeMemory() {
    for (size_t i = 0; i < st::SIZE; ++i)
      bytes[i] = 0xFF;
  }
  constexpr int length() const { return capacity; }
  constexpr uint8_t read(int address) {
    if (address < st::ADDRESS || address >= capacity) {
      outOfBounds = true;
      return 0xFF;
    }
    return bytes[address - st::ADDRESS];
  }
  constexpr void update(int address, uint8_t value) {
    if (address < st::ADDRESS || address >= capacity) {
      outOfBounds = true;
      return;
    }
    if (bytes[address - st::ADDRESS] == value)
      return;
    if (budget <= 0)
      return;
    --budget;
    ++writes;
    bytes[address - st::ADDRESS] = value;
  }
};
constexpr bool close(float a, float b) { return a - b < .0001f && b - a < .0001f; }
constexpr LinearCalibration example(bool reverse = false) {
  LinearCalibration c;
  c.setZero(reverse ? CalPoint{4080.6f, 4079, 4082, 50} : CalPoint{39.62f, 37, 42, 50});
  c.setSpan(reverse ? CalPoint{39.62f, 37, 42, 50} : CalPoint{4080.6f, 4079, 4082, 50}, 50);
  return c;
}
constexpr bool roundTrip(bool reverse) {
  FakeMemory m;
  auto source = example(reverse);
  if (!st::save(m, source) || m.outOfBounds)
    return false;
  const int writes = m.writes;
  LinearCalibration restored;
  if (st::load(m, restored) != st::LoadResult::Loaded || writes != m.writes || m.outOfBounds)
    return false;
  float a = 0, b = 0;
  return source.position(2050, a) && restored.position(2050, b) && close(a, b) &&
         restored.zero().count == 50 && restored.span().count == 50 &&
         restored.zero().low == source.zero().low && restored.span().high == source.span().high &&
         close(restored.distance(), 50);
}
constexpr bool blankStorage() {
  FakeMemory m;
  auto c = example();
  return st::load(m, c) == st::LoadResult::Empty && !c.ready() && !c.zeroKnown() && m.writes == 0;
}
constexpr bool shortStorage() {
  FakeMemory m;
  --m.capacity;
  auto c = example();
  return !st::save(m, c) && !st::invalidate(m) && st::load(m, c) == st::LoadResult::NoSpace &&
         !c.ready() && !m.outOfBounds && m.writes == 0;
}
constexpr bool resetSurvivesRestart() {
  FakeMemory m;
  auto c = example();
  if (!st::save(m, c) || !st::invalidate(m))
    return false;
  auto restarted = example(); // Stale RAM must also be cleared on load.
  return st::load(m, restarted) == st::LoadResult::Empty && !restarted.ready() &&
         !restarted.zeroKnown();
}
constexpr bool allSingleBitCorruptionRejected() {
  st::Record valid;
  st::encode(example(), valid);
  for (size_t i = 0; i < st::SIZE; ++i) {
    for (uint8_t bit = 0; bit < 8; ++bit) {
      auto corrupt = valid;
      corrupt.bytes[i] ^= 1U << bit;
      auto out = example();
      if (st::decode(corrupt, out) || out.ready() || out.zeroKnown())
        return false;
    }
  }
  return true;
}
constexpr bool rejectField(size_t offset, uint32_t value, size_t size) {
  st::Record r;
  st::encode(example(), r);
  st::put(r, offset, value, size);
  st::put(r, 36, st::checksum(r), 4); // Even with a valid CRC, invalid semantics are rejected.
  auto c = example();
  return !st::decode(r, c) && !c.ready() && !c.zeroKnown();
}
constexpr bool interruptedSave(int budget) {
  FakeMemory m;
  m.budget = budget;
  auto source = example();
  const bool saved = st::save(m, source);
  LinearCalibration reboot;
  const auto result = st::load(m, reboot);
  return !m.outOfBounds && (saved ? result == st::LoadResult::Loaded && reboot.ready()
                                  : result != st::LoadResult::Loaded && !reboot.ready());
}
constexpr bool allWriteCutPoints() {
  for (int budget = 0; budget <= 42; ++budget)
    if (!interruptedSave(budget))
      return false;
  return true;
}
constexpr bool failedInvalidatePreservesDataButReportsFailure() {
  FakeMemory m;
  auto c = example();
  st::save(m, c);
  m.budget = 0;
  return !st::invalidate(m) && !st::save(m, c) &&
         st::load(m, c) == st::LoadResult::Loaded; // Caller must warn old_record_may_remain.
}
constexpr bool cancelRecalibrationLeavesNoOldRecord() {
  FakeMemory m;
  auto c = example();
  if (!st::save(m, c) || !st::invalidate(m))
    return false;
  c.reset();
  PointCapture capture;
  capture.start(0);
  capture.add(1000, 10);
  capture.cancel(); // STOP, USB loss or a capture error do not write a new record.
  LinearCalibration reboot;
  return st::load(m, reboot) == st::LoadResult::Empty && !reboot.ready();
}
constexpr bool incompleteNeverSaved() {
  FakeMemory m;
  LinearCalibration c;
  if (st::save(m, c))
    return false;
  c.setZero(CalPoint{1000, 999, 1001, 50});
  return !st::save(m, c) && m.writes == 0;
}
constexpr bool repeatedReadsDoNotWear() {
  FakeMemory m;
  auto c = example();
  st::save(m, c);
  const int writes = m.writes;
  for (int i = 0; i < 20; ++i)
    if (st::load(m, c) != st::LoadResult::Loaded)
      return false;
  return m.writes == writes;
}
constexpr bool fractionalDistance() {
  FakeMemory m;
  auto c = example();
  c.setSpan(c.span(), 25.123456f);
  if (!st::save(m, c))
    return false;
  LinearCalibration loaded;
  return st::load(m, loaded) == st::LoadResult::Loaded && close(loaded.distance(), 25.123456f);
}
constexpr bool invalidSlotReported() {
  FakeMemory m;
  m.bytes[0] = st::COMMITTED;
  auto c = example();
  return st::load(m, c) == st::LoadResult::Invalid && !c.ready() && m.writes == 0;
}
constexpr bool mainFirmwareConsumesBenchRecord() {
  FakeMemory m;
  auto c = example(true);
  if (!st::save(m, c))
    return false;
  const int writes = m.writes;
  PersistedLinearSensor firmware;
  firmware.begin(m);
  float mm = 123;
  return firmware.calibrated() && firmware.storageState() == st::LoadResult::Loaded &&
         firmware.position(4060, mm) && close(mm, .2548886f) && firmware.within(mm) &&
         m.writes == writes;
}
constexpr bool mainMissingOrBad(bool corrupt) {
  FakeMemory m;
  if (corrupt) {
    auto c = example();
    st::save(m, c);
    m.bytes[12] ^= 1;
  }
  PersistedLinearSensor firmware;
  firmware.begin(m);
  float mm = 123;
  return !firmware.calibrated() && !firmware.position(1000, mm) && mm == 123;
}
constexpr bool mainRailRejected() {
  FakeMemory m;
  auto c = example();
  st::save(m, c);
  PersistedLinearSensor firmware;
  firmware.begin(m);
  float mm = 123;
  return !firmware.position(0, mm) && !firmware.position(4095, mm) && !firmware.position(-1, mm) &&
         mm == 123;
}
constexpr bool mainDoesNotRezeroOnBoot() {
  FakeMemory m;
  auto c = example(true);
  st::save(m, c);
  PersistedLinearSensor firmware;
  firmware.begin(m);
  float mm = 0;
  return firmware.position(2060, mm) && mm > 24 && mm < 26;
}
static_assert(st::ADDRESS == 4096 && st::SIZE == 40, "Only reserved EEPROM range");
static_assert(roundTrip(false), "Forward calibration restored, no writes on boot");
static_assert(roundTrip(true), "User reverse-direction ADC means restored");
static_assert(blankStorage(), "Blank EEPROM does not fabricate calibrated position");
static_assert(shortStorage(), "No out-of-bounds writes or reads");
static_assert(resetSurvivesRestart(), "CAL_RESET must invalidate persistent calibration");
static_assert(allSingleBitCorruptionRejected(), "Reject each of 320 single-bit corruptions");
static_assert(rejectField(1, 0, 4), "Wrong record magic");
static_assert(rejectField(5, 2, 1), "Unknown storage version");
static_assert(rejectField(6, 41, 1), "Wrong pin");
static_assert(rejectField(7, 10, 1), "Wrong ADC resolution");
static_assert(rejectField(8, 0, 4), "Wrong sensor/config profile");
static_assert(rejectField(20, 49, 2), "Incomplete zero capture");
static_assert(rejectField(26, 0, 2), "Incomplete span capture");
static_assert(rejectField(12, 0xFFFFFFFFUL, 4), "Out-of-range raw mean");
static_assert(rejectField(28, 0, 4), "Span mean outside min/max");
static_assert(rejectField(16, 0, 2), "Rail capture not restored");
static_assert(rejectField(32, 0, 4), "Zero distance not restored");
static_assert(rejectField(32, 76000000, 4), "Out-of-range distance not restored");
static_assert(allWriteCutPoints(), "Byte-write interruptions cannot accept a partial new record");
static_assert(failedInvalidatePreservesDataButReportsFailure(),
              "Failed erase must not claim success");
static_assert(cancelRecalibrationLeavesNoOldRecord(),
              "Cancelled calibration cannot resurrect old data");
static_assert(incompleteNeverSaved(), "No writes for uncalibrated/zero-only data");
static_assert(repeatedReadsDoNotWear(), "No periodic or reconnect writes");
static_assert(fractionalDistance(), "Fractional measured distances persist");
static_assert(invalidSlotReported(), "Corrupt records rejected without modifying EEPROM");
static_assert(mainFirmwareConsumesBenchRecord(),
              "Main reader consumes standalone saved calibration");
static_assert(mainMissingOrBad(false), "Main omits missing calibration, no fabricated zero");
static_assert(mainMissingOrBad(true), "Main rejects corrupt calibration");
static_assert(mainRailRejected(), "Main does not replace rail/invalid ADC with zero");
static_assert(mainDoesNotRezeroOnBoot(), "Current boot position never becomes the new zero");

int main() { return 0; }
