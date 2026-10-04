#ifndef SAVER_MAX30102_BENCH_PINMAP_H
#define SAVER_MAX30102_BENCH_PINMAP_H

#include <Arduino.h>

#if !defined(ARDUINO_TEENSY41)
#error "Select Teensy 4.1 for this bench sketch"
#endif

// Matches teensy/pinmap.h; kept local so this Arduino sketch is self-contained.
constexpr uint8_t PIN_MAX_SDA = 18;  // Default Wire SDA; no underside solder pads.
constexpr uint8_t PIN_MAX_SCL = 19;  // Default Wire SCL; paired with pin 18.
// VIN: module's documented supply only; use 3.3 V only if its VIN supports it.
// GND: Teensy GND. SDA and SCL each need a pull-up to 3.3 V, NOT 5 V.
// INT, RD, IRD: unconnected; FIFO is polled over I2C.

// Supplemental inhibition only: physically disconnect ALL motor power first.
constexpr uint8_t PIN_VAC1_EN_GUARD = 26;   // Existing vacuum-1 EN; hold LOW.
constexpr uint8_t PIN_VAC2_EN_GUARD = 37;   // Vacuum-2 EN moved off Serial8 TX35; hold LOW.
constexpr uint8_t PIN_VAC1_VSP_GUARD = 24;  // Existing vacuum-1 VSP; static LOW.
constexpr uint8_t PIN_VAC2_VSP_GUARD = 33;  // Existing vacuum-2 VSP; static LOW.

static_assert(PIN_VAC2_EN_GUARD != 34 && PIN_VAC2_EN_GUARD != 35,
              "PPG guard must not drive the WT901C Serial8 pins");

#endif
