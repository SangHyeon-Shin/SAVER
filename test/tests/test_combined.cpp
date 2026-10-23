// Compile-time checks of the actual bench headers, not a rewritten mock model.
#include "../bench_logic.h"
#include "../pressure_conversion.h"
using namespace saver_vacuum_bench;
using namespace saver_pressure_bench;

constexpr bool near(float a, float b, float tolerance = 0.1f) {
  return a >= b - tolerance && a <= b + tolerance;
}
static_assert(near(DIVIDER_GAIN, 1.47f, 0.00001f), "4k7 top / 10k bottom recovery");
static_assert(near(sensorVoltageToPa(0.5f), -100000), "Vacuum lower endpoint, Pa not kPa");
static_assert(near(sensorVoltageToPa(1.5f), -75000), "Quarter scale");
static_assert(near(sensorVoltageToPa(2.5f), -50000), "Midscale");
static_assert(near(sensorVoltageToPa(3.5f), -25000), "Three-quarter scale");
static_assert(near(sensorVoltageToPa(4.5f), 0), "Atmosphere is zero gauge, not absolute pressure");
static_assert(near(sensorVoltageToPa(3.6f), -22500), "Known -22.5 kPa is -22500 Pa");
static_assert(convertPressure(3794).ok && near(convertPressure(3794).pressurePa, -139.23f, 1),
              "User raw 3794 log, recalculated before display rounding");
static_assert(convertPressure(3817).ok && !convertPressure(3817).inNominalRange &&
              convertPressure(3817).pressurePa > 0, "Small atmospheric offset is visible, not clamped");
static_assert(!convertPressure(0).ok && convertPressure(0).rail, "Ground rail invalid");
static_assert(!convertPressure(1).ok && convertPressure(1).rail, "Low rail margin");
static_assert(!convertPressure(4094).ok && convertPressure(4094).rail, "High rail margin");
static_assert(!convertPressure(4095).ok && convertPressure(4095).rail, "Supply rail invalid");
static_assert(!convertPressure(-1).adcOk && !convertPressure(4096).adcOk, "ADC read failure/outside 12 bits");
static_assert(!convertPressure(300).ok && !convertPressure(3900).ok, "Outside sensor plausibility window");
static_assert(convertPressure(0).pressurePa != convertPressure(0).pressurePa,
              "Invalid pressure is NaN internally, never a fake zero measurement");
static_assert(near(convertPressure(3794).sensorV / DIVIDER_GAIN, convertPressure(3794).adcV),
              "Sensor volts and ADC volts are distinct");

constexpr bool relativeZero(int reference, int next) {
  RelativePressureZero z;
  const PressureSample original = convertPressure(next);
  if (!z.capture(reference) || !z.active())
    return false;
  return near(z.relativePa(original),
              original.pressurePa - convertPressure(reference).pressurePa) &&
         near(convertPressure(next).pressurePa, original.pressurePa);
}
static_assert(!RelativePressureZero{}.active(), "No automatic zero at boot");
static_assert(relativeZero(3575, 3575),
              "Fresh reference displays zero without changing raw pressure");
static_assert(relativeZero(3575, 3400), "Lower signal remains negative relative to reference");
static_assert(relativeZero(3575, 3650), "Higher signal remains positive, never clamped");
constexpr bool invalidZero(int raw) {
  RelativePressureZero z;
  z.capture(3575);
  return !z.capture(raw) && !z.active() && z.referencePa() != z.referencePa();
}
static_assert(invalidZero(0) && invalidZero(1), "Low rail cannot become a reference");
static_assert(invalidZero(4094) && invalidZero(4095), "High rail cannot become a reference");
static_assert(invalidZero(-1) && invalidZero(4096), "Invalid ADC cannot become a reference");
static_assert(invalidZero(300) && invalidZero(3900), "Out-of-window signal cannot be zeroed");
constexpr bool invalidRelative(int raw) {
  RelativePressureZero z;
  z.capture(3575);
  const float result = z.relativePa(convertPressure(raw));
  return result != result;
}
static_assert(invalidRelative(0) && invalidRelative(4095), "Zero never masks ADC rail");
static_assert(invalidRelative(300) && invalidRelative(3900),
              "Zero never expands valid voltage window");
constexpr bool clearRelative() {
  RelativePressureZero z;
  z.capture(3575);
  z.clear();
  const float result = z.relativePa(convertPressure(3575));
  return !z.active() && result != result;
}
static_assert(clearRelative(), "Cleared or disconnected reference is unavailable, not fake zero");
constexpr bool replaceRelative() {
  RelativePressureZero z;
  z.capture(3575);
  z.capture(3650);
  return near(z.referencePa(), convertPressure(3650).pressurePa) &&
         near(z.relativePa(convertPressure(3650)), 0);
}
static_assert(replaceRelative(), "Each ZERO explicitly replaces the previous reference");

