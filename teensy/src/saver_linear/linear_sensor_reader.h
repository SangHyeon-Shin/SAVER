#ifndef SAVER_PERSISTED_LINEAR_SENSOR_READER_H
#define SAVER_PERSISTED_LINEAR_SENSOR_READER_H

#include "linear_calibration_store.h"

namespace saver_linear {
// Read-only consumer: no EEPROM writes, Serial, motor commands or homing.
// Current sensor position is NOT the protocol's last-cycle peak cp_depth_mm.
class PersistedLinearSensor {
public:
  template <class Memory> constexpr void begin(Memory &memory) {
    state_ = storage::load(memory, calibration_);
  }
  constexpr bool position(int raw, float &mm) const { return calibration_.position(raw, mm); }
  constexpr bool within(float mm) const { return calibration_.within(mm); }
  constexpr bool calibrated() const { return calibration_.ready(); }
  constexpr storage::LoadResult storageState() const { return state_; }

private:
  LinearCalibration calibration_;
  storage::LoadResult state_ = storage::LoadResult::Empty;
};
} // namespace saver_linear
#endif
