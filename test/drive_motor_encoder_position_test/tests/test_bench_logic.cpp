#include "../bench_logic.h"

using namespace saver_drive_position_bench;

// Stationary states.
static_assert(quadratureDelta(0, 0) == 0, "00 stationary");
static_assert(quadratureDelta(1, 1) == 0, "01 stationary");
static_assert(quadratureDelta(2, 2) == 0, "10 stationary");
static_assert(quadratureDelta(3, 3) == 0, "11 stationary");

// One complete sequence in each direction.
static_assert(quadratureDelta(0, 1) == -1, "00 to 01");
static_assert(quadratureDelta(1, 3) == -1, "01 to 11");
static_assert(quadratureDelta(3, 2) == -1, "11 to 10");
static_assert(quadratureDelta(2, 0) == -1, "10 to 00");
static_assert(quadratureDelta(0, 2) == 1, "00 to 10");
static_assert(quadratureDelta(2, 3) == 1, "10 to 11");
static_assert(quadratureDelta(3, 1) == 1, "11 to 01");
static_assert(quadratureDelta(1, 0) == 1, "01 to 00");

// Both-bit jumps must not be counted as position.
static_assert(illegalQuadratureTransition(0, 3), "00 to 11 illegal");
static_assert(illegalQuadratureTransition(3, 0), "11 to 00 illegal");
static_assert(illegalQuadratureTransition(1, 2), "01 to 10 illegal");
static_assert(illegalQuadratureTransition(2, 1), "10 to 01 illegal");

static_assert(pwmRaw8(0) == 0, "zero PWM");
static_assert(pwmRaw8(10) == 26, "rounded 10 percent PWM");
static_assert(pwmRaw8(20) == 51, "20 percent PWM");
static_assert(pwmRaw8(50) == 128, "rounded 50 percent PWM");
static_assert(pwmRaw8(100) == 255, "full PWM");

static_assert(dutyForRemaining(21, 20, 15, 10) == 15, "cruise outside slow zone");
static_assert(dutyForRemaining(20, 20, 15, 10) == 10, "slow at boundary");
static_assert(dutyForRemaining(1, 20, 15, 10) == 10, "slow near target");

static_assert(!targetAllowed(1, 0), "zero maximum locks position run");
static_assert(!targetAllowed(0, 100), "zero target rejected");
static_assert(targetAllowed(100, 100), "target at limit accepted");
static_assert(!targetAllowed(101, 100), "target above limit rejected");

static_assert(!wrongOutboundDirection(-4, 0, 4), "outbound tolerance inclusive");
static_assert(wrongOutboundDirection(-5, 0, 4), "outbound wrong direction");
static_assert(!wrongReturnDirection(104, 100, 4), "return tolerance inclusive");
static_assert(wrongReturnDirection(105, 100, 4), "return wrong direction");

static_assert(withinTolerance(-4, 4), "negative final boundary");
static_assert(withinTolerance(4, 4), "positive final boundary");
static_assert(!withinTolerance(-5, 4), "negative final overshoot");
static_assert(!withinTolerance(5, 4), "positive final overshoot");

int main() { return 0; }
