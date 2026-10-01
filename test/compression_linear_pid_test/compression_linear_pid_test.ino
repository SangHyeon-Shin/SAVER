/*
 * Teensy 4.1 / VESC / 75 mm linear sensor: UNLOADED FIXTURE BENCH ONLY.
 * Open this .ino in Arduino IDE (Teensy 4.1, USB Serial); keep all local .h files.
 * Ported from user's working lm/sketch_sep16a: position -> CURRENT PID.
 * bench_config.h defaults to LM's 5 mm / 10 CPM, with all unverified locks retained.
 * Currently: raw/nominal sensor display, VESC query and no-drive PREVIEW are usable.
 * Missing limits, E-stop, calibration and PID settings MUST NOT be bypassed.
 * Not Jetson JSON firmware, not clinical CPR, no force control or automatic homing.
 *
 * VESC COMM TX -> 28/RX7, RX <- 29/TX7, signal GND -> Teensy GND.
 * VESC 5V/3.3V supply pins: DISCONNECTED. Motor phase/Hall wires go to VESC.
 * Linear conditioned OUT -> A16/40 (not digital 16), signal GND -> GND.
 * Every Teensy input must stay 0..3.3 V. Sensor supply/pinout need its datasheet.
 * Reserved: upper limit 3, lower limit 4, physical E-stop auxiliary contact 6.
 * Other motor power/control wiring must be disconnected; not initialized here.
 *
 * STOP/fault/end -> SET_CURRENT(0), NOT holding torque/brake/verified standstill.
 * A vertical axis must not rely on that request to hold up its own weight.
 * Physical power cut, travel stops, guarding, VESC timeout/regeneration protection
 * are independent requirements. No UART packet/config writes can provide them.
 */
#include <Arduino.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "bench_guard.h"
#include "calibration_capture.h"
#include "vesc_link.h"

#if !defined(ARDUINO_TEENSY41)
#error "Select Teensy 4.1 / USB Type Serial"
#endif

namespace {
namespace cfg = saver_depth_config;
namespace depth = saver_depth;
namespace vesc = saver_pid_vesc;
using namespace saver_depth_bench;

// Kept standalone for Arduino sketch copying; tests compare against teensy/pinmap.h.
constexpr uint8_t PIN_CP_UART_RX = 28;     // Serial7 receive from VESC TX.
constexpr uint8_t PIN_CP_UART_TX = 29;     // Serial7 transmit to VESC RX.
constexpr uint8_t PIN_LINEAR_SENSOR = A16; // Physical 40, existing linear sensor ADC.
constexpr uint8_t PIN_LIMIT_TOP = 3;       // Reserved top-limit input; user has not installed it.
constexpr uint8_t PIN_LIMIT_BOTTOM = 4;    // Reserved lower-limit input, not a motor output.
constexpr uint8_t PIN_ESTOP = 6; // Read-only auxiliary contact; physical cut is elsewhere.
constexpr uint32_t QUERY_MS = 50, PROBE_MS = 1000, REPORT_MS = 50;
constexpr uint32_t COMMAND_TIMEOUT_MS = 1000;
static_assert(A16 == 40, "A16 is physical pin 40");
static_assert(cfg::UART_BAUD >= 9600 && cfg::UART_BAUD <= 1000000, "UART baud range");

class DepthBench {
public:
  void begin() {
    pinMode(PIN_LINEAR_SENSOR, INPUT_DISABLE);
    analogReadResolution(12);
    analogReadAveraging(16);
    // No input is declared normal simply because a pull-up reads HIGH.
    // Once wiring is verified, use dry/conditioned 3.3 V contacts with known bias.
    pinMode(PIN_LIMIT_TOP, cfg::LIMIT_SWITCHES_INSTALLED ? INPUT_PULLUP : INPUT);
    pinMode(PIN_LIMIT_BOTTOM, cfg::LIMIT_SWITCHES_INSTALLED ? INPUT_PULLUP : INPUT);
    pinMode(PIN_ESTOP, cfg::PHYSICAL_ESTOP_AND_INPUT_VERIFIED ? INPUT_PULLUP : INPUT);
    Serial7.setRX(PIN_CP_UART_RX);
    Serial7.setTX(PIN_CP_UART_TX);
    Serial7.begin(cfg::UART_BAUD, SERIAL_8N1);
    Serial.begin(115200);
    requestStop();
    sampleAt_ = micros();
    lastQuery_ = millis() - QUERY_MS;
    lastProbe_ = millis() - PROBE_MS;
  }

