// Arduino-free, bench-only position conversion and outer depth loop.
// This computes a velocity request; it cannot establish motor/sensor safety.
#ifndef SAVER_DEPTH_CONTROL_H
#define SAVER_DEPTH_CONTROL_H

#include <float.h>
#include <math.h>
#include <stdint.h>

namespace saver_depth {

constexpr float NOMINAL_SPAN_MM = 75.0f;
constexpr int ADC_MAX = 4095;
constexpr double TRAJECTORY_PI = 3.14159265358979323846; // Avoid Arduino's PI macro.

constexpr bool finite(double value) { return value >= -DBL_MAX && value <= DBL_MAX; }
constexpr double clamp(double value, double low, double high) {
  return value < low ? low : (value > high ? high : value);
}

struct Calibration {
  int raw0 = 0, raw1 = ADC_MAX;
  float mm0 = 0.0f, mm1 = NOMINAL_SPAN_MM;
  bool confirmed = false;
};

struct Position {
  bool rawOk = false, rail = false, calibrated = false;
  int raw = 0;
  float adcV = 0.0f, nominalMm = 0.0f, mm = 0.0f;
};

constexpr bool validCalibration(const Calibration &calibration) {
  return calibration.confirmed && calibration.raw0 >= 0 && calibration.raw0 <= ADC_MAX &&
         calibration.raw1 >= 0 && calibration.raw1 <= ADC_MAX &&
         calibration.raw0 != calibration.raw1 && finite(calibration.mm0) &&
         finite(calibration.mm1) && calibration.mm0 >= 0.0f && calibration.mm0 <= NOMINAL_SPAN_MM &&
         calibration.mm1 >= 0.0f && calibration.mm1 <= NOMINAL_SPAN_MM &&
         calibration.mm0 != calibration.mm1;
}

constexpr Position convert(int raw, const Calibration &calibration) {
  Position result;
  result.raw = raw;
  result.rawOk = raw >= 0 && raw <= ADC_MAX;
  if (!result.rawOk)
    return result;
  result.adcV = static_cast<float>(raw * 3.3 / ADC_MAX);
  // Diagnostic nominal conversion only: NOT calibrated travel or presence.
  result.nominalMm = static_cast<float>(raw * 75.0 / ADC_MAX);
  // Two end codes on each rail are rejected, not clamped into a valid position.
  // This is an ADC plausibility guard, not a sensor disconnect detector.
  result.rail = raw <= 1 || raw >= ADC_MAX - 1;
  const int low = calibration.raw0 < calibration.raw1 ? calibration.raw0 : calibration.raw1;
  const int high = calibration.raw0 > calibration.raw1 ? calibration.raw0 : calibration.raw1;
  if (result.rail || !validCalibration(calibration) || raw < low || raw > high)
    return result;
  result.mm = static_cast<float>(calibration.mm0 + (static_cast<double>(raw) - calibration.raw0) *
                                                       (calibration.mm1 - calibration.mm0) /
                                                       (calibration.raw1 - calibration.raw0));
  result.calibrated = true;
  return result; // Callers must omit mm unless calibrated is true.
}

// D is full origin-to-bottom travel; f is cycles per second.
constexpr float peakVelocity(float depthMm, float cpm) {
  return static_cast<float>(TRAJECTORY_PI * depthMm * cpm / 60.0);
}
constexpr float peakAcceleration(float depthMm, float cpm) {
  return static_cast<float>(2.0 * TRAJECTORY_PI * TRAJECTORY_PI * depthMm * (cpm / 60.0) *
                            (cpm / 60.0));
}
// Assumes motor/screw speed ratio 1.5, lead 10 mm/rev, and USER-CLAIMED
// four pole pairs. This is electrical rpm, not mechanical motor rpm.
constexpr float erpmFromVelocity(float velocityMmS) {
  return static_cast<float>(static_cast<double>(velocityMmS) * 60.0 * 1.5 / 10.0 * 4.0);
}

struct Reference {
  float mm = 0.0f, velocityMmS = 0.0f;
  bool finished = true;
};

constexpr bool validReference(float originMm, float depthMm, float cpm, uint32_t cycles) {
  if (!finite(originMm) || !finite(depthMm) || !finite(cpm) || originMm < 0.0f || depthMm <= 0.0f ||
      cpm <= 0.0f || cycles == 0 || static_cast<double>(originMm) + depthMm > NOMINAL_SPAN_MM)
    return false;
  const double durationUs = static_cast<double>(cycles) * 60000000.0 / cpm;
  // One uint32_t elapsed-time interval must contain the complete run.
  return durationUs >= 1.0 && durationUs <= UINT32_MAX &&
         TRAJECTORY_PI * depthMm * cpm / 60.0 <= FLT_MAX;
}

// x = origin + D/2 * (1 - cos(2*pi*f*t)); v = D*pi*f*sin(2*pi*f*t).
// elapsedUs must be unsigned (nowUs - startedUs), not absolute micros().
// Invalid parameters and completion both return finished=true and zero velocity.
// No restart occurs after the explicit positive cycle count.
// GCC can constant-evaluate math.h sin/cos, allowing actual-core assertions;
// other C++14 compilers may use this function at runtime without doing so.
constexpr Reference reference(float originMm, float depthMm, float cpm, uint32_t elapsedUs,
                              uint32_t cycles) {
  Reference result;
  result.mm = finite(originMm) ? originMm : 0.0f;
  if (!validReference(originMm, depthMm, cpm, cycles))
    return result;
  const double elapsedCycles = static_cast<double>(elapsedUs) * cpm / 60000000.0;
  if (elapsedCycles >= cycles)
    return result;
  const double phase = elapsedCycles - static_cast<uint32_t>(elapsedCycles);
  const double angle = 2.0 * TRAJECTORY_PI * phase;
  result.mm = static_cast<float>(originMm + depthMm * 0.5 * (1.0 - ::cos(angle)));
  result.velocityMmS = static_cast<float>(TRAJECTORY_PI * depthMm * cpm / 60.0 * ::sin(angle));
  result.finished = false;
  return result;
}

struct Gains {
  float kp = 0.0f, ki = 0.0f, kd = 0.0f;
  float maxVelocityMmS = 0.0f, maxAccelerationMmS2 = 0.0f, integralLimitMmS = 0.0f;
};

constexpr bool validGains(const Gains &gains) {
  return finite(gains.kp) && finite(gains.ki) && finite(gains.kd) && finite(gains.maxVelocityMmS) &&
         finite(gains.maxAccelerationMmS2) && finite(gains.integralLimitMmS) && gains.kp > 0.0f &&
         gains.ki >= 0.0f && gains.kd >= 0.0f && gains.maxVelocityMmS > 0.0f &&
         gains.maxAccelerationMmS2 > 0.0f && gains.integralLimitMmS >= 0.0f &&
         (gains.ki == 0.0f || gains.integralLimitMmS > 0.0f);
}

// Outer position PID with velocity feed-forward and derivative on MEASUREMENT.
// u[mm/s] = v_ref + Kp[1/s]*error[mm] + I[mm/s] - Kd*d(measured)/dt.
// Ki units are 1/s^2; Kd is dimensionless. All mutable history belongs here.
// The caller must check calibrated position, sample freshness and interlocks,
// then translate the request to a verified motor direction/protocol.
class Pid {
public:
  constexpr void reset(float measuredMm) {
    previousMm_ = finite(measuredMm) ? measuredMm : 0.0f;
    initialized_ = finite(measuredMm);
    valid_ = initialized_;
    integralMmS_ = 0.0f;
    outputMmS_ = 0.0f;
  }

