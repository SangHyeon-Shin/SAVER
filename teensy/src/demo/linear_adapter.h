#ifndef SAVER_DEMO_LINEAR_ADAPTER_H
#define SAVER_DEMO_LINEAR_ADAPTER_H
#include "src/current_control.h"
#include "../saver_linear/linear_calibration_store.h"
namespace selectable {
// EEPROM 4096..4135 is read-only here. ZERO/CAL overrides only RAM.
class LinearAdapter {
public:
  template <class Memory> void load(Memory &m) {
    saved_ = saver_linear::storage::load(m, stored_) == saver_linear::storage::LoadResult::Loaded;
  }
  void reset() {
    ram_.reset();
    saved_ = false;
  }
  bool zero(int raw) {
    saved_ = false;
    return ram_.zero(raw);
  }
  bool calibrate(int raw, float mm) { return ram_.calibrate(raw, mm); }
  bool calibrated() const { return saved_ ? stored_.ready() : ram_.calibrated(); }
  bool zeroKnown() const { return saved_ ? stored_.zeroKnown() : ram_.zeroKnown(); }
  bool covers(float mm) const {
    return saved_ ? mm > 0 && mm < stored_.distance() : ram_.covers(mm);
  }
  bool position(int raw, float &mm) const {
    return saved_ ? stored_.position(raw, mm) : ram_.position(raw, mm);
  }

private:
  bool saved_ = false;
  saver_linear::LinearCalibration stored_;
  saver_current::Calibration ram_;
};
} // namespace selectable
#endif