  void tick() {
    syncUsb();
    // ADC + reference + PID do not print or send UART inside the control tick.
    const char *issue = controlTick();
    if (issue)
      fail(issue);
    checkSafety(); // Record a limit/E-stop/link fault even on the final profile tick.
    if (profileEnded_) {
      profileEnded_ = false;
      requestStop();
      reason_ = "PROFILE_ENDED";
      emit("EVENT,PROFILE_ENDED,requested_cycles=%lu,tracking_success_verified=0,standstill_"
           "verified=0",
           static_cast<unsigned long>(cycles_));
    }
    checkSafety();
    pollVesc();
    checkSafety();
    if (usbReady_)
      pollUsb();
    checkSafety();
    serviceTx();
    serviceCommissioning(); // Bounded diagnostic output, outside ADC/PID and stop handling.
    if (usbReady_ && static_cast<uint32_t>(millis() - reportAt_) >= REPORT_MS) {
      reportAt_ = millis();
      report();
    }
  }

private:
  RunGate gate_;
  CalibrationCapture capture_;
  struct CapturedPoint {
    bool present = false, rail = false, invalid = false;
    float mm = 0, meanRaw = 0;
    int minRaw = 0, maxRaw = 0;
    uint16_t count = 0;
  } captured_[2];
  bool captureReported_ = false, captureCancelledNotice_ = false;
  uint8_t nextCapture_ = 0, checkIndex_ = CONFIG_CHECK_COUNT + 1;
  uint32_t checkAt_ = 0;
  saver_current::Pid pid_;
  saver_current::PositionFilter filter_;
  saver_current::Calibration calibration_;
  saver_current::Gains gains_ = cfg::CURRENT_PID;
  depth::Position position_;
  depth::Reference reference_ = {0, 0, true};
  vesc::PacketParser parser_;
  vesc::Values values_;
  bool usbReady_ = false, firmwareKnown_ = false, valuesKnown_ = false;
  bool fwPending_ = false, valuesPending_ = false, zeroPending_ = true;
  bool originKnown_ = false, preview_ = false, sampleSeen_ = false;
  bool profileEnded_ = false, healthy_ = false, resting_ = false;
  float originMm_ = 0, targetDepth_ = cfg::DEFAULT_DEPTH_MM, targetRate_ = cfg::DEFAULT_RATE_CPM;
  float measuredVelocity_ = 0, filteredMm_ = 0, commandCurrentA_ = 0;
  float currentLimitA_ = cfg::DEFAULT_COMMAND_CURRENT_A, inputTripA_ = cfg::DEFAULT_INPUT_TRIP_A;
  int8_t motorSign_ = cfg::CURRENT_SIGN_FOR_INCREASING_MM;
  uint32_t lastCurrentTx_ = 0;
  uint32_t cycles_ = 1, startedUs_ = 0, sampleAt_ = 0, healthyAt_ = 0, restingAt_ = 0;
  uint32_t lastValues_ = 0, fwRequested_ = 0, valuesRequested_ = 0;
  uint32_t lastQuery_ = 0, lastProbe_ = 0, lastZero_ = 0, reportAt_ = 0;
  uint32_t sentControlAt_ = 0, controlAt_ = 0;
  uint32_t badPackets_ = 0, droppedLogs_ = 0, driveQueued_ = 0, zeroQueued_ = 0;
  uint8_t fwMajor_ = 0, fwMinor_ = 0;
  const char *reason_ = "BOOT_LOCKED";
  char line_[80] = {};
  uint8_t lineUsed_ = 0;
  bool discardLine_ = false;
  uint32_t lineStarted_ = 0;

  bool emit(const char *format, ...) {
    char text[384];
    va_list args;
    va_start(args, format);
    const int n = vsnprintf(text, sizeof(text) - 1, format, args);
    va_end(args);
    if (n < 0 || static_cast<size_t>(n) >= sizeof(text) - 1 || !Serial ||
        Serial.availableForWrite() < n + 1) {
      ++droppedLogs_;
      return false;
    }
    text[n] = '\n';
    Serial.write(reinterpret_cast<const uint8_t *>(text), n + 1);
    return true;
  }

  bool send(const vesc::Frame &f) {
    return Serial7.availableForWrite() >= static_cast<int>(f.length) &&
           Serial7.write(f.bytes, f.length) == f.length;
  }
  bool queueZero() {
    if (!send(vesc::releaseCommand()))
      return false;
    zeroPending_ = false;
    ++zeroQueued_;
    lastZero_ = millis();
    return true; // UART queued, NOT VESC execution ACK or physical stop.
  }
  void requestStop() {
    if (capture_.active())
      captureCancelledNotice_ = true;
    capture_.cancel();
    gate_.stop();
    preview_ = false;
    profileEnded_ = false;
    commandCurrentA_ = 0;
    pid_.reset(position_.calibrated ? filteredMm_ : 0.0f);
    zeroPending_ = true;
    queueZero(); // Attempt BEFORE reporting; retry has priority over every other TX.
  }
  void fail(const char *why) {
    requestStop();
    gate_.trip();
    originKnown_ = false;
    reason_ = why;
    emit("ERROR,%s,drive_command_active=0,stop_pending=%u", why, zeroPending_ ? 1U : 0U);
  }
  void invalidateLink() {
    firmwareKnown_ = false;
    valuesKnown_ = false;
    fwPending_ = false;
    valuesPending_ = false;
    healthy_ = false;
    parser_.reset();
    lastProbe_ = millis() - PROBE_MS;
  }
  bool valuesFresh() const {
    return firmwareKnown_ && valuesKnown_ &&
           static_cast<uint32_t>(millis() - lastValues_) < vesc::VALUES_STALE_MS;
  }
  bool inputsKnown() const {
    return cfg::LIMIT_SWITCHES_INSTALLED && cfg::PHYSICAL_ESTOP_AND_INPUT_VERIFIED &&
           levelValid(cfg::TOP_ACTIVE_LEVEL) && levelValid(cfg::BOTTOM_ACTIVE_LEVEL) &&
           levelValid(cfg::ESTOP_ACTIVE_LEVEL);
  }
  // Use the same runtime calibration check in CHECK output and drive permission.
  ConfigCheck configCheck(uint8_t index) const {
    return saver_depth_bench::configCheck(index, calibration_.calibrated() && originKnown_);
  }
  const char *configIssue() const {
    for (uint8_t i = 0; i < CONFIG_CHECK_COUNT; ++i)
      if (!configCheck(i).passed)
        return configCheck(i).reason;
    return nullptr;
  }
  const char *runtimeIssue() const {
    if (cfg::PHYSICAL_ESTOP_AND_INPUT_VERIFIED && levelValid(cfg::ESTOP_ACTIVE_LEVEL) &&
        digitalRead(PIN_ESTOP) == cfg::ESTOP_ACTIVE_LEVEL)
      return "PHYSICAL_ESTOP_ACTIVE";
    if (!usbReady_ || !Serial)
      return "USB_NOT_READY";
    if (!valuesFresh())
      return "VESC_VALUES_STALE";
    if (values_.fault)
      return "VESC_REPORTED_FAULT";
    if (!sampleSeen_ || static_cast<uint32_t>(micros() - sampleAt_) >= CONTROL_STALE_US)
      return "POSITION_STALE";
    if (!position_.calibrated)
      return "POSITION_NOT_CALIBRATED_OR_INVALID";
    if (!inputsKnown())
      return "SAFETY_INPUTS_UNKNOWN";
    if (digitalRead(PIN_ESTOP) == cfg::ESTOP_ACTIVE_LEVEL)
      return "PHYSICAL_ESTOP_ACTIVE";
    if (digitalRead(PIN_LIMIT_TOP) == cfg::TOP_ACTIVE_LEVEL)
      return "TOP_LIMIT_ACTIVE";
    if (digitalRead(PIN_LIMIT_BOTTOM) == cfg::BOTTOM_ACTIVE_LEVEL)
      return "BOTTOM_LIMIT_ACTIVE";
    if (position_.mm <= cfg::SOFT_MIN_MM || position_.mm >= cfg::SOFT_MAX_MM)
      return "OUTSIDE_SOFT_TRAVEL";
    const float volts = values_.inputDeciV / 10.0f;
    if (volts < cfg::MIN_INPUT_V || volts > cfg::MAX_INPUT_V)
      return "INPUT_VOLTAGE_LIMIT";
    if (absValue(values_.motorCentiA / 100.0f) > cfg::MAX_MOTOR_CURRENT_A)
      return "MOTOR_CURRENT_LIMIT";
    if (absValue(values_.inputCentiA / 100.0f) > inputTripA_)
      return "INPUT_CURRENT_LIMIT";
    if (values_.fetDeciC / 10.0f > cfg::MAX_FET_TEMP_C)
      return "FET_TEMPERATURE_LIMIT";
    return nullptr;
  }
  const char *runIssue() const {
    if (const char *why = configIssue())
      return why;
    if (const char *why = runtimeIssue())
      return why;
    if (!healthy_ || static_cast<uint32_t>(millis() - healthyAt_) < STABLE_MS)
      return "INPUTS_NOT_STABLE_FOR_250MS";
    if (!originKnown_)
      return "ZERO_REQUIRED";
    if (!resting_ || static_cast<uint32_t>(millis() - restingAt_) < STABLE_MS)
      return "POSITION_NOT_STATIONARY_FOR_250MS";
    if (absValue(position_.mm - originMm_) > cfg::START_TOLERANCE_MM)
      return "NOT_AT_MANUAL_ZERO";
    if (originMm_ <= cfg::SOFT_MIN_MM || originMm_ + targetDepth_ >= cfg::SOFT_MAX_MM)
      return "TARGET_EXCEEDS_SOFT_TRAVEL";
    if (!calibration_.covers(targetDepth_))
      return "TARGET_OUTSIDE_CALIBRATION";
    if (depth::peakVelocity(targetDepth_, targetRate_) > cfg::MAX_PROFILE_VELOCITY_MM_S ||
        depth::peakAcceleration(targetDepth_, targetRate_) > cfg::MAX_PROFILE_ACCELERATION_MM_S2)
      return "PROFILE_EXCEEDS_MOTION_LIMITS";
    if (!saver_current::validGains(gains_) || !positive(currentLimitA_) ||
        currentLimitA_ > cfg::MAX_ALLOWED_COMMAND_CURRENT_A || !positive(inputTripA_) ||
        inputTripA_ > cfg::MAX_ALLOWED_INPUT_TRIP_A || (motorSign_ != 1 && motorSign_ != -1))
      return "CURRENT_SETTINGS_INVALID";
    if (zeroPending_)
      return "STOP_TX_PENDING";
    return nullptr;
  }

