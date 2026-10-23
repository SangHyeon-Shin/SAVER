#ifndef SAVER_DEMO_BINDINGS_H
#define SAVER_DEMO_BINDINGS_H
#include "../../pinmap.h"
// Frozen tested driver assignments must fail to compile if pinmap.h changes.
static_assert(PIN_CP_UART_RX == 28 && PIN_CP_UART_TX == 29 && PIN_LINEAR_SENSOR == A16,
              "Update compression driver bindings with pinmap.h");
static_assert(PIN_NAU7802_SDA == 17 && PIN_NAU7802_SCL == 16 && PIN_MAX30102_SDA == 18 &&
                  PIN_MAX30102_SCL == 19,
              "Update independent I2C driver bindings with pinmap.h");
static_assert(PIN_VAC1_VSP == 24 && PIN_VAC1_EN == 26 && PIN_VAC1_ERR == 27 && PIN_VAC1_RUN == 25 &&
                  PIN_VAC2_VSP == 33 && PIN_VAC2_EN == 37 && PIN_VAC2_ERR == 36 &&
                  PIN_VAC2_RUN == 32 && PIN_PRESSURE_SENSOR == A8,
              "Update vacuum driver bindings with pinmap.h");
static_assert(PIN_LIMIT_TOP == 3 && PIN_LIMIT_BOTTOM == 4 && PIN_ESTOP == 6,
              "Update safety input bindings with pinmap.h");
#endif
