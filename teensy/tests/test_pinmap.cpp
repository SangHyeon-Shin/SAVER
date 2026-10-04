#include "../pinmap.h"

namespace {
constexpr uint8_t activePins[] = {PIN_CP_UART_RX,       PIN_CP_UART_TX,       PIN_CP_ENCODER_A,
                                  PIN_CP_ENCODER_B,     PIN_GYRO_UART_RX,     PIN_GYRO_UART_TX,
                                  PIN_VAC1_VSP,         PIN_VAC1_RUN,         PIN_VAC1_EN,
                                  PIN_VAC1_ERR,         PIN_VAC2_VSP,         PIN_VAC2_RUN,
                                  PIN_VAC2_EN,          PIN_VAC2_ERR,         PIN_FAN1_PWM,
                                  PIN_FAN1_TACH,        PIN_FAN2_PWM,         PIN_FAN2_TACH,
                                  PIN_LIMIT_TOP,        PIN_LIMIT_BOTTOM,     PIN_ESTOP,
                                  PIN_TEMPERATURE_NTC,  PIN_PRESSURE_SENSOR,  PIN_LINEAR_SENSOR,
                                  PIN_CURRENT_SENSOR,   PIN_MAX30102_SDA,     PIN_MAX30102_SCL,
                                  PIN_NAU7802_SDA,      PIN_NAU7802_SCL,      PIN_DRIVE1_RPWM,
                                  PIN_DRIVE1_LPWM,      PIN_DRIVE1_EN,        PIN_DRIVE1_ENCODER_A,
                                  PIN_DRIVE1_ENCODER_B, PIN_DRIVE2_RPWM,      PIN_DRIVE2_LPWM,
                                  PIN_DRIVE2_EN,        PIN_DRIVE2_ENCODER_A, PIN_DRIVE2_ENCODER_B};

template <size_t N> constexpr bool uniqueOuterPins(const uint8_t (&pins)[N]) {
  for (size_t i = 0; i < N; ++i) {
    if (pins[i] > 41)
      return false;
    for (size_t j = i + 1; j < N; ++j)
      if (pins[i] == pins[j])
        return false;
  }
  return true;
}
constexpr bool used(uint8_t pin) {
  for (uint8_t assigned : activePins)
    if (assigned == pin)
      return true;
  return false;
}
constexpr uint8_t duplicate[] = {34, 34};
constexpr uint8_t underside[] = {11, 48};
constexpr uint8_t valid[] = {11, 12};
} // namespace

static_assert(uniqueOuterPins(activePins), "Active signals must have unique outer pins");
static_assert(sizeof(activePins) / sizeof(activePins[0]) == 39, "39 assigned signals expected");
static_assert(!uniqueOuterPins(duplicate), "Duplicate checker must detect a UART/GPIO collision");
static_assert(!uniqueOuterPins(underside), "Underside pads must not enter this map");
static_assert(uniqueOuterPins(valid), "Adjacent distinct inputs must pass");
static_assert(PIN_CP_UART_RX == 28 && PIN_CP_UART_TX == 29, "Serial7 remains unchanged");
static_assert(PIN_GYRO_UART_RX == 34 && PIN_GYRO_UART_TX == 35, "WT901C uses default Serial8");
static_assert(PIN_VAC1_VSP == 24 && PIN_VAC1_RUN == 25, "Do not move tested vacuum-1 wiring");
static_assert(PIN_VAC1_EN == 26 && PIN_VAC1_ERR == 27, "Keep vacuum-1 EN/ERR");
static_assert(PIN_VAC2_RUN == 32 && PIN_VAC2_EN == 37, "Vacuum-2 must release Serial8 pins");
static_assert(PIN_VAC2_VSP == 33 && PIN_VAC2_ERR == 36, "Keep vacuum-2 PWM/ERR");
static_assert(PIN_MAX30102_SDA == 18 && PIN_MAX30102_SCL == 19, "PPG stays on Wire");
static_assert(PIN_NAU7802_SDA == 17 && PIN_NAU7802_SCL == 16, "Load cell uses separate Wire1");
static_assert(PIN_DRIVE1_ENCODER_A == 11 && PIN_DRIVE1_ENCODER_B == 12,
              "Encoder must release Wire1");
static_assert(PIN_DRIVE2_ENCODER_A == 20 && PIN_DRIVE2_ENCODER_B == 21, "Keep drive-2 encoder");
static_assert(PIN_PRESSURE_SENSOR == 22 && PIN_LINEAR_SENSOR == 40, "Keep ADC wiring");
static_assert(PIN_FAN1_PWM == 5 && PIN_FAN2_PWM == 2, "Keep independent fan PWM groups");
static_assert(!used(13) && !used(23) && !used(39), "Three disconnected spares must remain");
static_assert(PIN_LOAD_CELL_ADC_RESERVED == 23 && PIN_CP_HALL_RESERVED == 39,
              "Legacy ADC/Hall reservations are disconnected, not load-cell/Hall inputs");
