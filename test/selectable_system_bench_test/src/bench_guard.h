#ifndef SAVER_DEPTH_BENCH_GUARD_H
#define SAVER_DEPTH_BENCH_GUARD_H

#include "bench_config.h"

namespace saver_depth_bench {
constexpr uint32_t CONTROL_US = 5000;        // 200 Hz local ADC/current PID.
constexpr uint32_t CONTROL_STALE_US = 20000; // Scheduling deadline, not medical T_comm.
constexpr uint32_t ARM_MS = 10000;
constexpr uint32_t STABLE_MS = 250;
constexpr uint32_t ABSOLUTE_RUN_MS = 600000; // User-selected finite 10-minute bench ceiling.
constexpr float absValue(float v) { return v < 0.0f ? -v : v; }
constexpr bool positive(float v) { return saver_depth::finite(v) && v > 0.0f; }
constexpr bool levelValid(int8_t level) { return level == 0 || level == 1; }
constexpr bool limitsReady(bool required, bool installed) { return !required || installed; }
constexpr bool limitPolarityReady(bool installed, int8_t top, int8_t bottom) {
  return !installed || (levelValid(top) && levelValid(bottom));
}
constexpr bool estopAuxReady(bool installed, bool verified, int8_t activeLevel) {
  return !installed || (verified && levelValid(activeLevel));
}
constexpr bool configuredInputsReady() {
  using namespace saver_depth_config;
  return PHYSICAL_ESTOP_POWER_CUT_USER_CONFIRMED &&
         limitsReady(LIMIT_SWITCHES_REQUIRED, LIMIT_SWITCHES_INSTALLED) &&
         limitPolarityReady(LIMIT_SWITCHES_INSTALLED, TOP_ACTIVE_LEVEL, BOTTOM_ACTIVE_LEVEL) &&
         estopAuxReady(ESTOP_AUX_INSTALLED, ESTOP_AUX_INPUT_VERIFIED, ESTOP_ACTIVE_LEVEL);
}
constexpr float erpmPerMmS() {
  return 60.0f * saver_depth_config::MOTOR_REVS_PER_SCREW_REV *
         saver_depth_config::MOTOR_POLE_PAIRS / saver_depth_config::SCREW_LEAD_MM;
}

// The same checks drive both the run interlock and the complete CHECK report.
// Adding a report must never create a second, weaker set of run conditions.
constexpr uint8_t CONFIG_CHECK_COUNT = 14;
struct ConfigCheck {
  const char *reason;
  bool passed;
};
constexpr ConfigCheck configCheck(uint8_t index, bool calibrated = false) {
  using namespace saver_depth_config;
  switch (index) {
  case 0:
    return {"ZERO_AND_CAL_REQUIRED", calibrated};
  case 1:
    return {LIMIT_SWITCHES_REQUIRED ? "LIMIT_SWITCHES_MISSING" : "LIMIT_SWITCHES_OPTIONAL_BENCH",
            limitsReady(LIMIT_SWITCHES_REQUIRED, LIMIT_SWITCHES_INSTALLED)};
  case 2:
    return {"PHYSICAL_ESTOP_UNVERIFIED", PHYSICAL_ESTOP_POWER_CUT_USER_CONFIRMED};
  case 3:
    return {"INPUT_POLARITY_UNKNOWN",
            limitPolarityReady(LIMIT_SWITCHES_INSTALLED, TOP_ACTIVE_LEVEL, BOTTOM_ACTIVE_LEVEL) &&
                estopAuxReady(ESTOP_AUX_INSTALLED, ESTOP_AUX_INPUT_VERIFIED, ESTOP_ACTIVE_LEVEL)};
  case 4:
    return {"SENSOR_FAULT_TEST_UNVERIFIED", SENSOR_WIRING_AND_FAULT_TEST_VERIFIED};
  case 5:
    return {"MOTOR_DIRECTION_UNVERIFIED",
            MOTOR_DIRECTION_VERIFIED &&
                (CURRENT_SIGN_FOR_INCREASING_MM == 1 || CURRENT_SIGN_FOR_INCREASING_MM == -1)};
  case 6:
    return {"VESC_SETTINGS_UNVERIFIED", VESC_SETTINGS_AND_TIMEOUT_VERIFIED};
  case 7:
    return {"SOFT_TRAVEL_NOT_SET", saver_depth::finite(SOFT_MIN_MM) &&
                                       saver_depth::finite(SOFT_MAX_MM) && SOFT_MIN_MM < 0 &&
                                       SOFT_MAX_MM > 0 && SOFT_MAX_MM - SOFT_MIN_MM <= 75};
  case 8:
    return {"FEEDBACK_LIMITS_NOT_SET",
            positive(START_TOLERANCE_MM) && positive(MAX_START_VELOCITY_MM_S) &&
                positive(MAX_FOLLOWING_ERROR_MM) && positive(MAX_MEASURED_VELOCITY_MM_S) &&
                positive(SAMPLE_NOISE_ALLOWANCE_MM)};
  case 9:
    return {"CURRENT_PID_OR_SETTINGS_UNVERIFIED", CURRENT_CONTROL_SETTINGS_VERIFIED &&
                                                      saver_current::validGains(CURRENT_PID) &&
                                                      positive(POSITION_FILTER_TAU_S)};
  case 10:
    return {"PROFILE_MOTION_LIMITS_NOT_SET",
            positive(MAX_PROFILE_VELOCITY_MM_S) && positive(MAX_PROFILE_ACCELERATION_MM_S2)};
  case 11:
    return {"RUN_TIME_LIMIT_NOT_SET", MAX_RUN_MS > 0 && MAX_RUN_MS <= ABSOLUTE_RUN_MS};
  case 12:
    return {
        "ELECTRICAL_LIMITS_NOT_SET",
        CURRENT_CONTROL_SETTINGS_VERIFIED && positive(MIN_INPUT_V) && positive(MAX_INPUT_V) &&
            MIN_INPUT_V < MAX_INPUT_V && positive(MAX_MOTOR_CURRENT_A) &&
            positive(MAX_FET_TEMP_C) && positive(DEFAULT_COMMAND_CURRENT_A) &&
            DEFAULT_COMMAND_CURRENT_A <= MAX_ALLOWED_COMMAND_CURRENT_A &&
            MAX_ALLOWED_COMMAND_CURRENT_A < MAX_MOTOR_CURRENT_A && positive(DEFAULT_INPUT_TRIP_A) &&
            DEFAULT_INPUT_TRIP_A <= MAX_ALLOWED_INPUT_TRIP_A && positive(MAX_ALLOWED_INPUT_TRIP_A)};
  case 13:
    return {"BENCH_DRIVE_LOCKED", BENCH_DRIVE_CONFIRMED};
  default:
    return {"INVALID_CONFIG_CHECK", false};
  }
}
constexpr const char *configIssue(bool calibrated = false) {
  for (uint8_t i = 0; i < CONFIG_CHECK_COUNT; ++i)
    if (!configCheck(i, calibrated).passed)
      return configCheck(i, calibrated).reason;
  return nullptr;
}

constexpr bool validTarget(float depth, float cpm) {
  return positive(depth) && positive(cpm) && depth <= saver_depth_config::REQUEST_MAX_DEPTH_MM &&
         cpm <= saver_depth_config::REQUEST_MAX_RATE_CPM;
}
constexpr bool withinCalibration(float originMm, float depthMm, const saver_depth::Calibration &c) {
  if (!saver_depth::validCalibration(c) || !saver_depth::finite(originMm) || !positive(depthMm))
    return false;
  const float low = c.mm0 < c.mm1 ? c.mm0 : c.mm1;
  const float high = c.mm0 > c.mm1 ? c.mm0 : c.mm1;
  return originMm > low && originMm + depthMm < high;
}
constexpr bool validDuration(float cpm, uint32_t cycles, uint32_t limitMs) {
  // Divide before multiplying in floating point: uint32 cycle input cannot overflow.
  return positive(cpm) && cycles > 0 && limitMs > 0 && limitMs <= ABSOLUTE_RUN_MS &&
         static_cast<double>(cycles) * 60000.0 / cpm <= limitMs;
}

// Entire command tokens only: no NaN, exponent, signs, trailing garbage or overflow.
constexpr bool parseNumber(const char *p, float &out) {
  if (!p || !*p)
    return false;
  float value = 0.0f, scale = 1.0f;
  bool dot = false, digit = false;
  for (; *p; ++p) {
    if (*p == '.' && !dot) {
      dot = true;
      continue;
    }
    if (*p < '0' || *p > '9')
      return false;
    digit = true;
    if (dot) {
      scale *= 0.1f;
      value += (*p - '0') * scale;
    } else
      value = value * 10.0f + (*p - '0');
    if (!saver_depth::finite(value) || value > 1000000.0f)
      return false;
  }
  if (!digit)
    return false;
  out = value;
  return true;
}
constexpr bool parseCycles(const char *p, uint32_t &out) {
  if (!p || !*p)
    return false;
  uint32_t value = 0;
  for (; *p; ++p) {
    if (*p < '0' || *p > '9')
      return false;
    const uint32_t d = *p - '0';
    if (value > (UINT32_MAX - d) / 10U)
      return false;
    value = value * 10U + d;
  }
  if (!value)
    return false;
  out = value;
  return true;
}

// Pure command latch; conditions must be freshly checked by the sketch for every action.
class RunGate {
public:
  constexpr void stop() {
    armed_ = false;
    running_ = false;
  }
  constexpr void trip() {
    stop();
    fault_ = true;
  }
  constexpr bool clear(bool healthy) {
    const bool active = running_;
    stop();
    if (active || !healthy) {
      fault_ = true;
      return false;
    }
    fault_ = false;
    return true;
  }
  constexpr bool arm(bool ready, uint32_t now) {
    const bool active = running_;
    stop();
    if (active || !ready || fault_)
      return false;
    armed_ = true;
    armedAt_ = now;
    return true;
  }
  constexpr bool start(bool ready, uint32_t now) {
    if (running_ || !armed_ || fault_ || !ready ||
        static_cast<uint32_t>(now - armedAt_) >= ARM_MS) {
      trip();
      return false;
    }
    running_ = true;
    return true;
  }
  constexpr bool expire(uint32_t now) {
    if (armed_ && !running_ && static_cast<uint32_t>(now - armedAt_) >= ARM_MS) {
      stop();
      return true;
    }
    return false;
  }
  constexpr bool armed() const { return armed_; }
  constexpr bool running() const { return running_; }
  constexpr bool fault() const { return fault_; }

private:
  bool armed_ = false, running_ = false, fault_ = false;
  uint32_t armedAt_ = 0;
};
} // namespace saver_depth_bench
#endif
