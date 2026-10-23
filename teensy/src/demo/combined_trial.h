#ifndef SAVER_DEMO_COMBINED_TRIAL_H
#define SAVER_DEMO_COMBINED_TRIAL_H
#include <math.h>
namespace saver_demo {
// Bench supply allocation reported by the user, not medical/device ratings.
// Blowers have no current feedback here: these are measured input-current
// bounds at the chosen duty, INCLUDING startup, not software current limits.
class CombinedPower {
public:
  static constexpr float supplyLimitA() { return 10.0f; }
  static constexpr float branchLimitA() { return 3.0f; }
  bool configure(float blower1, float blower2, float other) {
    if (!isfinite(blower1) || !isfinite(blower2) || !isfinite(other) || blower1 <= 0 ||
        blower2 <= 0 || other < 0 || blower1 > branchLimitA() || blower2 > branchLimitA() ||
        blower1 + blower2 + other + branchLimitA() >= supplyLimitA())
      return false;
    blower1_ = blower1;
    blower2_ = blower2;
    other_ = other;
    known_ = true;
    return true;
  }
  void invalidate() { known_ = false; }
  bool known() const { return known_; }
  float otherLoadA() const { return blower1_ + blower2_ + other_; }
  const char *issue(float inputTrip) const {
    if (!known_)
      return "BOTH_POWER_MEASUREMENT_REQUIRED";
    if (!(inputTrip > 0 && inputTrip <= branchLimitA()))
      return "BOTH_SET_CP_INLIMIT_LE_3A";
    if (otherLoadA() + inputTrip >= supplyLimitA())
      return "BOTH_SUPPLY_BUDGET_EXCEEDED";
    return nullptr;
  }

private:
  bool known_ = false;
  float blower1_ = 0, blower2_ = 0, other_ = 0;
};
} // namespace saver_demo
#endif
