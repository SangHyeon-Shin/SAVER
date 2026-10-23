#ifndef SAVER_SELECTABLE_COMPRESSION_H
#define SAVER_SELECTABLE_COMPRESSION_H
#include <math.h>
#include "bench_io.h"
#include "src/bench_guard.h"
#include "src/vesc_link.h"
#include "src/nau7802_reader.h"
#include "linear_adapter.h"

namespace selectable {
namespace cfg = saver_depth_config;
namespace control = saver_depth_bench;
namespace vesc = saver_pid_vesc;

class Compression {
public:
  explicit Compression(Log &log) : log_(log), bus_(Wire1, 0x2A) {}
  void begin() {
    pinMode(A16, INPUT_DISABLE);
    pinMode(3, cfg::LIMIT_SWITCHES_INSTALLED ? INPUT_PULLUP : INPUT);
    pinMode(4, cfg::LIMIT_SWITCHES_INSTALLED ? INPUT_PULLUP : INPUT);
    pinMode(6, cfg::ESTOP_AUX_INSTALLED ? INPUT_PULLUP : INPUT);
    Serial7.setRX(28);
    Serial7.setTX(29);
    Serial7.begin(cfg::UART_BAUD, SERIAL_8N1);
    stop();
  }
  void enter() {
    stop();
    calibration_.reset();
    filter_.reset();
    forceCal_.reset();
    fwKnown_ = valuesKnown_ = fwPending_ = valuesPending_ = false;
    parser_.reset();
    // Ignore bytes from the previously selected session, including stale replies.
    for (unsigned i = 0; i < 1024 && Serial7.available(); ++i)
      Serial7.read();
    raw_ = -1;
    positionValid_ = forceSeen_ = healthy_ = resting_ = false;
    sampleAt_ = micros();
    queryAt_ = millis() - 100;
    probeAt_ = millis() - 1000;
    Wire1.setSDA(17);
    Wire1.setSCL(16);
    Wire1.begin();
    Wire1.setClock(400000);
    reader_.begin(millis());
    log_.emit("INFO,COMPRESSION,position_current_pid=LM,guards=compression_linear_pid_test,"
              "force=MEASURE_AND_CONFIGURED_ABS_TRIP,travel_switches=%u,estop_aux=%u",
              cfg::LIMIT_SWITCHES_INSTALLED, cfg::ESTOP_AUX_INSTALLED);
  }
  void stop() {
    gate_.stop();
    commandA_ = 0;
    pid_.reset(position_);
    zeroPending_ = true;
    if (capture_) {
      capture_ = 0;
      forceCal_.reset();
    }
    serviceStop();
  }
  // The router calls this in EVERY mode; only a zero-current frame is permitted.
  void serviceStop() {
    if (zeroPending_ && send(vesc::releaseCommand())) {
      zeroPending_ = false;
      zeroAt_ = millis();
    }
  }
  bool stopPending() const { return zeroPending_; }
  bool active() const { return gate_.running(); }
  bool armed() const { return gate_.armed(); }
  float targetRate() const { return rate_; }
  float commandCurrentLimit() const { return currentLimit_; }
  float inputCurrentLimit() const { return inputLimit_; }
  const char *durationIssue(uint32_t cycles) const {
    if (!control::validDuration(rate_, cycles, cfg::MAX_RUN_MS) ||
        uint64_t(double(cycles) * 60000.0 / rate_) +
                (cfg::RETURN_TO_ZERO_ENABLED ? cfg::RETURN_TIMEOUT_MS : 0) >
            cfg::MAX_RUN_MS)
      return "RUN_DURATION_EXCEEDS_LIMIT";
    return nullptr;
  }
  bool deadlineMissed() const {
    return gate_.running() && uint32_t(micros() - sampleAt_) >= control::CONTROL_STALE_US;
  }
  void stopForDeadline() { trip("CONTROL_DEADLINE_MISSED"); }
  bool standstill() const {
    return !active() && !stopPending() && valuesFresh() && values_.erpm == 0 && positionValid_ &&
           uint32_t(micros() - sampleAt_) < control::CONTROL_STALE_US &&
           cfg::MAX_START_VELOCITY_MM_S > 0 && fabsf(velocity_) <= cfg::MAX_START_VELOCITY_MM_S;
  }
  bool fault() const { return gate_.fault(); }
  const char *faultReason() const { return faultWhy_; }
  bool position(float &mm) const {
    mm = position_;
    return positionValid_;
  }
  bool force(double &n) const { return forceFresh() && forceCal_.force(forceRaw_, n); }
  bool motorCurrent(float &a) const {
    a = values_.motorCentiA / 100.0f;
    return valuesFresh();
  }
  bool cycleMetrics(float &depth, float &rate) const {
    depth = lastPeak_;
    rate = measuredRate_;
    return metricsKnown_ && gate_.running();
  }
  const char *readyIssue() const {
    if (gate_.fault())
      return "CP_FAULT_USE_CLEAR";
    if (forceLimit_ <= 0)
      return "FORCE_LIMIT_NOT_SET";
    return runIssue();
  }
  template <class Memory> void loadCalibration(Memory &m) { calibration_.load(m); }
  void tick() {
    pollVesc();
    if (const char *why = controlTick())
      trip(why);
    if (profileDone_) {
      profileDone_ = false;
      stop();
      log_.emit("EVENT,PROFILE_COMPLETE,standstill_verified=0");
    }
    safety();
    int32_t next = 0;
    const auto result = reader_.tick(bus_, millis(), next);
    if (result == reaction::Nau7802Reader::Result::Error) {
      forceSeen_ = false;
      forceCal_.reset();
      capture_ = 0;
      if (gate_.running() || gate_.armed())
        trip(reader_.error());
      else
        log_.emit("ERROR,NAU7802,reason=%s", reader_.error());
    }
    if (result == reaction::Nau7802Reader::Result::Sample)
      acceptForce(next);
    // An I2C timeout must not be hidden by resetting the control timestamp.
    if (gate_.running() && uint32_t(micros() - sampleAt_) >= control::CONTROL_STALE_US)
      trip("CONTROL_DEADLINE_MISSED");
    safety();
    if (capture_ && uint32_t(millis() - captureAt_) >= 3000) {
      capture_ = 0;
      forceCal_.reset();
      log_.emit("ERROR,FORCE_CAL_TIMEOUT");
    }
    gate_.expire(millis());
    serviceTx();
  }
  bool command(const Command &c) {
    float a = 0, b = 0, d = 0;
    uint32_t cycles = 1;
    if (c.is("CHECK", 1)) {
      checkIndex_ = 0;
      return true;
    }
    if (c.is("ARM", 1)) {
      const char *why = runIssue();
      if (!gate_.arm(!why && !capture_, millis())) {
        stop();
        log_.emit("ERROR,%s", why ? why : "CP_FAULT_OR_CAPTURE_USE_CLEAR");
      } else
        log_.emit("OK,ARMED,valid_ms=10000");
      return true;
    }
    if ((c.count == 1 || c.count == 2) && !strcmp(c.tokens[0], "RUN")) {
      if (c.count == 2 && !control::parseCycles(c.tokens[1], cycles)) {
        reject("BAD_CYCLE_COUNT");
        return true;
      }
      const char *why = runIssue();
      if (!why)
        why = durationIssue(cycles);
      if (!why && capture_)
        why = "FORCE_CAL_IN_PROGRESS";
      if (why || !gate_.start(!why, millis())) {
        reject(why ? why : "ARM_REQUIRED_OR_EXPIRED");
        return true;
      }
      cycles_ = cycles;
      metricsKnown_ = false;
      cycleNumber_ = 0;
      cyclePeak_ = 0;
      cycleAt_ = millis();
      started_ = micros();
      pid_.reset(position_);
      currentAt_ = millis();
      log_.emit("OK,COMPRESSION_STARTED,cycles=%lu", static_cast<unsigned long>(cycles));
      return true;
    }
    if (gate_.running()) {
      reject("CONFIG_REQUIRES_STOP");
      return true;
    }
    // All configuration changes cancel an old ARM. They never move the axis.
    if (c.is("SENSOR_RETRY", 1)) {
      stop();
      forceCal_.reset();
      forceSeen_ = false;
      capture_ = 0;
      Wire1.begin();
      Wire1.setClock(400000);
      reader_.begin(millis());
      log_.emit("OK,NAU7802_RETRY,TARE_AND_FORCE_CAL_REQUIRED=1");
      return true;
    }
    if (c.is("FORCELIMIT", 2) && control::parseNumber(c.tokens[1], a) && a > 0 && a <= 600) {
      stop();
      forceLimit_ = a;
      log_.emit("OK,FORCELIMIT,N=%.3f,bench_only=1", double(a));
      return true;
    }
    if (c.is("CLEAR", 1)) {
      const char *why = runtimeIssue();
      if (!gate_.clear(!why))
        log_.emit("ERROR,%s", why ? why : "CLEAR_REJECTED");
      else
        log_.emit("OK,CP_FAULT_CLEARED,armed=0");
      return true;
    }
    if (c.is("ZERO", 1)) {
      stop();
      filter_.reset();
      positionValid_ = false;
      healthy_ = resting_ = false;
      if (!calibration_.zero(analogRead(A16)))
        log_.emit("ERROR,LINEAR_ZERO_ADC_RAIL");
      else
        log_.emit("OK,LINEAR_ZERO,CAL_required=1");
      return true;
    }
    if (c.is("CAL", 2) && control::parseNumber(c.tokens[1], a)) {
      stop();
      filter_.reset();
      positionValid_ = false;
      healthy_ = resting_ = false;
      if (!calibration_.calibrate(analogRead(A16), a))
        log_.emit("ERROR,LINEAR_CAL_REJECTED");
      else
        log_.emit("OK,LINEAR_CAL,known_mm=%.3f,return_to_zero_manually=1", double(a));
      return true;
    }
    if (c.is("TARGET", 3) && control::parseNumber(c.tokens[1], a) &&
        control::parseNumber(c.tokens[2], b) && control::validTarget(a, b)) {
      stop();
      depth_ = a;
      rate_ = b;
      log_.emit("OK,TARGET,depth_mm=%.3f,cycles_per_min=%.3f", double(a), double(b));
      return true;
    }
    if (c.is("ILIMIT", 2) && control::parseNumber(c.tokens[1], a) && a > 0 &&
        a <= cfg::MAX_ALLOWED_COMMAND_CURRENT_A) {
      stop();
      currentLimit_ = a;
      log_.emit("OK,ILIMIT,A=%.3f", double(a));
      return true;
    }
    if (c.is("INLIMIT", 2) && control::parseNumber(c.tokens[1], a) && a > 0 &&
        a <= cfg::MAX_ALLOWED_INPUT_TRIP_A) {
      stop();
      inputLimit_ = a;
      log_.emit("OK,INLIMIT,A=%.3f", double(a));
      return true;
    }
    if (c.is("GAINS", 4) && control::parseNumber(c.tokens[1], a) &&
        control::parseNumber(c.tokens[2], b) && control::parseNumber(c.tokens[3], d)) {
      auto candidate = gains_;
      candidate.kp = a;
      candidate.ki = b;
      candidate.kd = d;
      if (!saver_current::validGains(candidate)) {
        reject("BAD_GAINS");
        return true;
      }
      stop();
      gains_ = candidate;
      log_.emit("OK,GAINS");
      return true;
    }
    if (c.is("TARE", 1) ||
        (c.is("FORCE_CAL", 2) && control::parseNumber(c.tokens[1], a) && a > 0 && a <= 100)) {
      const bool tare = c.is("TARE", 1);
      if (capture_ || !forceFresh() || (!tare && !forceCal_.zeroKnown)) {
        reject("FORCE_CAL_REQUIRES_READY_TARE_AND_KNOWN_KG");
        return true;
      }
      stop();
      capture_ = tare ? 1 : 2;
      massKg_ = a;
      captureAt_ = millis();
      forceWindow_ = reaction::Window{};
      if (tare)
        forceCal_.reset();
      else
        forceCal_.confirmed = false;
      log_.emit("OK,FORCE_CAPTURE_STARTED,samples=320,kind=%s", tare ? "TARE" : "CAL");
      return true;
    }
    return false;
  }
  void report() {
    log_.emit(
        "COMPRESSION,armed=%u,running=%u,fault=%u,adc_raw=%d,position_ok=%u,"
        "vesc_fresh=%u,force_ok=%u,force_calibrated=%u,stop_pending=%u,depth_mm=%.2f,cpm=%.2f",
        gate_.armed(), gate_.running(), gate_.fault(), raw_, positionValid_, valuesFresh(),
        forceFresh(), forceCal_.confirmed, zeroPending_, double(depth_), double(rate_));
    if (positionValid_)
      log_.emit("POSITION,mm=%.3f,reference_mm=%.3f,command_A=%.3f", double(position_),
                double(reference_), double(commandA_));
    double force = 0;
    if (forceFresh()) {
      if (forceCal_.force(forceRaw_, force))
        log_.emit("FORCE,raw=%ld,force_N=%.3f,force_limit_N=%.3f", static_cast<long>(forceRaw_),
                  force, double(forceLimit_));
      else
        log_.emit("FORCE,raw=%ld,calibrated=0", static_cast<long>(forceRaw_));
    }
    if (valuesFresh())
      log_.emit("VESC,input_V=%.2f,input_A=%.2f,motor_A=%.2f,fault=%u", values_.inputDeciV / 10.0,
                values_.inputCentiA / 100.0, values_.motorCentiA / 100.0, values_.fault);
  }
  void serviceCheck() {
    if (checkIndex_ < control::CONFIG_CHECK_COUNT) {
      const auto check = control::configCheck(checkIndex_++, calibration_.calibrated());
      log_.emit("CHECK,%s,passed=%u", check.reason, check.passed);
    } else if (checkIndex_ == control::CONFIG_CHECK_COUNT) {
      ++checkIndex_;
      const char *why = runIssue();
      log_.emit("CHECK,READY,passed=%u,reason=%s", !why && !gate_.fault(),
                why             ? why
                : gate_.fault() ? "CP_FAULT_USE_CLEAR"
                                : "OK");
    }
  }

private:
  void reject(const char *why) {
    stop();
    log_.emit("ERROR,%s", why);
  }
  void trip(const char *why) {
    stop();
    gate_.trip();
    healthy_ = resting_ = false;
    faultWhy_ = why;
    log_.emit("FAULT,%s,zero_current_requested=1,standstill_verified=0", why);
  }
  bool send(const vesc::Frame &f) {
    return Serial7.availableForWrite() >= int(f.length) &&
           Serial7.write(f.bytes, f.length) == f.length;
  }
  bool valuesFresh() const {
    return fwKnown_ && valuesKnown_ && uint32_t(millis() - valuesAt_) < vesc::VALUES_STALE_MS;
  }
  bool forceFresh() const {
    return forceSeen_ && reader_.ready() && !reaction::rail(forceRaw_) &&
           uint32_t(millis() - forceAt_) < 100;
  }
  const char *runtimeIssue() const {
    if (!Serial)
      return "USB_NOT_READY";
    if (!control::configuredInputsReady())
      return "SAFETY_INPUTS_UNKNOWN";
    if (cfg::ESTOP_AUX_INSTALLED && digitalRead(6) == cfg::ESTOP_ACTIVE_LEVEL)
      return "ESTOP_ACTIVE";
    if (cfg::LIMIT_SWITCHES_INSTALLED &&
        (digitalRead(3) == cfg::TOP_ACTIVE_LEVEL || digitalRead(4) == cfg::BOTTOM_ACTIVE_LEVEL))
      return "LIMIT_ACTIVE";
    if (!valuesFresh())
      return "VESC_VALUES_STALE_OR_FW_UNSUPPORTED";
    if (values_.fault)
      return "VESC_REPORTED_FAULT";
    if (values_.inputDeciV / 10.0f < cfg::MIN_INPUT_V ||
        values_.inputDeciV / 10.0f > cfg::MAX_INPUT_V)
      return "INPUT_VOLTAGE_LIMIT";
    if (fabsf(values_.motorCentiA / 100.0f) > cfg::MAX_MOTOR_CURRENT_A)
      return "MOTOR_CURRENT_LIMIT";
    if (fabsf(values_.inputCentiA / 100.0f) > inputLimit_)
      return "INPUT_CURRENT_LIMIT";
    if (values_.fetDeciC / 10.0f > cfg::MAX_FET_TEMP_C)
      return "FET_TEMPERATURE_LIMIT";
    if (!positionValid_ || uint32_t(micros() - sampleAt_) >= control::CONTROL_STALE_US)
      return "LINEAR_INVALID_OR_STALE";
    if (position_ <= cfg::SOFT_MIN_MM || position_ >= cfg::SOFT_MAX_MM)
      return "OUTSIDE_SOFT_TRAVEL";
    if (!forceFresh() || !forceCal_.confirmed)
      return "LOADCELL_UNAVAILABLE_OR_UNCALIBRATED";
    double n = 0;
    if (forceLimit_ > 0 && forceCal_.force(forceRaw_, n) && fabs(n) >= forceLimit_)
      return "FORCE_LIMIT";
    return nullptr;
  }
  const char *runIssue() const {
    if (!control::validTarget(depth_, rate_))
      return "TARGET_NOT_SET";
    if (forceLimit_ <= 0)
      return "FORCE_LIMIT_NOT_SET";
    if (const char *why = control::configIssue(calibration_.calibrated()))
      return why;
    if (const char *why = runtimeIssue())
      return why;
    if (zeroPending_)
      return "STOP_TX_PENDING";
    if (capture_)
      return "FORCE_CAL_IN_PROGRESS";
    if (!healthy_ || uint32_t(millis() - healthyAt_) < control::STABLE_MS || !resting_ ||
        uint32_t(millis() - restAt_) < control::STABLE_MS)
      return "INPUTS_NOT_STABLE_FOR_250MS";
    if (fabsf(position_) > cfg::START_TOLERANCE_MM)
      return "NOT_AT_MANUAL_ZERO";
    if (!calibration_.covers(depth_) || depth_ >= cfg::SOFT_MAX_MM)
      return "TARGET_OUTSIDE_CAL_OR_TRAVEL";
    if (saver_depth::peakVelocity(depth_, rate_) > cfg::MAX_PROFILE_VELOCITY_MM_S ||
        saver_depth::peakAcceleration(depth_, rate_) > cfg::MAX_PROFILE_ACCELERATION_MM_S2)
      return "PROFILE_EXCEEDS_MOTION_LIMITS";
    return nullptr;
  }
  const char *controlTick() {
    const uint32_t now = micros(), elapsed = now - sampleAt_;
    if (elapsed < control::CONTROL_US)
      return nullptr;
    const bool deadline = elapsed >= control::CONTROL_STALE_US;
    sampleAt_ = now;
    raw_ = analogRead(A16);
    const bool hadPosition = positionValid_;
    const float previous = rawPosition_;
    positionValid_ = calibration_.position(raw_, rawPosition_);
    if (gate_.running() && deadline)
      return "CONTROL_DEADLINE_MISSED";
    if (!positionValid_)
      return gate_.running() ? "LINEAR_ADC_RAIL_OR_INVALID" : nullptr;
    const float dt = elapsed / 1000000.0f;
    if (!hadPosition || deadline)
      filter_.reset();
    position_ = filter_.step(rawPosition_, dt, cfg::POSITION_FILTER_TAU_S);
    velocity_ = hadPosition && !deadline ? (rawPosition_ - previous) / dt : 0;
    if (!gate_.running())
      return nullptr;
    if (fabsf(rawPosition_ - previous) >
        cfg::MAX_MEASURED_VELOCITY_MM_S * dt + cfg::SAMPLE_NOISE_ALLOWANCE_MM)
      return "LINEAR_SAMPLE_JUMP";
    if (rawPosition_ <= cfg::SOFT_MIN_MM || rawPosition_ >= cfg::SOFT_MAX_MM)
      return "RAW_POSITION_SOFT_LIMIT";
    if (const char *why = runtimeIssue())
      return why;
    if (uint32_t(now - started_) / 1000 >= cfg::MAX_RUN_MS)
      return "RUN_TIME_LIMIT";
    const auto ref = saver_depth::reference(0, depth_, rate_, now - started_, cycles_);
    const uint32_t cycle = uint32_t(double(uint32_t(now - started_)) * rate_ / 60000000.0);
    if (cycle > cycleNumber_) {
      const uint32_t duration = millis() - cycleAt_;
      lastPeak_ = cyclePeak_;
      measuredRate_ = duration ? 60000.0f / duration : 0;
      metricsKnown_ = duration > 0;
      cyclePeak_ = 0;
      cycleAt_ = millis();
      cycleNumber_ = cycle;
    }
    if (position_ > cyclePeak_)
      cyclePeak_ = position_;
    reference_ = float(ref.mm);
    if (ref.finished) {
      reference_ = 0;
      if (fabsf(position_) <= cfg::RETURN_TOLERANCE_MM) {
        profileDone_ = true;
        return nullptr;
      }
      if (!cfg::RETURN_TO_ZERO_ENABLED)
        return "END_POSITION_ERROR";
      const double nominalUs = double(cycles_) * 60000000.0 / rate_;
      if (double(uint32_t(now - started_)) - nominalUs >= cfg::RETURN_TIMEOUT_MS * 1000.0)
        return "RETURN_ZERO_TIMEOUT";
    }
    if (fabsf(reference_ - position_) > cfg::MAX_FOLLOWING_ERROR_MM)
      return "FOLLOWING_ERROR";
    commandA_ = pid_.step(reference_, position_, dt, gains_, currentLimit_) *
                cfg::CURRENT_SIGN_FOR_INCREASING_MM;
    if (!pid_.valid())
      return "PID_INVALID";
    controlAt_ = micros();
    return nullptr;
  }
  void safety() {
    const char *why = runtimeIssue();
    if (why) {
      healthy_ = resting_ = false;
      if (gate_.running() || gate_.armed())
        trip(why);
    } else {
      if (!healthy_) {
        healthy_ = true;
        healthyAt_ = millis();
      }
      if (fabsf(velocity_) <= cfg::MAX_START_VELOCITY_MM_S) {
        if (!resting_) {
          resting_ = true;
          restAt_ = millis();
        }
      } else
        resting_ = false;
    }
  }
  void acceptForce(int32_t raw) {
    forceRaw_ = raw;
    forceAt_ = millis();
    forceSeen_ = !reaction::rail(raw);
    if (!forceSeen_) {
      forceCal_.reset();
      capture_ = 0;
      if (gate_.running() || gate_.armed())
        trip("LOADCELL_RAIL");
      return;
    }
    if (!capture_)
      return;
    forceWindow_.add(raw, forceCal_);
    if (forceWindow_.validCount < 320)
      return;
    const double mean = double(forceWindow_.rawSum) / forceWindow_.validCount;
    bool ok = false;
    if (capture_ == 1) {
      ok = forceCal_.tare(mean);
      tareMin_ = forceWindow_.rawMin;
      tareMax_ = forceWindow_.rawMax;
    } else if (reaction::separated(tareMin_, tareMax_, forceWindow_.rawMin, forceWindow_.rawMax))
      ok = forceCal_.calibrate(mean, massKg_);
    capture_ = 0;
    if (!ok)
      forceCal_.reset();
    log_.emit("%s,FORCE_CAPTURE_DONE,zero_known=%u,calibrated=%u", ok ? "OK" : "ERROR",
              forceCal_.zeroKnown, forceCal_.confirmed);
  }
  void pollVesc() {
    if (parser_.expire(millis()))
      badLink();
    for (unsigned i = 0; i < 128 && Serial7.available(); ++i) {
      const auto r = parser_.push(uint8_t(Serial7.read()), millis());
      if (r == vesc::PacketResult::Invalid) {
        badLink();
        continue;
      }
      if (r != vesc::PacketResult::Ready)
        continue;
      const uint8_t *p = parser_.data();
      const size_t n = parser_.size();
      if (n >= 3 && p[0] == vesc::FW_VERSION && fwPending_) {
        fwPending_ = false;
        fwKnown_ = vesc::supportedFirmware(p[1]);
        log_.emit("INFO,VESC_FW,major=%u,minor=%u,supported=%u", p[1], p[2], fwKnown_);
        if (!fwKnown_)
          badLink();
      } else if (n && p[0] == vesc::GET_VALUES && valuesPending_) {
        valuesPending_ = false;
        if (fwKnown_ && uint32_t(millis() - valuesRequested_) < vesc::VALUES_STALE_MS &&
            vesc::decodeValues(p, n, values_)) {
          valuesKnown_ = true;
          valuesAt_ = millis();
        } else
          badLink();
      }
    }
  }
  void badLink() {
    valuesKnown_ = fwKnown_ = fwPending_ = valuesPending_ = false;
    parser_.reset();
    if (gate_.running() || gate_.armed())
      trip("VESC_BAD_OR_EXPIRED_PACKET");
  }
  void serviceTx() {
    serviceStop();
    if (zeroPending_)
      return;
    if (gate_.running() && uint32_t(millis() - currentAt_) >= cfg::CURRENT_TX_MS) {
      const char *why = runtimeIssue();
      if (why || uint32_t(micros() - controlAt_) >= control::CONTROL_STALE_US) {
        trip(why ? why : "CURRENT_COMMAND_STALE");
        return;
      }
      if (!send(vesc::command(vesc::SET_CURRENT, int32_t(commandA_ * 1000.0f)))) {
        trip("UART_TX_FULL");
        return;
      }
      currentAt_ = millis();
    }
    if (!gate_.running() && uint32_t(millis() - zeroAt_) >= 100) {
      zeroPending_ = true;
      serviceStop();
      if (zeroPending_)
        return;
    }
    if (fwPending_ && uint32_t(millis() - probeAt_) >= 300)
      fwPending_ = false;
    if (!fwKnown_ && !fwPending_ && uint32_t(millis() - probeAt_) >= 1000) {
      if (send(vesc::request(vesc::FW_VERSION))) {
        fwPending_ = true;
        probeAt_ = millis();
      }
    }
    if (valuesPending_ && uint32_t(millis() - valuesRequested_) >= vesc::VALUES_STALE_MS)
      valuesPending_ = false;
    if (fwKnown_ && !valuesPending_ && uint32_t(millis() - queryAt_) >= 100) {
      if (send(vesc::request(vesc::GET_VALUES))) {
        valuesPending_ = true;
        queryAt_ = valuesRequested_ = millis();
      }
    }
  }
  Log &log_;
  RegisterBus<TwoWire> bus_;
  reaction::Nau7802Reader reader_;
  reaction::Calibration forceCal_;
  reaction::Window forceWindow_;
  LinearAdapter calibration_;
  saver_current::PositionFilter filter_;
  saver_current::Pid pid_;
  saver_current::Gains gains_ = cfg::CURRENT_PID;
  control::RunGate gate_;
  vesc::PacketParser parser_;
  vesc::Values values_{};
  bool zeroPending_ = true, fwKnown_ = false, valuesKnown_ = false;
  bool fwPending_ = false, valuesPending_ = false, positionValid_ = false;
  bool forceSeen_ = false, healthy_ = false, resting_ = false, profileDone_ = false;
  uint32_t sampleAt_ = 0, controlAt_ = 0, started_ = 0, currentAt_ = 0, zeroAt_ = 0;
  uint32_t valuesAt_ = 0, valuesRequested_ = 0, queryAt_ = 0, probeAt_ = 0;
  uint32_t forceAt_ = 0, captureAt_ = 0, healthyAt_ = 0, restAt_ = 0, cycles_ = 1;
  int raw_ = -1;
  int32_t forceRaw_ = 0, tareMin_ = 0, tareMax_ = 0;
  uint8_t capture_ = 0, checkIndex_ = control::CONFIG_CHECK_COUNT + 1;
  float position_ = 0, rawPosition_ = 0, velocity_ = 0, reference_ = 0, commandA_ = 0;
  float depth_ = 0, rate_ = 0; // Main firmware requires an explicit bench target.
  float currentLimit_ = cfg::DEFAULT_COMMAND_CURRENT_A, inputLimit_ = cfg::DEFAULT_INPUT_TRIP_A;
  double massKg_ = 0;
  float forceLimit_ = 0, cyclePeak_ = 0, lastPeak_ = 0, measuredRate_ = 0;
  bool metricsKnown_ = false;
  uint32_t cycleNumber_ = 0, cycleAt_ = 0;
  const char *faultWhy_ = "NONE";
};
} // namespace selectable
#endif
