#include "../combined_bench_logic.h"

using namespace saver_combined_bench;

constexpr RunAllLimits limits = {22, 50, 2000};
constexpr RunAllRequest nominal = {6, 6, 10, 1000};
static_assert(validate(nominal, limits) == nullptr, "nominal run accepted");
static_assert(validate({0, 6, 10, 1000}, limits) != nullptr, "vacuum zero rejected");
static_assert(validate({22, 22, 10, 1000}, limits) == nullptr, "measured vacuum point accepted");
static_assert(validate({23, 6, 10, 1000}, limits) != nullptr, "vacuum ceiling enforced");
static_assert(validate({6, 6, 0, 1000}, limits) != nullptr, "compression zero rejected");
static_assert(validate({6, 6, -50, 1000}, limits) == nullptr, "negative direction accepted");
static_assert(validate({6, 6, 51, 1000}, limits) != nullptr, "compression ceiling enforced");
static_assert(validate({6, 6, 10, 99}, limits) != nullptr, "minimum duration enforced");
static_assert(validate({6, 6, 10, 2001}, limits) != nullptr, "maximum duration enforced");
static_assert(dutyToPwm(6) == 15, "six percent PWM rounding");
static_assert(dutyToPwm(10) == 26, "ten percent PWM rounding");
static_assert(oppositeDuty(10) == -10, "positive first leg reverses negative");
static_assert(oppositeDuty(-50) == 50, "negative first leg reverses positive");
static_assert(nextRoundTripAction(RoundTripPhase::FirstLeg, 499, 500, false, 500, 2000) ==
                  RoundTripAction::None,
              "first leg remains active before deadline");
static_assert(nextRoundTripAction(RoundTripPhase::FirstLeg, 500, 500, false, 500, 2000) ==
                  RoundTripAction::StopForDwell,
              "first leg stops at deadline");
static_assert(nextRoundTripAction(RoundTripPhase::WaitForZero, 500, 500, false, 500, 2000) ==
                  RoundTripAction::None,
              "elapsed dwell alone cannot reverse");
static_assert(nextRoundTripAction(RoundTripPhase::WaitForZero, 500, 500, true, 500, 2000) ==
                  RoundTripAction::StartReturn,
              "stable zero after dwell permits return");
static_assert(nextRoundTripAction(RoundTripPhase::WaitForZero, 2000, 500, true, 500, 2000) ==
                  RoundTripAction::ZeroTimeout,
              "zero wait ceiling fails closed");
static_assert(nextRoundTripAction(RoundTripPhase::ReturnLeg, 500, 500, false, 500, 2000) ==
                  RoundTripAction::Complete,
              "return leg stops at deadline");

constexpr bool healthySequence() {
  VacuumInterlock interlock;
  return interlock.observe(false, false, 100) == 0 && !interlock.ready(349) &&
         interlock.ready(350);
}
static_assert(healthySequence(), "both ERR inputs must be HIGH for 250 ms");

constexpr bool faultSequence() {
  VacuumInterlock interlock;
  interlock.observe(false, false, 0);
  const uint8_t first = interlock.observe(true, false, 300);
  const uint8_t repeat = interlock.observe(true, false, 301);
  interlock.observe(false, false, 400);
  return first == 1 && repeat == 0 && !interlock.ready(650) &&
         interlock.clear(650) == nullptr && interlock.ready(650);
}
static_assert(faultSequence(), "vacuum fault latches until stable-high clear");

int main() { return 0; }