  const char *controlTick() {
    const uint32_t now = micros();
    uint32_t elapsed = now - sampleAt_;
    if (elapsed < CONTROL_US)
      return nullptr;
    const depth::Position before = position_;
    position_ = depth::convert(analogRead(PIN_LINEAR_SENSOR), depth::Calibration{});
    position_.rail = !saver_current::Calibration::usableRaw(position_.raw);
    position_.calibrated = calibration_.position(position_.raw, position_.mm);
    capture_.sample(position_.raw, millis()); // Statistics only; never changes control calibration.
    const uint32_t acquiredAt = micros();
    elapsed = acquiredAt - sampleAt_; // Actual sample-to-sample dt includes ADC acquisition.
    sampleAt_ = acquiredAt;
    const bool wasSeen = sampleSeen_;
    sampleSeen_ = true;
    const float dt = elapsed / 1000000.0f;
    if (position_.calibrated)
      filteredMm_ = filter_.step(position_.mm, dt, cfg::POSITION_FILTER_TAU_S);
    else
      filter_.reset();
    measuredVelocity_ = wasSeen && before.calibrated && position_.calibrated
                            ? (position_.mm - before.mm) / dt
                            : 0.0f;
    if (!wasSeen || !before.calibrated || !position_.calibrated || elapsed >= CONTROL_STALE_US ||
        !positive(cfg::MAX_START_VELOCITY_MM_S) ||
        absValue(measuredVelocity_) > cfg::MAX_START_VELOCITY_MM_S) {
      resting_ = false;
    } else if (!resting_) {
      resting_ = true;
      restingAt_ = millis();
    }
    if (gate_.armed() || gate_.running()) {
      if (elapsed >= CONTROL_STALE_US)
        return "CONTROL_DEADLINE_MISSED";
      if (!position_.calibrated)
        return "POSITION_INVALID_OR_ADC_RAIL";
      if (before.calibrated &&
          absValue(position_.mm - before.mm) >
              cfg::MAX_MEASURED_VELOCITY_MM_S * dt + cfg::SAMPLE_NOISE_ALLOWANCE_MM)
        return "POSITION_JUMP";
    }
    if (!gate_.running() && !preview_)
      return nullptr;
    reference_ = depth::reference(preview_ ? 0.0f : originMm_, targetDepth_, targetRate_,
                                  sampleAt_ - startedUs_, cycles_);
    if (reference_.finished) {
      const bool returned =
          position_.calibrated && absValue(position_.mm - originMm_) <= cfg::RETURN_TOLERANCE_MM;
      if (preview_ || returned || !cfg::RETURN_TO_ZERO_ENABLED) {
        if (!preview_ && !returned)
          return "END_POSITION_ERROR";
        profileEnded_ = true;
        commandCurrentA_ = 0;
        return nullptr;
      }
      const double profileUs = static_cast<double>(cycles_) * 60000000.0 / targetRate_;
      if (sampleAt_ - startedUs_ >= profileUs + cfg::RETURN_TIMEOUT_MS * 1000.0)
        return "RETURN_ZERO_TIMEOUT";
      // Optional LM behavior: ACTIVE position correction to zero, NOT zero current.
      reference_.mm = originMm_;
    }
    // Permit normal completion at the exact time cap, but never another drive tick.
    // Wall-clock run cap includes the optional active return tail.
    if (!preview_ && (sampleAt_ - startedUs_) / 1000U >= cfg::MAX_RUN_MS)
      return "RUN_TIME_LIMIT";
    if (preview_)
      return nullptr; // Reference-only path never calls PID or commands nonzero current.
    if (absValue(reference_.mm - position_.mm) > cfg::MAX_FOLLOWING_ERROR_MM)
      return "FOLLOWING_ERROR";
    const float logicalA = pid_.step(reference_.mm, filteredMm_, dt, gains_, currentLimitA_);
    if (!pid_.valid() || !depth::finite(logicalA))
      return "PID_INVALID";
    commandCurrentA_ = logicalA * motorSign_;
    if (!depth::finite(commandCurrentA_) || absValue(commandCurrentA_) > currentLimitA_)
      return "CURRENT_COMMAND_OUT_OF_RANGE";
    controlAt_ = sampleAt_;
    return nullptr;
  }

