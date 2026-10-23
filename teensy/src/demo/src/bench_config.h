#ifndef SAVER_COMPRESSION_LINEAR_CONFIG_H
#define SAVER_COMPRESSION_LINEAR_CONFIG_H

#include "depth_control.h"
#include "current_control.h"

namespace saver_depth_config {
// Research, unloaded guarded test fixture ONLY. Not clinical CPR settings.
// User: YwRobot ELB030638 75 mm / 10 kohm / 0..VCC, intended 3.3 V supply.
// 2026-09-27: user reports lm/sketch_sep16a works; this MERGE has not run on hardware.
// 1.5:1 REDUCTION, 10 mm screw lead. Supply rated 10 A does not set motor limits.
constexpr float DEFAULT_DEPTH_MM = 5.0f;  // From user's working LM sketch, NOT 50 mm at boot.
constexpr float DEFAULT_RATE_CPM = 10.0f; // Full down-and-back cycles/minute, same as LM.
constexpr float REQUEST_MAX_DEPTH_MM = 50.0f;
constexpr float REQUEST_MAX_RATE_CPM = 110.0f;
constexpr float MOTOR_REVS_PER_SCREW_REV = 1.5f; // Reduction, NOT 2/3 speed-up.
constexpr float SCREW_LEAD_MM = 10.0f;
constexpr uint8_t MOTOR_POLE_PAIRS = 4; // User-provided; verify against actual motor.
constexpr uint32_t UART_BAUD = 115200;

// ===== Fill from measurements / reviewed bench setup, NOT Gemini estimates. =====
// Control calibration is now RAM-only ZERO -> CAL measured_positive_distance_mm.
// CAPTURE remains diagnostic only. Working zero must be inside physical travel.
constexpr bool BENCH_DRIVE_CONFIRMED = false; // <<< LAST: guarded unloaded fixture reviewed.
// 2026-10-15 user observed PULSE +1 A = CW; requested press direction is CCW.
// This records that user observation, NOT closed-loop tracking/installed mechanics validation.
constexpr bool MOTOR_DIRECTION_VERIFIED = true;
constexpr int8_t CURRENT_SIGN_FOR_INCREASING_MM = -1;
// Viewed toward the protruding shaft end; ZERO/CAL must define press travel as positive mm.
constexpr const char *REQUESTED_PRESS_ROTATION = "CCW";
constexpr bool VESC_SETTINGS_AND_TIMEOUT_VERIFIED = false;
constexpr bool SENSOR_WIRING_AND_FAULT_TEST_VERIFIED = false;
// 2026-10-15 user: no travel switches; independent physical power cut exists, no pin-6 auxiliary.
// Standalone unloaded bench profile ONLY. Do not copy this omission into operating firmware.
constexpr bool LIMIT_SWITCHES_REQUIRED = false;
constexpr bool LIMIT_SWITCHES_INSTALLED = false;
constexpr bool PHYSICAL_ESTOP_POWER_CUT_USER_CONFIRMED =
    true; // User report, not a bench measurement.
constexpr bool ESTOP_AUX_INSTALLED = false;
constexpr bool ESTOP_AUX_INPUT_VERIFIED = false;
constexpr int8_t TOP_ACTIVE_LEVEL = -1; // <<< 0=LOW, 1=HIGH, -1=unknown.
constexpr int8_t BOTTOM_ACTIVE_LEVEL = -1;
constexpr int8_t ESTOP_ACTIVE_LEVEL = -1;

// Relative to EACH manual ZERO, strictly inside the real mechanical travel.
// Leave room to decelerate: sensor endpoints and mechanical hard stops are NOT targets.
constexpr float SOFT_MIN_MM = 0.0f; // Still unset: actual return-side clearance is unknown.
// 2026-10-15 user: press-side travel boundary is 50 mm from the manual origin.
constexpr float SOFT_MAX_MM =
    50.0f; // Boundary, not an endpoint target or measured stopping margin.
constexpr float START_TOLERANCE_MM = 0.0f;
constexpr float MAX_START_VELOCITY_MM_S = 0.0f; // Measured rest band before ARM/RUN.
constexpr float MAX_FOLLOWING_ERROR_MM = 0.0f;
constexpr float MAX_MEASURED_VELOCITY_MM_S = 0.0f;
constexpr float SAMPLE_NOISE_ALLOWANCE_MM = 0.0f;
constexpr float MAX_PROFILE_VELOCITY_MM_S = 0.0f;
constexpr float MAX_PROFILE_ACCELERATION_MM_S2 = 0.0f;
// 2026-10-15 user-selected per-RUN ceiling, NOT a validated continuous-duty rating.
constexpr uint32_t MAX_RUN_MS = 600000; // 10 minutes; other unverified guards remain locked.

// Copied from user's LM source: POSITION -> CURRENT, not the old speed PID units.
// kp[A/mm], ki[A/(mm*s)], kd[A/(mm/s)], integral bound[mm*s], velocity LPF[s].
constexpr saver_current::Gains CURRENT_PID = {0.80f, 0.30f, 0.020f, 25.0f, 0.025f};
constexpr float POSITION_FILTER_TAU_S = 0.010f;
constexpr float DEFAULT_COMMAND_CURRENT_A = 6.0f;
constexpr float DEFAULT_INPUT_TRIP_A = 2.8f;
constexpr float MAX_ALLOWED_COMMAND_CURRENT_A =
    6.0f; // PROJECT_STATE first-run ceiling, not LM's 15 A command grammar.
constexpr float MAX_ALLOWED_INPUT_TRIP_A = 10.0f; // LM ceiling, NOT inferred from PSU rating.
constexpr bool CURRENT_CONTROL_SETTINGS_VERIFIED = false; // Source values != validated limits.
constexpr uint32_t CURRENT_TX_MS = 20;                    // LM: 200 Hz PID, 50 Hz current requests.
// LM active return is implemented but OFF until separately reviewed. Not homing/holding.
constexpr bool RETURN_TO_ZERO_ENABLED = false;
constexpr uint32_t RETURN_TIMEOUT_MS = 1500;
constexpr float RETURN_TOLERANCE_MM = 0.7f;

// Supervisory VESC-reported limits only, NOT fast current limiting or VESC config writes.
// All must be reviewed for motor, supply, regeneration and measurement semantics.
// Values below come from LM, not a newly selected safe envelope. Verification gate above remains
// false.
constexpr float MIN_INPUT_V = 18.0f;
constexpr float MAX_INPUT_V = 26.5f;
constexpr float MAX_MOTOR_CURRENT_A = 12.0f; // PROJECT_STATE normal supervisory trip.
constexpr float MAX_FET_TEMP_C = 80.0f; // VESC FET, NOT motor winding temperature.
// ============================================================================
} // namespace saver_depth_config
#endif
