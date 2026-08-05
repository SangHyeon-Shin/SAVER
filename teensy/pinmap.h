#ifndef SAVER_PINMAP_H
#define SAVER_PINMAP_H

#include <Arduino.h>

/*
 * SAVER Teensy 4.1 pin assignment
 *
 * Verified against the Teensy 4.1 pin card and Teensyduino 1.62.0
 * cores/teensy4/pwm.c.  None of the pin numbers declared by the two ESP32
 * reference sketches are reused.
 *
 * Reserved default/canonical buses:
 *   Serial1 0/1, Serial2 7/8, Serial3 15/14, Serial4 16/17,
 *   Serial5 21/20, Serial6 25/24, Serial7 28/29, Serial8 34/35
 *   Wire SDA/SCL 18/19 (MAX30102 only)
 *   SPI 10/11/12/13, SPI1 0/1/26/27, SPI2 42/43/44/45
 *
 * PWM frequency groups:
 *   pin 51 -> FlexPWM3_3, pin 54 -> FlexPWM3_0, pin 5 -> FlexPWM2_1.
 * These are different timer submodules, so analogWriteFrequency() on one output
 * does not force the other two outputs to the same frequency.
 *
 * Pins 48..54 are underside SMT pads, not the outer breadboard headers.  This
 * map assumes the optional QSPI PSRAM/flash footprints are unpopulated.  Solder
 * leads before mounting the Teensy on a breadboard; pin 52 remains spare.
 * Alternate mux routes on 48/49/50/53/54 are consumed, but every bus listed
 * above retains its default/canonical route.
 */

/* PWM outputs */
constexpr uint8_t PIN_CP_MOTOR_VSP  = 51; // FlexPWM3_3_B: underside pad beside CP EN, with an independent PWM group.
constexpr uint8_t PIN_VAC_MOTOR_VSP = 54; // FlexPWM3_0_A: underside pad beside vacuum EN, on a separate PWM group.
constexpr uint8_t PIN_COOLING_FAN   = 5;  // FlexPWM2_1_A: header pin on a third group for independent fan frequency.

/* Digital outputs */
constexpr uint8_t PIN_CP_MOTOR_EN  = 50; // Underside pad adjacent to CP VSP, keeping the motor pair in one harness.
constexpr uint8_t PIN_VAC_MOTOR_EN = 53; // Underside pad adjacent to vacuum VSP, keeping the driver pair together.

/* Digital inputs */
constexpr uint8_t PIN_VAC_MOTOR_ERR = 48; // Underside status pad, next to RUN and away from sensor header wiring.
constexpr uint8_t PIN_VAC_MOTOR_RUN = 49; // Underside status pad, paired with the driver's active-low ERR input.
constexpr uint8_t PIN_LIMIT_TOP      = 3;  // Breadboard header GPIO adjacent to the lower-limit input.
constexpr uint8_t PIN_LIMIT_BOTTOM   = 4;  // Breadboard header GPIO adjacent to the upper-limit input.
constexpr uint8_t PIN_ESTOP          = 6;  // Dedicated breadboard header pin for hardware E-stop status reporting.

/* Interrupt inputs -- all Teensy 4.1 digital pins support attachInterrupt(). */
constexpr uint8_t PIN_ENCODER_A    = 30; // Adjacent header pin to encoder B for a short, matched quadrature pair.
constexpr uint8_t PIN_ENCODER_B    = 31; // Adjacent header pin to encoder A; hardware interrupt capable.
constexpr uint8_t PIN_HALL_SENSOR  = 37; // Breadboard-accessible interrupt pin outside every canonical bus route.
constexpr uint8_t PIN_VAC_MOTOR_FG = 38; // Breadboard-accessible interrupt pin for vacuum-driver speed pulses.

/* Analog inputs -- use only Teensy 4.1 A0..A17 pins. */
constexpr uint8_t PIN_PRESSURE_SENSOR = A8;  // Pin 22/A8: ADC input that avoids every default Serial pair and Wire/SPI.
constexpr uint8_t PIN_LOAD_CELL       = A9;  // Pin 23/A9: adjacent ADC channel simplifies the analog sensor harness.
constexpr uint8_t PIN_LINEAR_SENSOR   = A16; // Pin 40/A16: breadboard-accessible ADC input with no reserved-bus conflict.
constexpr uint8_t PIN_CURRENT_SENSOR  = A17; // Pin 41/A17: adjacent to A16 for the compression feedback harness.

/* Sensor buses */
constexpr uint8_t PIN_MAX30102_SDA = 18; // Primary Wire SDA: standard MAX30102 I2C library path.
constexpr uint8_t PIN_MAX30102_SCL = 19; // Primary Wire SCL: paired with pin 18 and used by no other signal here.
constexpr uint8_t PIN_DS18B20      = 9;  // Breadboard GPIO for 1-Wire; all canonical Serial, Wire, and SPI pins stay free.

#endif /* SAVER_PINMAP_H */