  void checkSafety() {
    const uint32_t now = millis();
    const char *why = runtimeIssue();
    if (why)
      healthy_ = false;
    else if (!healthy_) {
      healthy_ = true;
      healthyAt_ = now;
    }
    if ((gate_.armed() || gate_.running()) && why)
      fail(why);
    if (gate_.expire(now)) {
      requestStop();
      reason_ = "ARM_EXPIRED";
    }
    if (parser_.expire(now))
      badPacket("VESC_FRAME_TIMEOUT");
    if (fwPending_ && now - fwRequested_ >= vesc::VALUES_STALE_MS)
      fwPending_ = false;
    if (valuesPending_ && now - valuesRequested_ >= vesc::VALUES_STALE_MS)
      valuesPending_ = false;
    if (valuesKnown_ && !valuesFresh()) {
      requestStop();
      originKnown_ = false;
      invalidateLink();
      reason_ = "VESC_RECONNECT_REQUIRED";
    }
    if (lineUsed_ && now - lineStarted_ >= COMMAND_TIMEOUT_MS) {
      lineUsed_ = 0;
      discardLine_ = true;
      fail("USB_COMMAND_TIMEOUT");
    }
  }
  void syncUsb() {
    if (!Serial) {
      if (usbReady_) {
        fail("USB_DISCONNECTED");
        invalidateLink();
      }
      usbReady_ = false;
      checkIndex_ = CONFIG_CHECK_COUNT + 1;
      lineUsed_ = 0;
      discardLine_ = false;
      return;
    }
    if (usbReady_)
      return;
    for (uint8_t i = 0; i < 32 && Serial.available(); ++i)
      Serial.read();
    if (Serial.available())
      return; // Drain stale queued commands across session boundary.
    requestStop();
    captured_[0] = CapturedPoint{};
    captured_[1] = CapturedPoint{};
    nextCapture_ = 0; // A new USB session must not reuse measurements from old wiring/session.
    captureCancelledNotice_ = false;
    originKnown_ = false;
    calibration_.reset();
    filter_.reset();
    position_.calibrated = false;
    resting_ = false;
    motorSign_ = cfg::CURRENT_SIGN_FOR_INCREASING_MM;
    invalidateLink();
    lineUsed_ = 0;
    discardLine_ = false;
    usbReady_ = true;
    emit("SAVER_COMPRESSION_LINEAR_PID_TEST_READY,build=lm_current_v3,auto_start=0");
    help();
  }
  void badPacket(const char *why) {
    ++badPackets_;
    requestStop();
    gate_.trip();
    originKnown_ = false;
    reason_ = why;
    invalidateLink(); // Reprobe and require manual clear, ZERO and ARM. No automatic resume.
  }
  void pollVesc() {
    for (uint8_t i = 0; i < 64 && Serial7.available(); ++i) {
      const auto result = parser_.push(static_cast<uint8_t>(Serial7.read()), millis());
      if (result == vesc::PacketResult::Invalid)
        badPacket("VESC_BAD_FRAME");
      else if (result == vesc::PacketResult::Ready)
        handlePacket();
    }
  }
  void handlePacket() {
    const uint8_t *p = parser_.data();
    const size_t n = parser_.size();
    const uint32_t now = millis();
    if (!n)
      return;
    if (p[0] == vesc::FW_VERSION) {
      if (!fwPending_ || now - fwRequested_ >= vesc::VALUES_STALE_MS)
        return;
      fwPending_ = false;
      bool terminated = false;
      for (size_t i = 3; i < n; ++i)
        if (!p[i]) {
          terminated = true;
          break;
        }
      if (n < 5 || !terminated || !vesc::supportedFirmware(p[1])) {
        badPacket("VESC_UNSUPPORTED_FIRMWARE");
        return;
      }
      fwMajor_ = p[1];
      fwMinor_ = p[2];
      firmwareKnown_ = true;
      emit("VESC_FW,major=%u,minor=%u", static_cast<unsigned>(fwMajor_),
           static_cast<unsigned>(fwMinor_));
    } else if (p[0] == vesc::GET_VALUES) {
      if (!firmwareKnown_ || !valuesPending_ || now - valuesRequested_ >= vesc::VALUES_STALE_MS)
        return;
      valuesPending_ = false;
      vesc::Values decoded;
      if (!vesc::decodeValues(p, n, decoded)) {
        badPacket("VESC_BAD_VALUES");
        return;
      }
      values_ = decoded;
      valuesKnown_ = true;
      lastValues_ = now;
      if (decoded.fault && !gate_.fault())
        fail("VESC_REPORTED_FAULT");
    }
  }
  void serviceTx() {
    const uint32_t now = millis();
    if (zeroPending_) {
      queueZero();
      return;
    }
    if (gate_.running() && controlAt_ != sentControlAt_ &&
        now - lastCurrentTx_ >= cfg::CURRENT_TX_MS) {
      // Only a fresh successful tick can produce current. LM cadence: 50 Hz UART.
      if (configIssue() || runtimeIssue() || micros() - controlAt_ >= CONTROL_STALE_US) {
        fail("DRIVE_PRECONDITION_LOST");
        return;
      }
      const int32_t milliAmps = static_cast<int32_t>(lroundf(commandCurrentA_ * 1000.0f));
      if (!send(vesc::command(vesc::SET_CURRENT, milliAmps))) {
        fail("UART_TX_FULL");
        return;
      }
      ++driveQueued_;
      sentControlAt_ = controlAt_;
      lastCurrentTx_ = now;
    } else if (!gate_.running() && now - lastZero_ >= QUERY_MS) {
      if (!queueZero()) {
        zeroPending_ = true;
        return;
      }
    }
    // No standalone ALIVE; firmware/app configuration is never written.
    if (!firmwareKnown_) {
      if (!fwPending_ && now - lastProbe_ >= PROBE_MS && send(vesc::request(vesc::FW_VERSION))) {
        fwPending_ = true;
        fwRequested_ = now;
        lastProbe_ = now;
      }
    } else if (!valuesPending_ && now - lastQuery_ >= QUERY_MS &&
               send(vesc::request(vesc::GET_VALUES))) {
      valuesPending_ = true;
      valuesRequested_ = now;
      lastQuery_ = now;
    }
  }

