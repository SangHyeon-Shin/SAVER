#include "../bench_guard.h"

using namespace saver_depth_bench;
namespace cfg = saver_depth_config;

constexpr bool same(const char *a, const char *b) {
  if (!a || !b)
    return a == b;
  while (*a && *a == *b) {
    ++a;
    ++b;
  }
  return *a == *b;
}
constexpr bool decimal(const char *text, float wanted) {
  float value = 1234;
  return parseNumber(text, value) && absValue(value - wanted) < 0.0001f;
}
constexpr bool badDecimal(const char *text) {
  float value = 0;
  return !parseNumber(text, value);
}
constexpr bool count(const char *text, uint32_t wanted) {
  uint32_t value = 0;
  return parseCycles(text, value) && value == wanted;
}
constexpr bool badCount(const char *text) {
  uint32_t value = 0;
  return !parseCycles(text, value);
}
constexpr bool cannotAutoStart() {
  RunGate gate;
  return !gate.armed() && !gate.running() && !gate.start(true, 0) && gate.fault();
}
constexpr bool cannotRunUnready() {
  RunGate gate;
  return gate.arm(true, 0) && !gate.start(false, 1) && !gate.running() && gate.fault();
}
constexpr bool runStop() {
  RunGate gate;
  if (!gate.arm(true, 0) || !gate.start(true, 1) || !gate.running())
    return false;
  gate.stop();
  return !gate.running() && !gate.armed() && !gate.start(true, 2);
}
constexpr bool duplicateRun() {
  RunGate gate;
  return gate.arm(true, 0) && gate.start(true, 1) && !gate.start(true, 2) && gate.fault() &&
         !gate.running();
}
constexpr bool clearFault() {
  RunGate gate;
  gate.trip();
  if (gate.arm(true, 0) || gate.clear(false) || !gate.fault())
    return false;
  return gate.clear(true) && !gate.fault() && !gate.armed() && gate.arm(true, 1);
}
constexpr bool clearWhileRunning() {
  RunGate gate;
  return gate.arm(true, 0) && gate.start(true, 1) && !gate.clear(true) && gate.fault() &&
         !gate.running();
}
constexpr bool armWhileRunning() {
  RunGate gate;
  return gate.arm(true, 0) && gate.start(true, 1) && !gate.arm(true, 2) && !gate.running();
}
constexpr bool expiry(uint32_t start) {
  RunGate gate;
  return gate.arm(true, start) && !gate.expire(start + ARM_MS - 1) && gate.expire(start + ARM_MS) &&
         !gate.armed() && !gate.running();
}
constexpr bool staleArm() {
  RunGate gate;
  return gate.arm(true, 0) && !gate.start(true, ARM_MS) && gate.fault();
}
constexpr bool tripDisables() {
  RunGate gate;
  if (!gate.arm(true, 0) || !gate.start(true, 1))
    return false;
  gate.trip();
  return !gate.running() && !gate.armed() && gate.fault();
}

static_assert(same(configIssue(), "ZERO_AND_CAL_REQUIRED"), "Shipped profile locked");
constexpr bool checkTableComplete() {
  for (uint8_t i = 0; i < CONFIG_CHECK_COUNT; ++i)
    if (!configCheck(i).reason || configCheck(i).passed ||
        same(configCheck(i).reason, "INVALID_CONFIG_CHECK"))
      return false;
  return true;
}
static_assert(CONFIG_CHECK_COUNT == 14 && checkTableComplete(),
              "CHECK lists all 14 unmet shipped requirements; it cannot unlock anything");
static_assert(same(configCheck(0).reason, configIssue()), "Run/report share first blocker");
static_assert(same(configCheck(1).reason, "LIMIT_SWITCHES_MISSING"), "Missing actual limits");
static_assert(same(configCheck(2).reason, "PHYSICAL_ESTOP_UNVERIFIED"), "PSU switch not certified");
static_assert(same(configCheck(9).reason, "CURRENT_PID_OR_SETTINGS_UNVERIFIED"),
              "Working LM settings are not safety certification");
static_assert(same(configCheck(12).reason, "ELECTRICAL_LIMITS_NOT_SET"),
              "All limit groups visible");
static_assert(!configCheck(14).passed && !configCheck(255).passed,
              "Out-of-range check fails closed");
static_assert(!cfg::BENCH_DRIVE_CONFIRMED && !cfg::LIMIT_SWITCHES_INSTALLED &&
                  !cfg::PHYSICAL_ESTOP_AND_INPUT_VERIFIED,
              "Missing hardware not certified");
static_assert(cfg::CURRENT_SIGN_FOR_INCREASING_MM == 0 && cfg::MAX_RUN_MS == 0,
              "Direction and duration left unconfigured");
