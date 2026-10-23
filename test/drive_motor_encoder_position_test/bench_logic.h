#ifndef SAVER_DRIVE_MOTOR_ENCODER_POSITION_BENCH_LOGIC_H
#define SAVER_DRIVE_MOTOR_ENCODER_POSITION_BENCH_LOGIC_H

#include <stdint.h>

namespace saver_drive_position_bench {

// AB states are encoded as A in bit 1 and B in bit 0. A two-bit change is
// illegal for a correctly sampled quadrature sequence and returns zero.
constexpr int8_t quadratureDelta(uint8_t previous, uint8_t current) {
  return previous == current ? 0 :
         previous == 0U && current == 1U ? -1 :
         previous == 1U && current == 3U ? -1 :
         previous == 3U && current == 2U ? -1 :
         previous == 2U && current == 0U ? -1 :
         previous == 0U && current == 2U ? 1 :
         previous == 2U && current == 3U ? 1 :
         previous == 3U && current == 1U ? 1 :
         previous == 1U && current == 0U ? 1 : 0;
}

constexpr bool illegalQuadratureTransition(uint8_t previous, uint8_t current) {
  return previous != current && quadratureDelta(previous, current) == 0;
}

constexpr uint8_t pwmRaw8(uint8_t percent) {
  return static_cast<uint8_t>((static_cast<uint16_t>(percent) * 255U + 50U) / 100U);
}

constexpr uint8_t dutyForRemaining(int32_t remaining, int32_t slowZone,
                                   uint8_t cruiseDuty, uint8_t slowDuty) {
  return remaining <= slowZone ? slowDuty : cruiseDuty;
}

constexpr bool targetAllowed(int32_t target, int32_t maximum) {
  return maximum > 0 && target > 0 && target <= maximum;
}

constexpr bool wrongOutboundDirection(int32_t position, int32_t start, int32_t tolerance) {
  return position < start - tolerance;
}

constexpr bool wrongReturnDirection(int32_t position, int32_t returnStart, int32_t tolerance) {
  return position > returnStart + tolerance;
}

constexpr bool withinTolerance(int32_t error, int32_t tolerance) {
  return error >= -tolerance && error <= tolerance;
}

} // namespace saver_drive_position_bench

#endif