  void help() {
    emit("COMMANDS,STATUS|CHECK|CAPTURE measured_mm|CAL_SHOW|CAL_RESET|HELP");
    emit("COMMANDS,TARGET depth_mm cpm|PREVIEW [cycles]|ZERO|CAL known_mm|CLEAR_ERR|ARM|RUN "
         "[cycles]|STOP");
    emit("COMMANDS,ILIMIT motor_A|INLIMIT input_A|GAINS kp ki kd|MOTORFLIP");
    emit("PINS,RX7=28,TX7=29,LINEAR_A16=40,TOP=3,BOTTOM=4,ESTOP=6");
    emit("CONFIG,default_depth_mm=%.1f,default_cpm=%.1f,auto_start=0,stop_is_not_hold=1",
         static_cast<double>(cfg::DEFAULT_DEPTH_MM), static_cast<double>(cfg::DEFAULT_RATE_CPM));
    emit("NOTE,POSITION_PID_TO_CURRENT,PREVIEW_no_drive,read_bench_config_h");
    emit("NOTE,ZERO_clears_scale,CAL_requires_measured_positive_travel,return_manually,no_auto_"
         "home");
    emit("NOTE,CAPTURE_requires_motor_power_off_and_manual_position_measurement,no_auto_"
         "calibration");
  }

  void reportCaptured(uint8_t index) {
    const CapturedPoint &p = captured_[index];
    if (!p.present) {
      emit("CAL_POINT,slot=%u,present=0", static_cast<unsigned>(index + 1));
      return;
    }
    emit("CAL_POINT,slot=%u,measured_mm=%.3f,adc_mean=%.3f,adc_min=%d,adc_max=%d,"
         "samples=%u,rail_seen=%u,invalid_seen=%u,quality_verified=0",
         static_cast<unsigned>(index + 1), static_cast<double>(p.mm),
         static_cast<double>(p.meanRaw), p.minRaw, p.maxRaw, static_cast<unsigned>(p.count),
         p.rail ? 1U : 0U, p.invalid ? 1U : 0U);
  }

  void showCalibration() {
    emit("CONTROL_CAL,zero_known=%u,scale_known=%u,origin_valid=%u",
         calibration_.zeroKnown() ? 1U : 0U, calibration_.calibrated() ? 1U : 0U,
         originKnown_ ? 1U : 0U);
    if (calibration_.calibrated())
      emit("CONTROL_CAL_POINTS,zero_raw=%d,cal_raw=%d,known_mm=%.3f,ram_only=1,accuracy_verified=0",
           calibration_.zeroRaw(), calibration_.calRaw(),
           static_cast<double>(calibration_.knownMm()));
    reportCaptured(0);
    reportCaptured(1);
    const CapturedPoint &a = captured_[0], &b = captured_[1];
    if (!a.present || !b.present) {
      emit("CAL_CANDIDATE,available=0,reason=TWO_MEASURED_POINTS_REQUIRED,applied=0");
      return;
    }
    const depth::Calibration candidate = {static_cast<int>(a.meanRaw + 0.5f),
                                          static_cast<int>(b.meanRaw + 0.5f), a.mm, b.mm, true};
    if (a.rail || b.rail || a.invalid || b.invalid || !depth::validCalibration(candidate) ||
        !(a.maxRaw < b.minRaw || b.maxRaw < a.minRaw)) {
      emit("CAL_CANDIDATE,available=0,reason=RAIL_INVALID_DUPLICATE_OR_OVERLAPPING_POINTS,applied="
           "0");
      return;
    }
    emit("CAL_CANDIDATE,available=1,raw0=%d,raw1=%d,mm0=%.3f,mm1=%.3f,"
         "applied=0,quality_verified=0,direction_verified=0",
         candidate.raw0, candidate.raw1, static_cast<double>(candidate.mm0),
         static_cast<double>(candidate.mm1));
  }

