// Shared-code software assertions only. NO motor, ADC, GPIO or UART access.
#define PI 3.1415926535897932384626433832795 // Arduino macro regression: header must coexist.
#include "../depth_control.h"
#include <limits>

using namespace saver_depth;

constexpr bool near(float actual, float expected, float tolerance = 0.0001f) {
  return actual >= expected - tolerance && actual <= expected + tolerance;
}
constexpr Calibration confirmed(int raw0 = 100, int raw1 = 4000, float mm0 = 0.0f,
                                float mm1 = 75.0f) {
  return Calibration{raw0, raw1, mm0, mm1, true};
}
constexpr Gains testGains(float kp = 1.0f, float ki = 0.0f, float kd = 0.0f, float speed = 100.0f,
                          float acceleration = 1000.0f, float integral = 0.0f) {
  return Gains{kp, ki, kd, speed, acceleration, integral};
}
constexpr float quietNan = std::numeric_limits<float>::quiet_NaN();
constexpr float positiveInfinity = std::numeric_limits<float>::infinity();

static_assert(!saver_depth::finite(quietNan) && !saver_depth::finite(positiveInfinity) &&
                  !saver_depth::finite(-positiveInfinity),
              "Reject nonfinite values");
static_assert(saver_depth::finite(FLT_MAX) && saver_depth::finite(-FLT_MAX),
              "Finite endpoint representation");
static_assert(!validCalibration(Calibration{}), "Nominal defaults are NOT a calibration");
static_assert(!convert(2048, Calibration{}).calibrated, "No inferred sensor calibration");
static_assert(near(convert(2048, Calibration{}).nominalMm, 37.509157f),
              "75 mm nominal diagnostic conversion");
static_assert(near(convert(4095, Calibration{}).adcV, 3.3f), "Nominal full-scale voltage");
static_assert(near(convert(0, Calibration{}).nominalMm, 0.0f), "Nominal zero remains diagnostic");
static_assert(!convert(-1, confirmed()).rawOk && !convert(4096, confirmed()).rawOk,
              "ADC range invalid");
static_assert(convert(0, confirmed()).rail && convert(1, confirmed()).rail, "Low ADC rail guard");
static_assert(convert(4094, confirmed()).rail && convert(4095, confirmed()).rail,
              "High ADC rail guard");
static_assert(!convert(1, confirmed(0, 4095)).calibrated, "Low rail cannot control");
static_assert(!convert(4094, confirmed(0, 4095)).calibrated, "High rail cannot control");
static_assert(convert(2, confirmed(0, 4095)).calibrated, "First interior ADC code");
static_assert(convert(4093, confirmed(0, 4095)).calibrated, "Last interior ADC code");
static_assert(convert(100, confirmed()).calibrated && near(convert(100, confirmed()).mm, 0),
              "Calibrated first endpoint");
static_assert(convert(4000, confirmed()).calibrated && near(convert(4000, confirmed()).mm, 75),
              "Calibrated second endpoint");
static_assert(near(convert(2050, confirmed()).mm, 37.5f), "Calibrated midpoint");
static_assert(!convert(99, confirmed()).calibrated && !convert(4001, confirmed()).calibrated,
              "No extrapolation or clamp");
static_assert(near(convert(100, confirmed(4000, 100)).mm, 75), "Reversed raw direction");
static_assert(near(convert(100, confirmed(100, 4000, 75, 0)).mm, 75),
              "Reversed physical direction");
static_assert(near(convert(2050, confirmed(100, 4000, 10, 60)).mm, 35), "Partial-span calibration");
static_assert(!validCalibration(confirmed(100, 100)), "Zero ADC calibration span");
static_assert(!validCalibration(confirmed(100, 4000, 5, 5)), "Zero physical calibration span");
static_assert(!validCalibration(confirmed(-1, 4000)) && !validCalibration(confirmed(100, 4096)),
              "Calibration ADC endpoints bounded");
static_assert(!validCalibration(confirmed(100, 4000, -1, 75)) &&
                  !validCalibration(confirmed(100, 4000, 0, 76)),
              "Calibration physical endpoints bounded");
static_assert(!validCalibration(confirmed(100, 4000, quietNan, 75)), "NaN calibration rejected");
static_assert(!validCalibration(confirmed(100, 4000, 0, positiveInfinity)),
              "Infinite calibration rejected");
static_assert(near(peakVelocity(50, 110), 287.97934f, 0.001f),
              "Requested bench reference peak mm/s");
static_assert(near(peakAcceleration(50, 110), 3317.2837f, 0.01f),
              "Requested bench reference peak mm/s^2");
static_assert(near(erpmFromVelocity(10), 360) && near(erpmFromVelocity(-10), -360),
              "Electrical RPM conversion direction and ratio");
static_assert(near(erpmFromVelocity(peakVelocity(50, 110)), 10367.256f, 0.01f),
              "Four-claimed-pole-pair bench peak ERPM");
