#ifndef SAVER_SELECTABLE_PPG_H
#define SAVER_SELECTABLE_PPG_H
#include "bench_io.h"
#include "src/optical_window.h"
#include "src/ppg_signal.h"
#include <spo2_algorithm.h>

namespace selectable {
namespace optical = saver_max_bench;
class Ppg {
public:
  explicit Ppg(Log &log) : log_(log), bus_(Wire, 0x57) {}
  bool heartRate(float &v) const {
    v = window_.hr();
    return window_.hrOk();
  }
  bool saturation(float &v) const {
    v = window_.spo2();
    return window_.spo2Ok();
  }
  bool stop() {
    window_.reset();
    confirmation_.reset();
    state_ = State::Off;
    if (!touched_)
      return true;
    // Confirm shutdown bit, not just an I2C ACK. Power removal needs hardware.
    uint8_t mode = 0;
    shutdownOk_ = bus_.write(0x09, 0x80) && bus_.read(0x09, &mode, 1) && (mode & 0x80);
    if (shutdownOk_)
      touched_ = false;
    else
      log_.emit("ERROR,PPG_SHUTDOWN_UNVERIFIED,disconnect_sensor_power_before_motors=1");
    return shutdownOk_;
  }
  bool quiet() const { return !touched_ && shutdownOk_; }
  void enter() {
    window_.reset();
    confirmation_.reset();
    reason_ = "INITIALIZING";
    Wire.setSDA(18);
    Wire.setSCL(19);
    Wire.begin();
    Wire.setClock(100000);
    uint8_t part = 0;
    if (!bus_.read(0xFF, &part, 1) || part != 0x15) {
      // An absent, never-started sensor must not lock the other two modules.
      state_ = State::Failed;
      reason_ = "PPG_NOT_FOUND";
      log_.emit("ERROR,%s", reason_);
      return;
    }
    touched_ = true;
    shutdownOk_ = false;
    if (!bus_.write(0x09, 0x40)) {
      fail("PPG_RESET_FAILED");
      return;
    }
    state_ = State::Reset;
    since_ = millis();
  }
  void tick() {
    window_.expire(millis());
    if (!window_.contactReady())
      confirmation_.reset();
    if (state_ == State::Reset) {
      uint8_t mode = 0;
      if (!bus_.read(0x09, &mode, 1)) {
        fail("PPG_RESET_READ_FAILED");
        return;
      }
      if (mode & 0x40) {
        if (uint32_t(millis() - since_) >= 150)
          fail("PPG_RESET_TIMEOUT");
        return;
      }
      state_ = State::Configure;
      step_ = 0;
    }
    if (state_ == State::Configure) {
      constexpr uint8_t regs[] = {0x09, 0x02, 0x03, 0x04, 0x05, 0x06, 0x08, 0x0A, 0x0C, 0x0D, 0x09};
      constexpr uint8_t vals[] = {0x80, 0x80, 0x00, 0, 0, 0, 0x4F, 0x27, 60, 60, 0x03};
      if (!bus_.write(regs[step_], vals[step_])) {
        fail("PPG_CONFIG_WRITE_FAILED");
        return;
      }
      if (++step_ == sizeof(regs)) {
        uint8_t config[3] = {}, led[2] = {}, status[2] = {};
        if (!bus_.read(0x08, config, 3) || !bus_.read(0x0C, led, 2) ||
            !bus_.read(0x00, status, 2) || config[0] != 0x4F || config[1] != 3 ||
            config[2] != 0x27 || led[0] != 60 || led[1] != 60) {
          fail("PPG_CONFIG_READBACK_FAILED");
          return;
        }
        state_ = State::Running;
        sampleAt_ = pollAt_ = millis();
        reason_ = "WARMUP";
        log_.emit("OK,PPG_READY,estimates=UNVALIDATED_BENCH_ONLY");
      }
      return;
    }
    if (state_ != State::Running || uint32_t(millis() - pollAt_) < 10)
      return;
    pollAt_ = millis();
    uint8_t status[2] = {}, ptr[3] = {}, mode = 0;
    if (!bus_.read(0x00, status, 2) || !bus_.read(0x04, ptr, 3) || !bus_.read(0x09, &mode, 1)) {
      fail("PPG_I2C_READ_FAILED");
      return;
    }
    if (mode != 3 || (status[0] & 1)) {
      fail("PPG_RESET_DETECTED");
      return;
    }
    if ((status[0] & 0xA0) || (ptr[1] & 31)) {
      fail("PPG_FIFO_OR_AMBIENT_OVERFLOW");
      return;
    }
    uint8_t count = ((ptr[0] & 31) - (ptr[2] & 31)) & 31;
    if (count > 16) {
      fail("PPG_BACKLOG");
      return;
    }
    if (!count) {
      if (uint32_t(millis() - sampleAt_) >= 1000)
        fail("PPG_SAMPLE_TIMEOUT");
      return;
    }
    if (count > 8)
      count = 8;
    uint8_t bytes[48] = {};
    if (!bus_.read(0x07, bytes, count * 6)) {
      fail("PPG_SHORT_FIFO_READ");
      return;
    }
    sampleAt_ = millis();
    for (uint8_t i = 0; i < count; ++i) {
      const uint8_t *p = bytes + i * 6;
      red_ = ((uint32_t(p[0]) << 16) | (uint32_t(p[1]) << 8) | p[2]) & optical::ADC_MAX;
      ir_ = ((uint32_t(p[3]) << 16) | (uint32_t(p[4]) << 8) | p[5]) & optical::ADC_MAX;
      if (window_.push(red_, ir_, millis()))
        calculate();
      if (!window_.contactReady())
        confirmation_.reset();
    }
  }
  void report() {
    log_.emit("PPG,acquiring=%u,contact=%s,window_samples=%u,hr_valid=%u,spo2_valid=%u,reason=%s",
              state_ == State::Running, window_.contactName(), window_.count(), window_.hrOk(),
              window_.spo2Ok(), reason_);
    if (state_ == State::Running && uint32_t(millis() - sampleAt_) < 200)
      log_.emit("OPTICAL,red_raw=%lu,ir_raw=%lu", static_cast<unsigned long>(red_),
                static_cast<unsigned long>(ir_));
    if (window_.hrOk())
      log_.emit("ESTIMATE,heart_rate_bpm=%ld,clinical_validated=0",
                static_cast<long>(window_.hr()));
    if (window_.spo2Ok())
      log_.emit("ESTIMATE,spo2_pct=%ld,clinical_validated=0", static_cast<long>(window_.spo2()));
  }

private:
  enum class State { Off, Reset, Configure, Running, Failed };
  void fail(const char *why) {
    stop();
    state_ = State::Failed;
    reason_ = why;
    log_.emit("ERROR,%s", why);
  }
  void calculate() {
    const auto q =
        optical::inspectPulse(window_.redData(), window_.irData(), optical::WINDOW_SAMPLES);
    const bool confirmed = confirmation_.accept(q);
    int32_t hr = 0, spo2 = 0;
    int8_t hrValid = 0, spo2Valid = 0;
    if (confirmed) {
      uint32_t red[100], ir[100];
      optical::prepareLegacyWindow(window_.redData(), window_.irData(), red, ir, 100);
      maxim_heart_rate_and_oxygen_saturation(ir, 100, red, &spo2, &spo2Valid, &hr, &hrValid);
    }
    const bool accepted = confirmed && hrValid && optical::legacyPeriodAgrees(hr, q) && spo2Valid &&
                          spo2 >= 0 && spo2 <= 100;
    window_.completeCalculation(q.hrBpm, confirmed, spo2, accepted);
    reason_ = confirmed ? "PERIODIC_ESTIMATE_NOT_CLINICAL" : "SIGNAL_NOT_CONFIRMED";
  }
  Log &log_;
  RegisterBus<TwoWire> bus_;
  optical::OpticalWindow window_;
  optical::PulseConfirmation confirmation_;
  State state_ = State::Off;
  bool touched_ = false, shutdownOk_ = true;
  uint8_t step_ = 0;
  uint32_t since_ = 0, pollAt_ = 0, sampleAt_ = 0, red_ = 0, ir_ = 0;
  const char *reason_ = "OFF";
};
} // namespace selectable
#endif