  void serviceCommissioning() {
    const uint32_t now = millis();
    capture_.expire(now);
    if (usbReady_ && captureCancelledNotice_ &&
        emit("EVENT,CAPTURE_CANCELLED,reason=%s,calibration_unchanged=1", reason_))
      captureCancelledNotice_ = false;
    if (usbReady_ && !captureReported_ && capture_.timedOut()) {
      captureReported_ = true;
      emit("ERROR,CAPTURE_TIMEOUT,calibration_unchanged=1");
    }
    if (usbReady_ && !captureReported_ && capture_.ready()) {
      captureReported_ = true;
      CapturedPoint &p = captured_[nextCapture_];
      p.present = true;
      p.mm = capture_.pointMm();
      p.meanRaw = capture_.meanRaw();
      p.minRaw = capture_.minRaw();
      p.maxRaw = capture_.maxRaw();
      p.count = capture_.count();
      p.rail = capture_.railSeen();
      p.invalid = capture_.rawInvalidSeen();
      reportCaptured(nextCapture_);
      nextCapture_ = (nextCapture_ + 1) % 2;
    }
    // One line per report interval rather than a burst that could starve controls/log buffers.
    if (!usbReady_ || checkIndex_ > CONFIG_CHECK_COUNT || now - checkAt_ < REPORT_MS)
      return;
    checkAt_ = now;
    if (checkIndex_ == CONFIG_CHECK_COUNT) {
      if (emit("CHECK_END,config_complete=%u,not_a_hardware_verification=1",
               configIssue() ? 0U : 1U))
        ++checkIndex_;
      return;
    }
    const ConfigCheck item = configCheck(checkIndex_);
    if (emit("CHECK,item=%u,requirement=%s,passed=%u", static_cast<unsigned>(checkIndex_ + 1),
             item.reason, item.passed ? 1U : 0U))
      ++checkIndex_; // Retry later if USB queue is full; never wait for the host.
  }
  void report() {
    const char *lock = configIssue();
    emit("STATUS,ms=%lu,locked=%u,armed=%u,running=%u,preview=%u,fault=%u,origin_known=%u,reason=%"
         "s,lock_reason=%s",
         static_cast<unsigned long>(millis()), lock ? 1U : 0U, gate_.armed() ? 1U : 0U,
         gate_.running() ? 1U : 0U, preview_ ? 1U : 0U, gate_.fault() ? 1U : 0U,
         originKnown_ ? 1U : 0U, reason_, lock ? lock : "NONE");
    emit("CAPTURE_STATUS,active=%u,samples=%u,slot1_present=%u,slot2_present=%u,applied=0",
         capture_.active() ? 1U : 0U, static_cast<unsigned>(capture_.count()),
         captured_[0].present ? 1U : 0U, captured_[1].present ? 1U : 0U);
    if (sampleSeen_ && position_.rawOk) {
      emit("LINEAR,raw_ok=1,position_ok=%u,adc_raw=%d,adc_V=%.4f,nominal_mm=%.3f,adc_rail=%u,"
           "sensor_presence_verified=0",
           position_.calibrated ? 1U : 0U, position_.raw, static_cast<double>(position_.adcV),
           static_cast<double>(position_.nominalMm), position_.rail ? 1U : 0U);
      if (position_.calibrated) {
        emit("POSITION,position_mm=%.3f", static_cast<double>(position_.mm));
        if (originKnown_)
          emit("DEPTH,depth_mm=%.3f", static_cast<double>(position_.mm - originMm_));
      }
    } else
      emit("LINEAR,raw_ok=0,position_ok=0");
    if (gate_.running() || preview_) {
      emit("TRACK,reference_mm=%.3f,reference_velocity_mm_s=%.3f,command_current_A=%.3f,preview_"
           "only=%u",
           static_cast<double>(reference_.mm), static_cast<double>(reference_.velocityMmS),
           static_cast<double>(commandCurrentA_), preview_ ? 1U : 0U);
      if (!preview_ && position_.calibrated)
        emit("PID,filtered_mm=%.3f,velocity_mm_s=%.3f,p_A=%.3f,i_A=%.3f,d_A=%.3f",
             static_cast<double>(filteredMm_), static_cast<double>(pid_.velocity()),
             static_cast<double>(pid_.p()), static_cast<double>(pid_.i()),
             static_cast<double>(pid_.d()));
    }
    if (valuesFresh())
      emit("VESC,values_ok=1,age_ms=%lu,input_V=%.1f,motor_current_A=%.2f,input_current_A=%.2f,"
           "erpm=%ld,fet_temp_C=%.1f,fault_code=%u",
           static_cast<unsigned long>(millis() - lastValues_), values_.inputDeciV / 10.0,
           values_.motorCentiA / 100.0, values_.inputCentiA / 100.0,
           static_cast<long>(values_.erpm), values_.fetDeciC / 10.0,
           static_cast<unsigned>(values_.fault));
    else
      emit("VESC,values_ok=0");
  }
  void diagnostics() {
    emit("CHECKS,calibration=%u,limits=%u,physical_estop=%u,direction=%u,vesc_settings=%u,sensor_"
         "fault_test=%u,bench_confirmed=%u",
         calibration_.calibrated() ? 1U : 0U, cfg::LIMIT_SWITCHES_INSTALLED ? 1U : 0U,
         cfg::PHYSICAL_ESTOP_AND_INPUT_VERIFIED ? 1U : 0U, cfg::MOTOR_DIRECTION_VERIFIED ? 1U : 0U,
         cfg::VESC_SETTINGS_AND_TIMEOUT_VERIFIED ? 1U : 0U,
         cfg::SENSOR_WIRING_AND_FAULT_TEST_VERIFIED ? 1U : 0U,
         cfg::BENCH_DRIVE_CONFIRMED ? 1U : 0U);
    emit("CURRENT_CONFIG,kp=%.3f,ki=%.3f,kd=%.3f,command_limit_A=%.3f,input_trip_A=%.3f,"
         "motor_sign=%d,active_return_enabled=%u,vesc_settings_written=0",
         static_cast<double>(gains_.kp), static_cast<double>(gains_.ki),
         static_cast<double>(gains_.kd), static_cast<double>(currentLimitA_),
         static_cast<double>(inputTripA_), static_cast<int>(motorSign_),
         cfg::RETURN_TO_ZERO_ENABLED ? 1U : 0U);
    emit("SETPOINT,depth_mm=%.3f,cpm=%.3f,peak_velocity_mm_s=%.3f,peak_acceleration_mm_s2=%.3f",
         static_cast<double>(targetDepth_), static_cast<double>(targetRate_),
         static_cast<double>(depth::peakVelocity(targetDepth_, targetRate_)),
         static_cast<double>(depth::peakAcceleration(targetDepth_, targetRate_)));
    emit("DIAG,stop_pending=%u,drive_queued=%lu,zero_queued=%lu,bad_packets=%lu,dropped_logs=%lu",
         zeroPending_ ? 1U : 0U, static_cast<unsigned long>(driveQueued_),
         static_cast<unsigned long>(zeroQueued_), static_cast<unsigned long>(badPackets_),
         static_cast<unsigned long>(droppedLogs_));
    if (inputsKnown())
      emit("INPUTS,top_raw=%d,bottom_raw=%d,estop_raw=%d", digitalRead(PIN_LIMIT_TOP),
           digitalRead(PIN_LIMIT_BOTTOM), digitalRead(PIN_ESTOP));
    else
      emit("INPUTS,inputs_ok=0");
  }