constexpr bool initial() {
  CombinedState s;
  return !s.armed() && !s.runningMask() && s.start(1, 10, 10, true) && !s.runningMask();
}
static_assert(initial(), "No boot start and RUN requires ARM");
constexpr bool noPressure() {
  CombinedState s;
  if (!s.arm(false) || s.armed()) return false;
  s.arm(true);
  return s.start(1, 10, 10, false) && !s.armed() && !s.runningMask();
}
static_assert(noPressure(), "Bad pressure blocks ARM and RUN");
constexpr bool startSelected(uint8_t mask) {
  CombinedState s; s.observe(false, false, 0); s.arm(true);
  return !s.start(mask, 10, 12, true) && s.runningMask() == mask;
}
static_assert(startSelected(1), "Motor 1 alone");
static_assert(startSelected(2), "Motor 2 alone");
static_assert(startSelected(3), "Both selected atomically");
constexpr bool invalidBoth(uint32_t duty1, uint32_t duty2) {
  CombinedState s; s.arm(true);
  return s.start(3, duty1, duty2, true) && !s.armed() && !s.runningMask();
}
static_assert(invalidBoth(10, 101), "Invalid second duty cannot partially start first motor");
static_assert(invalidBoth(0, 10), "Invalid first duty");
static_assert(invalidBoth(101, 10), "Overrange first duty");
static_assert(invalidBoth(10, 0), "Zero second duty cannot partially start first motor");
constexpr bool idleFault() {
  CombinedState s; s.observe(false, false, 0); s.arm(true);
  s.start(1, 10, 10, true);
  const SafetyEvent e = s.observe(false, true, 400);
  return e.faultMask == 2 && e.stopMask == 2 && s.runningMask() == 1 && s.armed();
}
static_assert(idleFault(), "Idle disconnected motor 2 does not stop motor 1");
constexpr bool activeFault() {
  CombinedState s; s.observe(false, false, 0); s.arm(true);
  s.start(3, 10, 10, true);
  const SafetyEvent e = s.observe(true, false, 400);
  return e.stopMask == 3 && s.motor(0).fault() && !s.runningMask() && !s.armed();
}
static_assert(activeFault(), "Running channel ERR stops both and disarms");
constexpr bool blockedPair() {
  CombinedState s; s.observe(false, true, 0); s.arm(true);
  return s.start(3, 10, 10, true) && !s.runningMask();
}
static_assert(blockedPair(), "Second motor fault cannot partially start first motor");
constexpr bool manualStop(uint32_t now) {
  CombinedState s; s.observe(false, false, now); s.arm(true);
  if (s.start(3, 10, 12, true)) return false;
  const uint32_t intervals[] = {15000U, 30000U, 3600000U};
  for (uint32_t elapsed : intervals) {
    if (s.observe(false, false, now + elapsed).stopMask || s.runningMask() != 3 || !s.armed())
      return false;
  }
  s.stop(3, true);
  return !s.runningMask() && !s.armed();
}
static_assert(manualStop(0), "No automatic run deadline; explicit STOP still stops both");
static_assert(manualStop(0xFFFFFFF0U), "Manual-run behavior survives millis rollover");
constexpr bool usbOrMalformedStop() {
  CombinedState s; s.observe(false, true, 0); s.arm(true);
  s.start(1, 10, 10, true); s.stop(3, true);
  return !s.runningMask() && !s.armed() && s.motor(1).fault();
}
static_assert(usbOrMalformedStop(), "Disconnect/reject stop preserves faults, no auto restart");
constexpr bool setManualDuty() {
  CombinedState s; s.arm(true); s.start(1, 10, 10, true);
  return !s.set(0, 12) && !s.observe(false, false, 3600000).stopMask &&
         s.motor(0).duty() == 12 && s.runningMask() == 1;
}
static_assert(setManualDuty(), "SET changes duty without adding a hidden run deadline");
constexpr bool repeatedRun() {
  CombinedState s; s.arm(true); s.start(1, 10, 10, true);
  return s.start(1, 10, 10, true) && !s.runningMask() && !s.armed();
}
static_assert(repeatedRun(), "Repeated RUN stops/disarms instead of restarting a manual run");
constexpr bool clearChannel() {
  CombinedState s; s.observe(true, true, 0); s.observe(false, true, 100);
  if (!s.clear(1, 349) || !s.motor(0).fault()) return false;
  return !s.clear(1, 350) && !s.motor(0).fault() && s.motor(1).fault() && !s.runningMask();
}
static_assert(clearChannel(), "Independent ERR clear after HIGH 250 ms, no restart");
constexpr bool stopOne() {
  CombinedState s; s.arm(true); s.start(3, 10, 10, true); s.stop(1);
  return s.runningMask() == 2 && s.motor(0).duty() == 0;
}
static_assert(stopOne(), "STOP1 leaves only motor 2 commanded");

constexpr bool runArguments(uint8_t mask, const char* first, const char* second,
                            const char* extra, bool accepted, uint32_t expected1, uint32_t expected2) {
  uint32_t a = 7, b = 8;
  const char* error = parseRunDuties(mask, first, second, extra, 6, a, b);
  return (error == nullptr) == accepted && a == expected1 && b == expected2;
}
static_assert(runArguments(1, nullptr, nullptr, nullptr, true, 6, 6), "Default single RUN duty");
static_assert(runArguments(1, "10", nullptr, nullptr, true, 10, 10), "Single RUN with explicit duty");
static_assert(runArguments(3, "10", "12", nullptr, true, 10, 12), "Both RUN duties parsed atomically");
static_assert(runArguments(1, "10", "5000", nullptr, false, 7, 8), "Reject old timed RUN without partial commit");
static_assert(runArguments(3, "10", "12", "5000", false, 7, 8), "Reject old timed RUNBOTH");
static_assert(runArguments(3, "10", nullptr, nullptr, false, 7, 8), "Reject missing second duty");
static_assert(runArguments(1, "bad", nullptr, nullptr, false, 7, 8), "Reject malformed duty atomically");
