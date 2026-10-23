#ifndef SAVER_VACUUM_COMPRESSION_COMBINED_BENCH_LOGIC_H
#define SAVER_VACUUM_COMPRESSION_COMBINED_BENCH_LOGIC_H

#include <stdint.h>

namespace saver_combined_bench {

// Local engineering limits for the first simultaneous power/noise bench run.
// They are not medical settings or verified hardware ratings.
constexpr uint32_t MIN_RUN_MS = 100;
constexpr uint32_t VAC_ERR_CLEAR_MS = 250;

struct RunAllLimits {
  uint8_t maxVacuumPercent;
  uint8_t maxCompressionPercent;
  uint32_t maxRunMs;
};

struct RunAllRequest {
  uint8_t vacuum1Percent;
  uint8_t vacuum2Percent;
  int16_t compressionPercent;
  uint32_t durationMs;
};

enum class RoundTripPhase : uint8_t { Idle, FirstLeg, WaitForZero, ReturnLeg };
enum class RoundTripAction : uint8_t { None, StopForDwell, StartReturn, Complete, ZeroTimeout };

constexpr RoundTripAction nextRoundTripAction(RoundTripPhase phase, uint32_t elapsedMs,
                                               uint32_t legMs, bool zeroReady,
                                               uint32_t dwellMs, uint32_t zeroTimeoutMs) {
  if (phase == RoundTripPhase::FirstLeg && elapsedMs >= legMs)
    return RoundTripAction::StopForDwell;
  if (phase == RoundTripPhase::WaitForZero) {
    if (elapsedMs >= zeroTimeoutMs)
      return RoundTripAction::ZeroTimeout;
    if (elapsedMs >= dwellMs && zeroReady)
      return RoundTripAction::StartReturn;
  }
  if (phase == RoundTripPhase::ReturnLeg && elapsedMs >= legMs)
    return RoundTripAction::Complete;
  return RoundTripAction::None;
}

constexpr uint8_t dutyToPwm(uint8_t percent) {
  return static_cast<uint8_t>((static_cast<uint16_t>(percent) * 255U + 50U) / 100U);
}

constexpr uint16_t magnitude(int16_t value) {
  return value < 0 ? static_cast<uint16_t>(-static_cast<int32_t>(value))
                   : static_cast<uint16_t>(value);
}

constexpr int16_t oppositeDuty(int16_t value) {
  return static_cast<int16_t>(-static_cast<int32_t>(value));
}

constexpr const char *validate(const RunAllRequest &request, const RunAllLimits &limits) {
  if (!limits.maxVacuumPercent || !limits.maxCompressionPercent ||
      limits.maxVacuumPercent > 100 || limits.maxCompressionPercent > 100 ||
      limits.maxRunMs < MIN_RUN_MS)
    return "CONFIG_LOCKED";
  if (!request.vacuum1Percent || !request.vacuum2Percent ||
      request.vacuum1Percent > limits.maxVacuumPercent ||
      request.vacuum2Percent > limits.maxVacuumPercent)
    return "VACUUM_DUTY_OUT_OF_RANGE";
  if (!request.compressionPercent ||
      magnitude(request.compressionPercent) > limits.maxCompressionPercent)
    return "COMPRESSION_DUTY_OUT_OF_RANGE";
  if (request.durationMs < MIN_RUN_MS || request.durationMs > limits.maxRunMs)
    return "DURATION_OUT_OF_RANGE";
  return nullptr;
}

class VacuumInterlock {
public:
  constexpr uint8_t observe(bool err1Active, bool err2Active, uint32_t now) {
    const bool active[2] = {err1Active, err2Active};
    uint8_t newlyLatched = 0;
    for (uint8_t i = 0; i < 2; ++i) {
      if (active[i]) {
        if (!latched_[i])
          newlyLatched |= static_cast<uint8_t>(1U << i);
        latched_[i] = true;
        healthyTracking_[i] = false;
      } else if (!healthyTracking_[i]) {
        healthyTracking_[i] = true;
        healthySince_[i] = now;
      }
    }
    return newlyLatched;
  }

  constexpr bool inputsHealthy(uint32_t now) const {
    return healthyTracking_[0] && healthyTracking_[1] &&
           static_cast<uint32_t>(now - healthySince_[0]) >= VAC_ERR_CLEAR_MS &&
           static_cast<uint32_t>(now - healthySince_[1]) >= VAC_ERR_CLEAR_MS;
  }

  constexpr bool ready(uint32_t now) const {
    return !latched_[0] && !latched_[1] && inputsHealthy(now);
  }

  constexpr const char *clear(uint32_t now) {
    if (!inputsHealthy(now))
      return "VACUUM_ERR_NOT_HIGH_FOR_250MS";
    latched_[0] = false;
    latched_[1] = false;
    return nullptr;
  }

  constexpr bool fault(unsigned index) const { return index < 2 && latched_[index]; }

private:
  bool latched_[2] = {false, false};
  bool healthyTracking_[2] = {false, false};
  uint32_t healthySince_[2] = {0, 0};
};

} // namespace saver_combined_bench

#endif
