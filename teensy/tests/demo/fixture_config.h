#pragma once
// SYNTHETIC TEST FIXTURE ONLY. These deliberately nonzero permissive values
// exercise guarded drive paths; they are never included by the Arduino build.
#define SAVER_COMPRESSION_LINEAR_CONFIG_H
#include "../../src/demo/src/current_control.h"
namespace saver_depth_config {
constexpr float DEFAULT_DEPTH_MM = 5, DEFAULT_RATE_CPM = 10;
constexpr float REQUEST_MAX_DEPTH_MM = 50, REQUEST_MAX_RATE_CPM = 110;
constexpr float MOTOR_REVS_PER_SCREW_REV = 1.5f, SCREW_LEAD_MM = 10;
constexpr uint8_t MOTOR_POLE_PAIRS = 4;
constexpr uint32_t UART_BAUD = 115200;
constexpr bool BENCH_DRIVE_CONFIRMED = true, MOTOR_DIRECTION_VERIFIED = true;
constexpr int8_t CURRENT_SIGN_FOR_INCREASING_MM = -1;
constexpr bool VESC_SETTINGS_AND_TIMEOUT_VERIFIED = true;
constexpr bool SENSOR_WIRING_AND_FAULT_TEST_VERIFIED = true;
constexpr bool LIMIT_SWITCHES_REQUIRED = false, LIMIT_SWITCHES_INSTALLED = false;
constexpr bool PHYSICAL_ESTOP_POWER_CUT_USER_CONFIRMED = true;
constexpr bool ESTOP_AUX_INSTALLED = false, ESTOP_AUX_INPUT_VERIFIED = false;
constexpr int8_t TOP_ACTIVE_LEVEL = -1, BOTTOM_ACTIVE_LEVEL = -1, ESTOP_ACTIVE_LEVEL = -1;
constexpr float SOFT_MIN_MM = -2, SOFT_MAX_MM = 50, START_TOLERANCE_MM = .7f;
constexpr float MAX_START_VELOCITY_MM_S = 1, MAX_FOLLOWING_ERROR_MM = 10;
constexpr float MAX_MEASURED_VELOCITY_MM_S = 200, SAMPLE_NOISE_ALLOWANCE_MM = 1;
constexpr float MAX_PROFILE_VELOCITY_MM_S = 1000, MAX_PROFILE_ACCELERATION_MM_S2 = 100000;
constexpr uint32_t MAX_RUN_MS = 600000;
constexpr saver_current::Gains CURRENT_PID = {.8f, .3f, .02f, 25, .025f};
constexpr float POSITION_FILTER_TAU_S = .01f, DEFAULT_COMMAND_CURRENT_A = 6;
constexpr float DEFAULT_INPUT_TRIP_A = 2.8f, MAX_ALLOWED_COMMAND_CURRENT_A = 15;
constexpr float MAX_ALLOWED_INPUT_TRIP_A = 10;
constexpr bool CURRENT_CONTROL_SETTINGS_VERIFIED = true, RETURN_TO_ZERO_ENABLED = false;
constexpr uint32_t CURRENT_TX_MS = 20, RETURN_TIMEOUT_MS = 1500;
constexpr float RETURN_TOLERANCE_MM = .7f, MIN_INPUT_V = 18, MAX_INPUT_V = 26.5f;
constexpr float MAX_MOTOR_CURRENT_A = 17, MAX_FET_TEMP_C = 80;
} // namespace saver_depth_config
