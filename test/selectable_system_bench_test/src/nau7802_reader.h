#ifndef SAVER_NAU7802_READER_H
#define SAVER_NAU7802_READER_H
#include "logger_logic.h"

namespace reaction {
// Register-level CH1 reader based on Nuvoton NAU7802 Rev2.6, not a scale library.
// Bus must implement bool read(reg, bytes, length) and bool write(reg, value).
// Each tick does bounded work. Bus transaction timeout is the transport's responsibility.
class Nau7802Reader {
public:
  enum class State {
    Reset,
    ResetWait,
    PowerWait,
    Configure,
    Settle,
    Calibrate,
    Discard,
    Ready,
    Failed
  };
  enum class Result { None, Sample, Error };
  constexpr void begin(uint32_t now) {
    state_ = State::Reset;
    since_ = now;
    step_ = 0;
    discarded_ = 0;
    error_ = "NONE";
  }
  constexpr bool ready() const { return state_ == State::Ready; }
  constexpr bool failed() const { return state_ == State::Failed; }
  constexpr const char *error() const { return error_; }
  template <typename Bus> constexpr Result tick(Bus &bus, uint32_t now, int32_t &raw) {
    uint8_t v = 0;
    switch (state_) {
    case State::Reset:
      if (!bus.write(0x00, 0x01))
        return fail("I2C_RESET_FAILED");
      state_ = State::ResetWait;
      since_ = now;
      break;
    case State::ResetWait:
      if (uint32_t(now - since_) < 2)
        break;
      if (!bus.write(0x00, 0x00) || !bus.write(0x00, 0x06))
        return fail("I2C_POWER_FAILED");
      state_ = State::PowerWait;
      since_ = now;
      break;
    case State::PowerWait:
      if (!bus.read(0x00, &v, 1))
        return fail("I2C_POWER_STATUS_FAILED");
      if (v & 0x08) {
        state_ = State::Configure;
        step_ = 0;
      } else if (uint32_t(now - since_) >= 100)
        return fail("POWER_READY_TIMEOUT");
      break;
    case State::Configure: {
      // 3.3 V module input assumed; internal 3.0 V LDO gives headroom.
      // Verify actual module AVDD/E+/REFP wiring before use. CH2 not used.
      constexpr uint8_t regs[] = {0x01, 0x00, 0x02, 0x15, 0x1B, 0x1C};
      constexpr uint8_t vals[] = {0x2F, 0x96, 0x70, 0x30, 0x00, 0x80};
      if (!bus.write(regs[step_], vals[step_]))
        return fail("I2C_CONFIG_FAILED");
      if (++step_ == sizeof(regs)) {
        state_ = State::Settle;
        since_ = now;
      }
      break;
    }
    case State::Settle:
      if (uint32_t(now - since_) < 250)
        break;
      if (!bus.read(0x01, &v, 1) || (v & 0x3F) != 0x2F)
        return fail("GAIN_LDO_READBACK_FAILED");
      if (!bus.read(0x02, &v, 1) || (v & 0xF0) != 0x70)
        return fail("RATE_READBACK_FAILED");
      if (!bus.write(0x02, 0x74))
        return fail("AFE_CAL_START_FAILED");
      state_ = State::Calibrate;
      since_ = now;
      break;
    case State::Calibrate:
      if (!bus.read(0x02, &v, 1))
        return fail("AFE_CAL_READ_FAILED");
      if (v & 0x04) {
        if (uint32_t(now - since_) >= 1000)
          return fail("AFE_CAL_TIMEOUT");
      } else if (v & 0x08)
        return fail("AFE_CAL_FAILED");
      else {
        state_ = State::Discard;
        since_ = now;
      }
      break;
    case State::Discard:
    case State::Ready: {
      if (!bus.read(0x00, &v, 1))
        return fail("I2C_READY_READ_FAILED");
      if (!(v & 0x20)) {
        if (uint32_t(now - since_) >= 100)
          return fail("SAMPLE_TIMEOUT");
        break;
      }
      uint8_t bytes[3] = {};
      if (!bus.read(0x12, bytes, 3))
        return fail("I2C_SHORT_OR_FAILED_SAMPLE");
      since_ = now;
      if (state_ == State::Discard) {
        if (++discarded_ == 8)
          state_ = State::Ready;
        break;
      }
      raw = decode24(bytes[0], bytes[1], bytes[2]);
      return Result::Sample;
    }
    case State::Failed:
      break;
    }
    return Result::None;
  }

private:
  constexpr Result fail(const char *why) {
    state_ = State::Failed;
    error_ = why;
    return Result::Error;
  }
  State state_ = State::Reset;
  uint32_t since_ = 0;
  uint8_t step_ = 0, discarded_ = 0;
  const char *error_ = "NONE";
};
} // namespace reaction
#endif