static_assert(validReference(10, 50, 110, 1), "Explicit finite bench profile");
static_assert(!validReference(26, 50, 110, 1), "Full path must stay inside nominal span");
static_assert(!validReference(0, 0, 110, 1) && !validReference(0, 50, 0, 1),
              "Unconfigured profile disabled");
static_assert(!validReference(0, 50, 110, 0), "No implicit continuous run");
static_assert(!validReference(-1, 50, 110, 1) && !validReference(0, -1, 110, 1),
              "Negative position/depth rejected");
static_assert(!validReference(0, 50, -1, 1), "Negative rate rejected");
static_assert(!validReference(quietNan, 50, 110, 1) && !validReference(0, positiveInfinity, 110, 1),
              "Nonfinite profile rejected");
static_assert(!validReference(0, 50, quietNan, 1), "Nonfinite cycle rate rejected");
static_assert(!validReference(0, 50, 1, UINT32_MAX),
              "Run duration cannot exceed elapsed clock interval");
static_assert(!validReference(0, 50, 100000000, 1), "Sub-microsecond duration rejected");

constexpr bool referenceCases() {
  const Reference start = reference(10, 50, 60, 0, 2);
  const Reference quarter = reference(10, 50, 60, 250000, 2);
  const Reference bottom = reference(10, 50, 60, 500000, 2);
  const Reference release = reference(10, 50, 60, 750000, 2);
  const Reference next = reference(10, 50, 60, 1000000, 2);
  const Reference end = reference(10, 50, 60, 2000000, 2);
  const Reference after = reference(10, 50, 60, 2500000, 2);
  const uint32_t startedUs = UINT32_MAX - 100000;
  const uint32_t nowUs = startedUs + 250000U;
  const Reference rollover = reference(10, 50, 60, static_cast<uint32_t>(nowUs - startedUs), 2);
  return !start.finished && near(start.mm, 10) && near(start.velocityMmS, 0) &&
         near(quarter.mm, 35) && near(quarter.velocityMmS, peakVelocity(50, 60)) &&
         near(bottom.mm, 60) && near(bottom.velocityMmS, 0) && near(release.mm, 35) &&
         near(release.velocityMmS, -peakVelocity(50, 60)) && !next.finished && near(next.mm, 10) &&
         near(next.velocityMmS, 0) && end.finished && near(end.mm, 10) &&
         near(end.velocityMmS, 0) && after.finished && near(after.mm, 10) &&
         near(after.velocityMmS, 0) && near(rollover.mm, quarter.mm) &&
         near(rollover.velocityMmS, quarter.velocityMmS);
}
// GCC's constant-expression math builtins exercise the same reference() used
// by the sketch. Non-GCC host compilers execute referenceCases() from main.
#if defined(__GNUC__) && !defined(__clang__)
static_assert(referenceCases(),
              "Actual sin/cos reference endpoints, signs, cycles and timer rollover");
#endif
static_assert(reference(10, 0, 60, 0, 1).finished, "Invalid reference immediately finished");
static_assert(reference(quietNan, 50, 60, 0, 1).mm == 0, "Invalid origin yields finite sentinel");

static_assert(!validGains(Gains{}), "No invented controller gains or output caps");
static_assert(validGains(testGains()), "Explicit proportional bench fixture valid");
static_assert(!validGains(testGains(0)), "Zero Kp disables control");
static_assert(!validGains(testGains(-1)) && !validGains(testGains(1, -1)), "Negative P/I rejected");
static_assert(!validGains(testGains(1, 0, -1)), "Negative derivative rejected");
static_assert(!validGains(testGains(1, 0, 0, 0)), "Velocity cap required");
static_assert(!validGains(testGains(1, 0, 0, 100, 0)), "Acceleration cap required");
static_assert(!validGains(testGains(1, 1)), "Active integrator requires bound");
static_assert(!validGains(testGains(1, 0, 0, 100, 1000, -1)), "Negative integral bound rejected");
static_assert(!validGains(testGains(quietNan)) && !validGains(testGains(1, positiveInfinity)),
              "Nonfinite gains rejected");
static_assert(!validGains(testGains(1, 0, 0, positiveInfinity)), "Nonfinite cap rejected");