  void pollUsb() {
    for (uint8_t i = 0; i < 32 && Serial.available(); ++i) {
      char c = static_cast<char>(Serial.read());
      if (c == '\n') {
        if (!discardLine_ && lineUsed_) {
          line_[lineUsed_] = '\0';
          handleCommand(line_);
        }
        lineUsed_ = 0;
        discardLine_ = false;
        continue;
      }
      if (discardLine_)
        continue;
      if (c == '\r' || c == '\t')
        c = ' '; // Never join tokens across embedded CR.
      if (c < 32 || c > 126 || lineUsed_ >= sizeof(line_) - 1) {
        lineUsed_ = 0;
        discardLine_ = true;
        fail("USB_BAD_OR_LONG_COMMAND");
        continue;
      }
      if (!lineUsed_)
        lineStarted_ = millis();
      line_[lineUsed_++] = static_cast<char>(toupper(static_cast<unsigned char>(c)));
    }
  }
  void handleCommand(char *text) {
    char *context = nullptr;
    char *cmd = strtok_r(text, " ", &context);
    char *a = strtok_r(nullptr, " ", &context);
    char *b = strtok_r(nullptr, " ", &context);
    char *c = strtok_r(nullptr, " ", &context);
    char *extra = strtok_r(nullptr, " ", &context);
    if (!cmd)
      return;
    if ((!strcmp(cmd, "STOP") || !strcmp(cmd, "DISARM")) && !a) {
      requestStop();
      reason_ = "USER_STOP";
      emit("OK,STOP_REQUESTED,standstill_verified=0");
      return;
    }
    if (!strcmp(cmd, "HELP") && !a) {
      help();
      return;
    }
    if (!strcmp(cmd, "STATUS") && !a) {
      report();
      diagnostics();
      return;
    }
    if (!strcmp(cmd, "CHECK") && !a) {
      checkIndex_ = 0;
      checkAt_ = millis() - REPORT_MS;
      emit("CHECK_BEGIN,count=%u,read_only=1", static_cast<unsigned>(CONFIG_CHECK_COUNT));
      return;
    }
    if (gate_.running() || preview_) {
      fail("COMMAND_REQUIRES_STOP");
      return;
    }
    if (!strcmp(cmd, "CAL_SHOW") && !a) {
      showCalibration();
      return;
    }
    if (!strcmp(cmd, "CAL_RESET") && !a) {
      requestStop();
      originKnown_ = false;
      calibration_.reset();
      position_.calibrated = false;
      filter_.reset();
      captured_[0] = CapturedPoint{};
      captured_[1] = CapturedPoint{};
      nextCapture_ = 0;
      emit("OK,CALIBRATION_AND_CAPTURE_RECORDS_CLEARED,armed=0");
      return;
    }
    if (!strcmp(cmd, "CAPTURE") && a && !b) {
      requestStop();
      originKnown_ = false;
      float measuredMm = 0;
      if (!parseNumber(a, measuredMm) || !sampleSeen_ || micros() - sampleAt_ >= CONTROL_STALE_US ||
          !capture_.start(measuredMm, millis())) {
        fail("CAPTURE_REQUIRES_FRESH_ADC_AND_MEASURED_0_TO_75_MM");
        return;
      }
      // Every new attempt invalidates its slot, so a timeout cannot leave a stale pair looking new.
      captured_[nextCapture_] = CapturedPoint{};
      captureReported_ = false;
      captureCancelledNotice_ = false;
      reason_ = "CAPTURE_ONLY_NO_DRIVE";
      emit("OK,CAPTURE_STARTED,slot=%u,measured_mm=%.3f,armed=0,calibration_unchanged=1",
           static_cast<unsigned>(nextCapture_ + 1), static_cast<double>(measuredMm));
      return;
    }
    if (capture_.active()) {
      fail("CAPTURE_ACTIVE_USE_STOP");
      return;
    }
    if (!strcmp(cmd, "TARGET") && a && b && !c) {
      float d = 0, r = 0;
      requestStop();
      if (!parseNumber(a, d) || !parseNumber(b, r) || !validTarget(d, r)) {
        fail("TARGET_OUT_OF_REQUESTED_ENVELOPE");
        return;
      }
      targetDepth_ = d;
      targetRate_ = r;
      emit("OK,TARGET,depth_mm=%.3f,cpm=%.3f,armed=0", static_cast<double>(d),
           static_cast<double>(r));
    } else if (!strcmp(cmd, "PREVIEW") && !b) {
      uint32_t count = 1;
      requestStop();
      if ((a && !parseCycles(a, count)) || !validDuration(targetRate_, count, ABSOLUTE_RUN_MS)) {
        fail("PREVIEW_DURATION_OUT_OF_BENCH_RANGE");
        return;
      }
      cycles_ = count;
      preview_ = true;
      startedUs_ = micros();
      reason_ = "REFERENCE_PREVIEW";
      reference_ = depth::reference(0, targetDepth_, targetRate_, 0, cycles_);
      emit("OK,PREVIEW,drive_command_active=0,virtual_origin_mm=0");
    } else if (!strcmp(cmd, "ZERO") && !a) {
      requestStop();
      originKnown_ = false;
      calibration_.reset();
      position_.calibrated = false;
      filter_.reset();
      resting_ = false;
      if (!sampleSeen_ || micros() - sampleAt_ >= CONTROL_STALE_US ||
          !calibration_.zero(position_.raw)) {
        fail("ZERO_REQUIRES_FRESH_NONRAIL_ADC");
        return;
      }
      originMm_ = 0;
      emit("OK,ZERO,raw=%d,scale_known=0,armed=0,homing_verified=0", calibration_.zeroRaw());
    } else if (!strcmp(cmd, "CAL") && a && !b) {
      requestStop();
      originKnown_ = false;
      position_.calibrated = false;
      filter_.reset();
      resting_ = false;
      float knownMm = 0;
      if (!parseNumber(a, knownMm) || !sampleSeen_ || micros() - sampleAt_ >= CONTROL_STALE_US ||
          !calibration_.calibrate(position_.raw, knownMm)) {
        // Invalid CAL must discard any old scale, but retain ZERO for a measured retry.
        calibration_.calibrate(position_.raw, 0);
        fail("CAL_REQUIRES_ZERO_AND_MEASURED_5_TO_60_MM");
        return;
      }
      originKnown_ = true;
      originMm_ = 0;
      emit("OK,CAL,known_mm=%.3f,zero_raw=%d,cal_raw=%d,armed=0,ram_only=1,accuracy_verified=0",
           static_cast<double>(knownMm), calibration_.zeroRaw(), calibration_.calRaw());
    } else if (!strcmp(cmd, "ILIMIT") && a && !b) {
      requestStop();
      float amps = 0;
      if (!parseNumber(a, amps) || amps < 1 || amps > cfg::MAX_ALLOWED_COMMAND_CURRENT_A) {
        fail("ILIMIT_OUT_OF_CONFIGURED_RANGE");
        return;
      }
      currentLimitA_ = amps;
      emit("OK,ILIMIT,command_limit_A=%.3f,armed=0,vesc_settings_written=0",
           static_cast<double>(amps));
    } else if (!strcmp(cmd, "INLIMIT") && a && !b) {
      requestStop();
      float amps = 0;
      if (!parseNumber(a, amps) || amps < .5f || amps > cfg::MAX_ALLOWED_INPUT_TRIP_A) {
        fail("INLIMIT_OUT_OF_CONFIGURED_RANGE");
        return;
      }
      inputTripA_ = amps;
      emit("OK,INLIMIT,supervisory_input_trip_A=%.3f,armed=0,vesc_settings_written=0",
           static_cast<double>(amps));
    } else if (!strcmp(cmd, "GAINS") && a && b && c && !extra) {
      requestStop();
      saver_current::Gains next = gains_;
      if (!parseNumber(a, next.kp) || !parseNumber(b, next.ki) || !parseNumber(c, next.kd) ||
          !saver_current::validGains(next)) {
        fail("CURRENT_GAINS_OUT_OF_RANGE");
        return;
      }
      gains_ = next;
      emit("OK,GAINS,kp=%.3f,ki=%.3f,kd=%.3f,armed=0", static_cast<double>(next.kp),
           static_cast<double>(next.ki), static_cast<double>(next.kd));
    } else if (!strcmp(cmd, "MOTORFLIP") && !a) {
      requestStop();
      if (!cfg::MOTOR_DIRECTION_VERIFIED || (motorSign_ != 1 && motorSign_ != -1)) {
        fail("MOTOR_DIRECTION_UNVERIFIED");
        return;
      }
      motorSign_ = -motorSign_;
      emit("OK,MOTORFLIP,motor_sign=%d,armed=0,physical_direction_not_verified_by_command=1",
           static_cast<int>(motorSign_));
    } else if (!strcmp(cmd, "CLEAR_ERR") && !a) {
      requestStop();
      if (!gate_.clear(healthy_ && millis() - healthyAt_ >= STABLE_MS && !runtimeIssue())) {
        fail("FAULT_CAUSE_NOT_CLEARED");
        return;
      }
      reason_ = "LOCAL_FAULT_CLEARED";
      emit("OK,LOCAL_FAULT_CLEARED,armed=0");
    } else if (!strcmp(cmd, "ARM") && !a) {
      if (const char *why = runIssue()) {
        fail(why);
        return;
      }
      if (!gate_.arm(true, millis())) {
        fail("FAULT_LATCHED_OR_ARM_REJECTED");
        return;
      }
      emit("OK,ARMED,expires_ms=10000");
    } else if (!strcmp(cmd, "RUN") && !b) {
      uint32_t count = 1;
      const uint32_t returnBudget = cfg::RETURN_TO_ZERO_ENABLED ? cfg::RETURN_TIMEOUT_MS : 0;
      if ((a && !parseCycles(a, count)) || count > 20 || cfg::MAX_RUN_MS <= returnBudget ||
          !validDuration(targetRate_, count, cfg::MAX_RUN_MS - returnBudget)) {
        fail("RUN_DURATION_NOT_CONFIGURED_OR_EXCEEDED");
        return;
      }
      if (const char *why = runIssue()) {
        fail(why);
        return;
      }
      if (!gate_.start(true, millis())) {
        fail("RUN_REQUIRES_FRESH_ARM");
        return;
      }
      cycles_ = count;
      startedUs_ = micros();
      pid_.reset(filteredMm_);
      controlAt_ = 0;
      sentControlAt_ = 0;
      commandCurrentA_ = 0;
      lastCurrentTx_ = millis() - cfg::CURRENT_TX_MS;
      reference_ = depth::reference(originMm_, targetDepth_, targetRate_, 0, cycles_);
      reason_ = "PROFILE_REQUESTED";
      emit("OK,RUN_REQUESTED,cycles=%lu,depth_mm=%.3f,cpm=%.3f,tracking_success_verified=0",
           static_cast<unsigned long>(cycles_), static_cast<double>(targetDepth_),
           static_cast<double>(targetRate_));
    } else
      fail("UNKNOWN_OR_MALFORMED_COMMAND");
  }
};

DepthBench &bench() {
  static DepthBench instance;
  return instance;
}
} // namespace

void setup() { bench().begin(); }
void loop() { bench().tick(); }
