#ifndef SAVER_PINMAP_H
#define SAVER_PINMAP_H

#include <Arduino.h>

/*
 * SAVER Teensy 4.1 pin assignment (2026-09-06)
 *
 * This map uses only the outer 0..41 through-hole pins.  The underside
 * 42..54 pads are intentionally unused, so no QSPI-pad lead soldering is
 * required.  Native USB Serial does not consume GPIO pins.
 *
 * Active buses:
 *   Serial7 RX/TX 28/29 : compression motor driver (baud/protocol TBD)
 *   Wire SDA/SCL 18/19  : MAX30102
 *
 * Consequence of the dense outer-pin map:
 *   Serial1..6, Serial8, Wire1, Wire2 and the canonical SPI CS pin 10 are
 *   consumed as GPIO.  Pins 11/12/13 remain physically free, but a complete
 *   default SPI allocation is not reserved.
 *
 * PWM frequency groups (Teensyduino 1.62.0 cores/teensy4/pwm.c):
 *   vacuum 1 pin 24 -> FlexPWM1_2, vacuum 2 pin 33 -> FlexPWM2_0
 *   fan 1 pin 5     -> FlexPWM2_1, fan 2 pin 2     -> FlexPWM4_2
 *   drive 1 pins 7/8 share FlexPWM1_3 intentionally
 *   drive 2 pins 0/1 use FlexPWM1_1/FlexPWM1_0; configure equal frequency
 *
 * Electrical rule: every Teensy GPIO/ADC signal is 3.3 V only and is not
 * 5 V tolerant.  Level conversion, pull-ups and analog conditioning belong
 * in the external interface circuit, not in this logical pin map.
 */

/* Compression motor UART -- driver TX crosses to RX7, driver RX to TX7. */
constexpr uint8_t PIN_CP_UART_RX = 28; // Hardware RX7; keeps real UART traffic off native USB Serial.
constexpr uint8_t PIN_CP_UART_TX = 29; // Hardware TX7 adjacent to RX7 for one compression-driver harness.

/* Compression feedback inputs. */
constexpr uint8_t PIN_CP_ENCODER_A = 30; // Interrupt-capable outer pin adjacent to encoder B.
constexpr uint8_t PIN_CP_ENCODER_B = 31; // Interrupt-capable outer pin adjacent to encoder A.
constexpr uint8_t PIN_CP_HALL      = 39; // Remaining outer interrupt input, separated from both encoder pairs.

/* Vacuum motor 1.  RUN/ERR/FG are motor-to-Teensy status signals. */
constexpr uint8_t PIN_VAC1_VSP = 24; // FlexPWM1_2_X gives vacuum 1 its own PWM-frequency group.
constexpr uint8_t PIN_VAC1_RUN = 25; // Status input beside VSP for a compact vacuum-1 harness.
constexpr uint8_t PIN_VAC1_EN  = 26; // Dedicated output lets vacuum 1 be disabled independently.
constexpr uint8_t PIN_VAC1_ERR = 27; // Error input beside EN; external circuitry must limit it to 3.3 V.
constexpr uint8_t PIN_VAC1_FG  = 32; // Interrupt-capable pulse input outside the active UART and I2C buses.

/* Vacuum motor 2. */
constexpr uint8_t PIN_VAC2_VSP = 33; // FlexPWM2_0_B is independent of vacuum 1 and both fan PWM groups.
constexpr uint8_t PIN_VAC2_RUN = 34; // Status input next to the other vacuum-2 control signals.
constexpr uint8_t PIN_VAC2_EN  = 35; // Dedicated output lets vacuum 2 be disabled independently.
constexpr uint8_t PIN_VAC2_ERR = 36; // Error input next to EN; external circuitry must limit it to 3.3 V.
constexpr uint8_t PIN_VAC2_FG  = 37; // Interrupt-capable speed-pulse input beside the vacuum-2 harness.