constexpr bool proportionalAndFeedforward() {
  Pid pid;
  pid.reset(10);
  return near(pid.step(15, 3, 10, 0.1f, testGains(2)), 13) && pid.valid();
}
constexpr bool derivativeOnMeasurement() {
  Pid pid;
  pid.reset(10);
  const float before = pid.step(10, 0, 10, 0.1f, testGains(1, 0, 2));
  const float targetStep = pid.step(20, 0, 10, 0.1f, testGains(1, 0, 2));
  const float measuredStep = pid.step(20, 0, 11, 0.1f, testGains(1, 0, 2));
  return near(before, 0) && near(targetStep, 10) && near(measuredStep, -11);
}
constexpr bool speedSaturation() {
  Pid pid;
  pid.reset(0);
  const Gains gains = testGains(10, 0, 0, 5);
  return near(pid.step(10, 0, 0, 1, gains), 5) && near(pid.step(-10, 0, 0, 1, gains), -5);
}
constexpr bool slewAndReversal() {
  Pid pid;
  pid.reset(0);
  const Gains gains = testGains(10, 0, 0, 100, 10);
  const float first = pid.step(20, 0, 0, 0.1f, gains);
  const float second = pid.step(20, 0, 0, 0.1f, gains);
  const float reverse = pid.step(-20, 0, 0, 0.1f, gains);
  return near(first, 1) && near(second, 2) && near(reverse, 1);
}
constexpr bool antiwindupSpeed() {
  Pid pid;
  pid.reset(0);
  const Gains gains = testGains(1, 1, 0, 5, 1000, 10);
  for (int i = 0; i < 100; ++i)
    pid.step(10, 0, 0, 0.1f, gains);
  return near(pid.outputMmS(), 5) && near(pid.integralMmS(), 0) &&
         near(pid.step(0, 0, 0, 0.1f, gains), 0);
}
constexpr bool antiwindupSlew() {
  Pid pid;
  pid.reset(0);
  const Gains gains = testGains(1, 1, 0, 100, 1, 10);
  pid.step(10, 0, 0, 0.1f, gains);
  return near(pid.outputMmS(), 0.1f) && near(pid.integralMmS(), 0);
}
constexpr bool integralLimitAndUnwind() {
  Pid pid;
  pid.reset(0);
  const Gains gains = testGains(1, 1, 0, 100, 1000, 2);
  for (int i = 0; i < 30; ++i)
    pid.step(1, 0, 0, 0.1f, gains);
  const float peakIntegral = pid.integralMmS();
  pid.step(-1, 0, 0, 0.1f, gains);
  return near(peakIntegral, 2) && near(pid.integralMmS(), 1.9f);
}
constexpr bool unwindWhileSaturated() {
  Pid pid;
  pid.reset(0);
  const Gains gains = testGains(1, 1, 0, 5, 1000, 3);
  pid.step(1, 0, 0, 1, gains);      // Store +1 mm/s integral while unsaturated.
  pid.step(-1, 10, 0, 0.1f, gains); // Positive FF saturation, negative integration.
  return near(pid.outputMmS(), 5) && near(pid.integralMmS(), 0.9f);
}
constexpr bool resetClearsHistory() {
  Pid pid;
  pid.reset(0);
  pid.step(1, 0, 0, 1, testGains(1, 1, 1, 100, 1000, 3));
  pid.reset(20);
  return near(pid.integralMmS(), 0) && near(pid.outputMmS(), 0) &&
         near(pid.step(20, 0, 20, 0.1f, testGains(1, 0, 1)), 0);
}
constexpr bool invalidStep(float target, float velocity, float measured, float dt,
                           const Gains &gains) {
  Pid pid;
  pid.reset(0);
  pid.step(1, 0, 0, 1, testGains(1, 1, 0, 100, 1000, 3));
  const float stopped = pid.step(target, velocity, measured, dt, gains);
  return stopped == 0 && pid.outputMmS() == 0 && pid.integralMmS() == 0 && !pid.valid();
}
static_assert(proportionalAndFeedforward(), "Position error plus velocity feedforward");
static_assert(derivativeOnMeasurement(), "Target step has no derivative kick");
static_assert(speedSaturation(), "Signed velocity output cap");
static_assert(slewAndReversal(), "Signed acceleration slew and reversal");
static_assert(antiwindupSpeed(), "Integrator freezes at velocity saturation");
static_assert(antiwindupSlew(), "Integrator freezes at acceleration saturation");
static_assert(integralLimitAndUnwind(), "Bounded integral and error reversal");
static_assert(unwindWhileSaturated(), "Unwind is allowed while still saturated");
static_assert(resetClearsHistory(), "Reset clears integral and measurement derivative history");
static_assert(invalidStep(0, 0, 0, 0, testGains()), "Zero dt immediately stops");
static_assert(invalidStep(0, 0, 0, -1, testGains()), "Negative dt immediately stops");
static_assert(invalidStep(0, 0, 0, quietNan, testGains()), "NaN dt immediately stops");
static_assert(invalidStep(quietNan, 0, 0, 1, testGains()), "NaN target immediately stops");
static_assert(invalidStep(0, positiveInfinity, 0, 1, testGains()),
              "Infinite feedforward immediately stops");
static_assert(invalidStep(0, 0, quietNan, 1, testGains()), "NaN measurement immediately stops");
static_assert(invalidStep(0, 0, 0, 1, Gains{}), "Unconfigured gains immediately stop");

int main() {
  // A native build can execute the same suite, including runtime libm reference.
  return referenceCases() && proportionalAndFeedforward() && derivativeOnMeasurement() &&
                 speedSaturation() && slewAndReversal() && antiwindupSpeed() && antiwindupSlew() &&
                 integralLimitAndUnwind() && unwindWhileSaturated() && resetClearsHistory()
             ? 0
             : 1;
}