  constexpr float step(float targetMm, float targetVelocityMmS, float measuredMm, float dtS,
                       const Gains &gains) {
    if (!finite(targetMm) || !finite(targetVelocityMmS) || !finite(measuredMm) || !finite(dtS) ||
        dtS <= 0.0f || !validGains(gains)) {
      reset(measuredMm);
      valid_ = false;
      return 0.0f; // A fault bypasses slew limiting and requests stop immediately.
    }
    if (!initialized_)
      reset(measuredMm);
    const double error = static_cast<double>(targetMm) - measuredMm;
    const double measuredVelocity = (static_cast<double>(measuredMm) - previousMm_) / dtS;
    const double base = targetVelocityMmS + gains.kp * error - gains.kd * measuredVelocity;
    const double priorIntegral =
        gains.ki > 0.0f ? clamp(integralMmS_, -gains.integralLimitMmS, gains.integralLimitMmS)
                        : 0.0;
    const double proposedIntegral = clamp(priorIntegral + gains.ki * error * dtS,
                                          -gains.integralLimitMmS, gains.integralLimitMmS);
    const double proposed = base + proposedIntegral;
    const double limited = limit(proposed, dtS, gains);
    const double integralChange = proposedIntegral - priorIntegral;
    // Freeze only integration that pushes farther into EITHER velocity or slew
    // saturation. Integration that unwinds an existing excess is allowed.
    const bool pushingHigh = proposed > limited && integralChange > 0.0;
    const bool pushingLow = proposed < limited && integralChange < 0.0;
    integralMmS_ =
        static_cast<float>((pushingHigh || pushingLow) ? priorIntegral : proposedIntegral);
    outputMmS_ = static_cast<float>(limit(base + integralMmS_, dtS, gains));
    previousMm_ = measuredMm;
    valid_ = true;
    return outputMmS_;
  }

  constexpr float outputMmS() const { return outputMmS_; }
  constexpr float integralMmS() const { return integralMmS_; }
  constexpr bool valid() const { return valid_; }

private:
  constexpr double limit(double request, float dtS, const Gains &gains) const {
    const double delta = static_cast<double>(gains.maxAccelerationMmS2) * dtS;
    const double slewLimited = clamp(request, outputMmS_ - delta, outputMmS_ + delta);
    // Speed cap wins if limits are changed while a previous output is outside
    // the new envelope. Normal use fixes all gains/caps for an entire run.
    return clamp(slewLimited, -gains.maxVelocityMmS, gains.maxVelocityMmS);
  }

  float previousMm_ = 0.0f, integralMmS_ = 0.0f, outputMmS_ = 0.0f;
  bool initialized_ = false, valid_ = false;
};

} // namespace saver_depth
#endif