/* Two independent 4-wire cooling fans. */
constexpr uint8_t PIN_FAN1_PWM  = 5;  // FlexPWM2_1_A gives fan 1 an independent PWM-frequency group.
constexpr uint8_t PIN_FAN1_TACH = 38; // Interrupt-capable RPM feedback input near the high-number sensor pins.
constexpr uint8_t PIN_FAN2_PWM  = 2;  // FlexPWM4_2_A gives fan 2 an independent PWM-frequency group.
constexpr uint8_t PIN_FAN2_TACH = 9;  // Freed by replacing DS18B20 with an analog NTC; interrupt capable.

/* Safety/status inputs.  E-stop physically cuts motor power elsewhere. */
constexpr uint8_t PIN_LIMIT_TOP    = 3; // Adjacent to the lower-limit input for one switch harness.
constexpr uint8_t PIN_LIMIT_BOTTOM = 4; // Adjacent to the upper-limit input for one switch harness.
constexpr uint8_t PIN_ESTOP        = 6; // Dedicated read-only status input for the hardware E-stop circuit.

/* Analog inputs -- aliases below are Teensy pins A0..A17, all limited to 3.3 V. */
constexpr uint8_t PIN_TEMPERATURE_NTC        = A0;  // Pin 14/A0 reads the two-wire NTC through a voltage divider.
constexpr uint8_t PIN_PRESSURE_SENSOR        = A8;  // Pin 22/A8 is reserved for conditioned pressure voltage.
constexpr uint8_t PIN_LOAD_CELL_ADC_RESERVED = A9;  // Pin 23/A9 is reserved; never connect a raw bridge directly.
constexpr uint8_t PIN_LINEAR_SENSOR          = A16; // Pin 40/A16 keeps displacement feedback on an outer ADC pin.
constexpr uint8_t PIN_CURRENT_SENSOR         = A17; // Pin 41/A17 sits beside A16 for the compression sensor harness.

/* MAX30102 uses the canonical primary I2C controller; INT is intentionally unused. */
constexpr uint8_t PIN_MAX30102_SDA = 18; // Primary Wire SDA, with module pull-up voltage selected as 3.3 V.
constexpr uint8_t PIN_MAX30102_SCL = 19; // Primary Wire SCL paired with pin 18.

/* BTS7960 drive motor 1: R_EN and L_EN are tied to the single EN signal. */
constexpr uint8_t PIN_DRIVE1_RPWM      = 7;  // FlexPWM1_3_B pairs with pin 8 at one shared PWM frequency.
constexpr uint8_t PIN_DRIVE1_LPWM      = 8;  // FlexPWM1_3_A pairs with pin 7 for opposite direction.
constexpr uint8_t PIN_DRIVE1_EN        = 15; // One output disables both half bridges of module 1 together.
constexpr uint8_t PIN_DRIVE1_ENCODER_A = 16; // Interrupt-capable outer pin adjacent to drive-1 encoder B.
constexpr uint8_t PIN_DRIVE1_ENCODER_B = 17; // Interrupt-capable outer pin adjacent to drive-1 encoder A.

/* BTS7960 drive motor 2: R_EN and L_EN are tied to the single EN signal. */
constexpr uint8_t PIN_DRIVE2_RPWM      = 0;  // FlexPWM1_1_X is an available outer PWM output.
constexpr uint8_t PIN_DRIVE2_LPWM      = 1;  // FlexPWM1_0_X is adjacent; set it to the same PWM frequency as pin 0.
constexpr uint8_t PIN_DRIVE2_EN        = 10; // Separate enable permits module 2 to be shut down independently.
constexpr uint8_t PIN_DRIVE2_ENCODER_A = 20; // Interrupt-capable outer pin adjacent to drive-2 encoder B.
constexpr uint8_t PIN_DRIVE2_ENCODER_B = 21; // Interrupt-capable outer pin adjacent to drive-2 encoder A.

/* Pins 11, 12 and 13 remain unassigned.  BTS7960 R_IS/L_IS are not connected. */

static_assert(A0 == 14 && A8 == 22 && A9 == 23 && A16 == 40 && A17 == 41,
              "pinmap.h requires the Teensy 4.1 analog pin mapping");

#endif /* SAVER_PINMAP_H */
