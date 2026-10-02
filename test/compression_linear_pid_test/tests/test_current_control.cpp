#include "../current_control.h"
#include "../vesc_link.h"
using namespace saver_current;
constexpr Gains gains = {.8f, .3f, .02f, 25, .025f};
constexpr bool near(float a, float b) { return a - b < .0001f && b - a < .0001f; }
constexpr float nanValue = __builtin_nanf("");
constexpr bool calibration(int zeroRaw, int calRaw) {
  Calibration c;
  float mm = 999;
  return !c.position(zeroRaw, mm) && mm == 999 && c.zero(zeroRaw) && !c.calibrated() &&
         c.calibrate(calRaw, 20) && c.position(zeroRaw, mm) && near(mm, 0) &&
         c.position((zeroRaw + calRaw) / 2, mm) && near(mm, 10) && c.position(calRaw, mm) &&
         near(mm, 20) && c.covers(5) && !c.covers(20);
}
constexpr bool invalidCalibration(int raw, float mm) {
  Calibration c;
  return c.zero(1000) && !c.calibrate(raw, mm) && !c.calibrated();
}
constexpr bool zeroClearsScale() {
  Calibration c;
  if (!c.zero(1000) || !c.calibrate(2000, 20))
    return false;
  return c.zero(1500) && !c.calibrated() && c.zeroKnown();
}
constexpr bool invalidRecalibration() {
  Calibration c;
  if (!c.zero(1000) || !c.calibrate(2000, 20))
    return false;
  return !c.calibrate(2000, nanValue) && !c.calibrated();
}
constexpr bool invalidZero() {
  Calibration c;
  c.zero(1000);
  c.calibrate(2000, 20);
  return !c.zero(0) && !c.calibrated() && !c.zeroKnown();
}
constexpr bool noRails() {
  Calibration c;
  float mm = 99;
  return c.zero(1000) && c.calibrate(2000, 20) && !c.position(0, mm) && mm == 99 &&
         !c.position(4095, mm) && mm == 99;
}
constexpr float firstCurrent(float target) {
  Pid p;
  p.reset(0);
  return p.step(target, 0, .005f, gains, 6);
}
constexpr bool saturation(float sign) {
  Pid p;
  p.reset(0);
  for (int i = 0; i < 1000; ++i)
    if (!near(p.step(sign * 50, 0, .005f, gains, 6), sign * 6) || !p.valid() || p.i() != 0)
      return false;
  return true;
}
constexpr bool measurementDerivative() {
  Pid p;
  p.reset(0);
  p.step(10, 0, .005f, gains, 6);
  return p.d() == 0 && p.step(10, 1, .005f, gains, 6) > 0 && p.d() < 0;
}
constexpr bool invalidPid(float target, float measured, float dt, float limit) {
  Pid p;
  p.reset(0);
  return p.step(target, measured, dt, gains, limit) == 0 && !p.valid();
}
constexpr bool filter() {
  PositionFilter f;
  if (f.step(0, .005f, .01f) != 0 || !near(f.step(3, .005f, .01f), 1))
    return false;
  f.reset();
  return f.step(7, .005f, .01f) == 7;
}
constexpr bool resetPid() {
  Pid p;
  p.reset(0);
  p.step(1, 0, .005f, gains, 6);
  p.reset(4);
  return p.p() == 0 && p.i() == 0 && p.d() == 0 && p.step(4, 4, .005f, gains, 6) == 0;
}
static_assert(validGains(gains), "Working LM gains retained with CURRENT units");
static_assert(!validGains({0, .3f, .02f, 25, .025f}), "No zero Kp");
static_assert(!validGains({6, .3f, .02f, 25, .025f}), "Kp ceiling");
static_assert(!validGains({.8f, -1, .02f, 25, .025f}), "No negative Ki");
static_assert(!validGains({.8f, 4, .02f, 25, .025f}), "Ki ceiling");
static_assert(!validGains({.8f, .3f, .6f, 25, .025f}), "Kd ceiling");
static_assert(!validGains({.8f, .3f, .02f, 0, .025f}), "Integral bound required");
static_assert(!validGains({.8f, .3f, .02f, 25, 0}), "Filter time required");
static_assert(!validGains({nanValue, .3f, .02f, 25, .025f}), "Reject NaN");
static_assert(calibration(1000, 2000), "Forward sensor calibration");
static_assert(calibration(3000, 2000), "Reverse sensor calibration");
static_assert(zeroClearsScale(), "ZERO invalidates earlier scale");
static_assert(invalidRecalibration(), "Bad CAL cannot keep previous scale active");
static_assert(invalidZero(), "Invalid ZERO invalidates old origin and scale");
static_assert(noRails(), "No synthetic zero position on ADC failure");
static_assert(invalidCalibration(1001, 20), "Raw span too short");
static_assert(invalidCalibration(2000, 4.9f), "Known distance minimum");
static_assert(invalidCalibration(2000, 60.1f), "Known distance maximum");
static_assert(invalidCalibration(2000, nanValue), "CAL NaN");
static_assert(invalidCalibration(0, 20), "CAL rail low");
static_assert(invalidCalibration(4095, 20), "CAL rail high");
static_assert(invalidCalibration(1030, 60), "Implausible full-stroke ADC span");
static_assert(Calibration::usableRaw(6) && Calibration::usableRaw(4089), "LM ADC rail margin");
static_assert(!Calibration::usableRaw(5) && !Calibration::usableRaw(4090), "Both rails excluded");
static_assert(near(firstCurrent(1), .8015f), "LM first current calculation");
static_assert(near(firstCurrent(-1), -.8015f), "Signed current reverse");
static_assert(firstCurrent(0) == 0, "Zero error at rest requests zero current");
static_assert(saturation(1), "Positive current clamp and antiwindup");
static_assert(saturation(-1), "Negative current clamp and antiwindup");
static_assert(measurementDerivative(), "Derivative on measurement not target, filtered");
static_assert(invalidPid(nanValue, 0, .005f, 6), "Invalid reference stops");
static_assert(invalidPid(1, nanValue, .005f, 6), "Invalid sensor stops");
static_assert(invalidPid(1, 0, .020f, 6), "20ms deadline not replaced with5ms");
static_assert(invalidPid(1, 0, 0, 6), "No zero dt");
static_assert(invalidPid(1, 0, .005f, 0), "No missing current limit");
static_assert(filter(), "Position LPF10ms and reset");
static_assert(resetPid(), "No integral or derivative history on restart");
constexpr auto reverse = saver_pid_vesc::command(saver_pid_vesc::SET_CURRENT, -6000);
static_assert(reverse.bytes[2] == 6 && saver_pid_vesc::signed32(reverse.bytes + 3) == -6000,
              "CURRENT uses signed milliamps, not ERPM/duty");
static_assert(saver_pid_vesc::crc16(reverse.bytes + 2, 5) ==
                  static_cast<uint16_t>((reverse.bytes[7] << 8) | reverse.bytes[8]),
              "Current frame CRC");
int main() { return 0; }