static_assert(saver_current::validGains(cfg::CURRENT_PID) &&
                  !cfg::CURRENT_CONTROL_SETTINGS_VERIFIED,
              "LM current PID copied but review gate not bypassed");
static_assert(cfg::DEFAULT_COMMAND_CURRENT_A == 6 && cfg::DEFAULT_INPUT_TRIP_A == 2.8f,
              "User LM defaults; neither is a power supply current command");
static_assert(configCheck(0, true).passed && same(configIssue(true), "LIMIT_SWITCHES_MISSING"),
              "Runtime calibration cannot waive missing physical protection");
static_assert(cfg::DEFAULT_DEPTH_MM == 5 && cfg::DEFAULT_RATE_CPM == 10,
              "LM startup target; no default 50mm110CPM");
static_assert(!cfg::RETURN_TO_ZERO_ENABLED, "Active return tail requires separate review");
static_assert(erpmPerMmS() == 36.0f, "Reduction 1.5, lead10, user polepairs4");
static_assert(withinCalibration(10, 50, saver_depth::Calibration{0, 4095, 0, 75, true}),
              "Entire requested stroke lies inside calibrated range");
static_assert(withinCalibration(10, 50, saver_depth::Calibration{4095, 0, 0, 75, true}),
              "Reverse ADC direction preserves coordinate envelope");
static_assert(!withinCalibration(10, 50, saver_depth::Calibration{1000, 3000, 20, 55, true}),
              "Cannot start a stroke outside measured calibration coverage");
static_assert(!withinCalibration(25, 50, saver_depth::Calibration{0, 4095, 0, 75, true}),
              "Do not target calibration endpoint");
static_assert(validTarget(50, 110), "Requested target is a valid bench reference");
static_assert(validTarget(1, 1), "Smaller reference supported, not safety certification");
static_assert(!validTarget(0, 110) && !validTarget(50, 0), "Zero is not a run target");
static_assert(!validTarget(50.1f, 110) && !validTarget(50, 110.1f), "Requested envelope");
static_assert(!validTarget(-1, 110) && !validTarget(50, __builtin_nanf("")),
              "Finite positive only");
static_assert(validDuration(110, 1, 546), "One cycle fits546ms");
static_assert(!validDuration(110, 1, 545), "No rounding down requested duration");
static_assert(validDuration(110, 55, 30000), "30 seconds finite preview");
static_assert(!validDuration(110, 56, 30000), "Over30seconds rejected");
static_assert(!validDuration(110, 1, 0) && !validDuration(110, 0, 30000), "Zero locks");
static_assert(!validDuration(110, UINT32_MAX, 30000), "No integer overflow in duration");
static_assert(!validDuration(0, 1, 30000) && !validDuration(-1, 1, 30000), "Invalid rate");
static_assert(decimal("50", 50) && decimal("50.25", 50.25f), "Whole decimal tokens");
static_assert(decimal(".5", 0.5f) && decimal("0", 0), "Numeric parser separate from target range");
static_assert(badDecimal("") && badDecimal(nullptr) && badDecimal("."), "Empty token");
static_assert(badDecimal("NaN") && badDecimal("INF") && badDecimal("1e2"), "No exceptional values");
static_assert(badDecimal("-1") && badDecimal("+1") && badDecimal("1.2.3"), "Malformed numbers");
static_assert(badDecimal("12x") && badDecimal(" 12") && badDecimal("12 "), "No partial tokens");
static_assert(badDecimal("999999999999999999999999999999999999999"), "Huge numeric rejected");
static_assert(count("1", 1) && count("4294967295", UINT32_MAX), "Bounded integer parsing");
static_assert(badCount("4294967296") && badCount("99999999999999999999"), "Overflow rejected");
static_assert(badCount("0") && badCount("-1") && badCount("1.5"), "Positive integer cycles only");
static_assert(badCount(nullptr) && badCount("") && badCount("1X"), "Invalid cycle tokens");
static_assert(cannotAutoStart(), "Manual arm required");
static_assert(cannotRunUnready(), "Fresh conditions rechecked at RUN");
static_assert(runStop(), "STOP consumes ARM; RUN cannot replay");
static_assert(duplicateRun(), "Duplicate RUN stops/latches, does not extend run");
static_assert(clearFault(), "Fault clears only with healthy inputs; no auto-arm");
static_assert(clearWhileRunning(), "CLEAR during RUN stops/rejects");
static_assert(armWhileRunning(), "ARM during RUN stops/rejects");
static_assert(expiry(0), "ARM expires exactly at10seconds");
static_assert(expiry(UINT32_MAX - 100), "ARM timeout across millis wrap");
static_assert(staleArm(), "RUN cannot use expired ARM");
static_assert(tripDisables(), "Fault invalidates armed and running");

int main() { return 0; }
