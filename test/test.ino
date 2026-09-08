/*
 * SAVER vacuum-motor and pressure-sensor bench test for Teensy 4.1.
 *
 * This sketch is deliberately independent of the production firmware.
 * It never starts a motor at boot, and every RUN command has a time limit.
 *
 * Serial Monitor settings:
 *   - 115200 baud (the value is conventional for Teensy native USB)
 *   - Newline or Both NL & CR
 *
 * Commands:
 *   HELP
 *   ARM
 *   RUN1 <duty_pct> <duration_ms>
 *   RUN2 <duty_pct> <duration_ms>
 *   RUNBOTH <motor1_pct> <motor2_pct> <duration_ms>
 *   STOP1
 *   STOP2
 *   STOP
 *   DISARM
 *   READ
 *   STATUS
 *
 * Example: run vacuum motor 1 at 10 percent for 3000 ms
 *   ARM
 *   RUN1 10 3000
 *
 * Electrical limits:
 *   - Every Teensy GPIO and ADC input must remain between 0 V and 3.3 V.
 *   - Pressure divider wiring used by this sketch:
 *       sensor OUT -- 4.7 kOhm -- A8 -- 10 kOhm -- GND
 *     Do not connect the candidate 0.5 V to 4.5 V output directly to A8.
 *   - RUN/ERR are printed only as raw logic levels. Their voltage, output
 *     type, and active polarity have not been verified on the real driver.
 */

#include <Arduino.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/* Copied from teensy/pinmap.h at commit 5c84ce7. Do not use ESP32 pin numbers. */
constexpr uint8_t PIN_VAC1_VSP = 24;
constexpr uint8_t PIN_VAC1_RUN = 25;
constexpr uint8_t PIN_VAC1_EN  = 26;
constexpr uint8_t PIN_VAC1_ERR = 27;

constexpr uint8_t PIN_VAC2_VSP = 33;
constexpr uint8_t PIN_VAC2_RUN = 34;
constexpr uint8_t PIN_VAC2_EN  = 35;
constexpr uint8_t PIN_VAC2_ERR = 36;

constexpr uint8_t PIN_PRESSURE = A8;  // Teensy 4.1 physical pin 22.

constexpr float PWM_FREQUENCY_HZ = 10000.0f;
constexpr uint8_t PWM_MAX = 255;
constexpr uint16_t ADC_MAX = 4095;
constexpr float ADC_REFERENCE_V = 3.3f;
constexpr float PRESSURE_R_TOP_OHM = 4700.0f;     // Sensor OUT to A8.
constexpr float PRESSURE_R_BOTTOM_OHM = 10000.0f; // A8 to GND.
constexpr float PRESSURE_DIVIDER_RATIO =
    PRESSURE_R_BOTTOM_OHM / (PRESSURE_R_TOP_OHM + PRESSURE_R_BOTTOM_OHM);

/*
 * Pressure conversion is intentionally disabled until the complete XGZP6847A
 * part number is confirmed.  The suffix selects pressure direction, span, and
 * supply/output range, so a family-wide kPa equation would be unsafe.
 */
constexpr uint32_t TELEMETRY_PERIOD_MS = 100;
constexpr uint32_t MIN_RUN_MS = 100;
constexpr uint32_t MAX_RUN_MS = 30000;
constexpr size_t COMMAND_BUFFER_SIZE = 96;

struct VacuumMotor {
  uint8_t vspPin;
  uint8_t enPin;
  uint8_t dutyPct;
  uint32_t startedAtMs;
  uint32_t durationMs;
  bool running;
};

VacuumMotor g_motor1 = {PIN_VAC1_VSP, PIN_VAC1_EN, 0, 0, 0, false};
VacuumMotor g_motor2 = {PIN_VAC2_VSP, PIN_VAC2_EN, 0, 0, 0, false};

bool g_armed = false;
char g_commandBuffer[COMMAND_BUFFER_SIZE];
size_t g_commandLength = 0;
bool g_commandOverflow = false;
uint32_t g_lastTelemetryMs = 0;

uint8_t dutyToPwm(uint8_t dutyPct) {
  return (uint8_t)(((uint16_t)dutyPct * PWM_MAX + 50U) / 100U);
}

void stopMotor(VacuumMotor& motor) {
  /* VSP zero first, then disable/brake with EN LOW. */
  analogWrite(motor.vspPin, 0);
  digitalWrite(motor.enPin, LOW);
  motor.dutyPct = 0;
  motor.startedAtMs = 0;
  motor.durationMs = 0;
  motor.running = false;
}

void stopAll() {
  stopMotor(g_motor1);
  stopMotor(g_motor2);
}

void startMotor(VacuumMotor& motor, uint8_t dutyPct, uint32_t durationMs) {
  /* Keep VSP at zero while enabling, then apply the requested PWM duty. */
  analogWrite(motor.vspPin, 0);
  digitalWrite(motor.enPin, HIGH);
  analogWrite(motor.vspPin, dutyToPwm(dutyPct));
  motor.dutyPct = dutyPct;
  motor.startedAtMs = millis();
  motor.durationMs = durationMs;
  motor.running = true;
}

void printHelp() {
  Serial.println("COMMANDS,HELP|ARM|RUN1 pct ms|RUN2 pct ms|RUNBOTH pct1 pct2 ms");
  Serial.println("COMMANDS,STOP1|STOP2|STOP|DISARM|READ|STATUS");
  Serial.println("LIMITS,duty_pct=1..100,duration_ms=100..30000");
}

void printStatus() {
  Serial.print("STATUS,armed=");
  Serial.print(g_armed ? 1 : 0);
  Serial.print(",m1_running=");
  Serial.print(g_motor1.running ? 1 : 0);
  Serial.print(",m1_pct=");
  Serial.print(g_motor1.dutyPct);
  Serial.print(",m2_running=");
  Serial.print(g_motor2.running ? 1 : 0);
  Serial.print(",m2_pct=");
  Serial.println(g_motor2.dutyPct);
}

void printPressureAndStatus() {
  const uint16_t raw = (uint16_t)analogRead(PIN_PRESSURE);
  const float adcVoltage = (float)raw * ADC_REFERENCE_V / (float)ADC_MAX;
  const float sensorVoltage = adcVoltage / PRESSURE_DIVIDER_RATIO;
  const bool atAdcRail = (raw <= 1U || raw >= ADC_MAX - 1U);

  Serial.print("DATA,ms=");
  Serial.print(millis());
  Serial.print(",adc_raw=");
  Serial.print(raw);
  Serial.print(",adc_V=");
  Serial.print(adcVoltage, 4);
  Serial.print(",sensor_V=");
  Serial.print(sensorVoltage, 4);
  Serial.print(",adc_rail=");
  Serial.print(atAdcRail ? 1 : 0);
  Serial.print(",armed=");
  Serial.print(g_armed ? 1 : 0);
  Serial.print(",m1_pct=");
  Serial.print(g_motor1.dutyPct);
  Serial.print(",m2_pct=");
  Serial.print(g_motor2.dutyPct);
  Serial.print(",m1_run_raw=");
  Serial.print(digitalRead(PIN_VAC1_RUN));
  Serial.print(",m1_err_raw=");
  Serial.print(digitalRead(PIN_VAC1_ERR));
  Serial.print(",m2_run_raw=");
  Serial.print(digitalRead(PIN_VAC2_RUN));
  Serial.print(",m2_err_raw=");
  Serial.println(digitalRead(PIN_VAC2_ERR));
}

bool parseUnsigned(const char* text, uint32_t& value) {
  if (text == nullptr || *text == '\0' || *text == '-') return false;
  char* end = nullptr;
  const unsigned long parsed = strtoul(text, &end, 10);
  if (end == text || *end != '\0') return false;
  value = (uint32_t)parsed;
  return true;
}

bool noMoreTokens(char*& context) {
  return strtok_r(nullptr, " \t", &context) == nullptr;
}

void failClosed(const char* reason) {
  stopAll();
  g_armed = false;
  Serial.print("ERROR,");
  Serial.print(reason);
  Serial.println(",motors_off=1,armed=0");
}

bool parseRunArguments(char*& context, uint8_t& dutyPct, uint32_t& durationMs) {
  const char* dutyText = strtok_r(nullptr, " \t", &context);
  const char* durationText = strtok_r(nullptr, " \t", &context);
  uint32_t duty = 0;
  uint32_t duration = 0;

  if (!parseUnsigned(dutyText, duty) ||
      !parseUnsigned(durationText, duration) ||
      !noMoreTokens(context) ||
      duty < 1U || duty > 100U ||
      duration < MIN_RUN_MS || duration > MAX_RUN_MS) {
    return false;
  }

  dutyPct = (uint8_t)duty;
  durationMs = duration;
  return true;
}

void handleCommand(char* line) {
  char* context = nullptr;
  char* command = strtok_r(line, " \t", &context);
  if (command == nullptr) return;

  for (char* p = command; *p != '\0'; ++p) {
    *p = (char)toupper((unsigned char)*p);
  }

  if (strcmp(command, "HELP") == 0 && noMoreTokens(context)) {
    printHelp();
    return;
  }
  if (strcmp(command, "READ") == 0 && noMoreTokens(context)) {
    printPressureAndStatus();
    return;
  }
  if (strcmp(command, "STATUS") == 0 && noMoreTokens(context)) {
    printStatus();
    return;
  }
  if (strcmp(command, "ARM") == 0 && noMoreTokens(context)) {
    stopAll();
    g_armed = true;
    Serial.println("OK,ARMED,motors_off=1");
    return;
  }
  if (strcmp(command, "STOP") == 0 && noMoreTokens(context)) {
    stopAll();
    Serial.println("OK,ALL_MOTORS_OFF");
    return;
  }
  if (strcmp(command, "STOP1") == 0 && noMoreTokens(context)) {
    stopMotor(g_motor1);
    Serial.println("OK,MOTOR1_OFF");
    return;
  }
  if (strcmp(command, "STOP2") == 0 && noMoreTokens(context)) {
    stopMotor(g_motor2);
    Serial.println("OK,MOTOR2_OFF");
    return;
  }
  if (strcmp(command, "DISARM") == 0 && noMoreTokens(context)) {
    stopAll();
    g_armed = false;
    Serial.println("OK,DISARMED,motors_off=1");
    return;
  }

  if (!g_armed) {
    failClosed("NOT_ARMED");
    return;
  }

  if (strcmp(command, "RUN1") == 0) {
    uint8_t dutyPct = 0;
    uint32_t durationMs = 0;
    if (!parseRunArguments(context, dutyPct, durationMs)) {
      failClosed("BAD_RUN1_ARGUMENTS");
      return;
    }
    stopAll();
    startMotor(g_motor1, dutyPct, durationMs);
    Serial.println("OK,MOTOR1_RUNNING");
    return;
  }

  if (strcmp(command, "RUN2") == 0) {
    uint8_t dutyPct = 0;
    uint32_t durationMs = 0;
    if (!parseRunArguments(context, dutyPct, durationMs)) {
      failClosed("BAD_RUN2_ARGUMENTS");
      return;
    }
    stopAll();
    startMotor(g_motor2, dutyPct, durationMs);
    Serial.println("OK,MOTOR2_RUNNING");
    return;
  }

  if (strcmp(command, "RUNBOTH") == 0) {
    const char* duty1Text = strtok_r(nullptr, " \t", &context);
    const char* duty2Text = strtok_r(nullptr, " \t", &context);
    const char* durationText = strtok_r(nullptr, " \t", &context);
    uint32_t duty1 = 0;
    uint32_t duty2 = 0;
    uint32_t duration = 0;
    if (!parseUnsigned(duty1Text, duty1) ||
        !parseUnsigned(duty2Text, duty2) ||
        !parseUnsigned(durationText, duration) ||
        !noMoreTokens(context) ||
        duty1 < 1U || duty1 > 100U ||
        duty2 < 1U || duty2 > 100U ||
        duration < MIN_RUN_MS || duration > MAX_RUN_MS) {
      failClosed("BAD_RUNBOTH_ARGUMENTS");
      return;
    }
    stopAll();
    startMotor(g_motor1, (uint8_t)duty1, duration);
    startMotor(g_motor2, (uint8_t)duty2, duration);
    Serial.println("OK,BOTH_MOTORS_RUNNING");
    return;
  }

  failClosed("UNKNOWN_OR_MALFORMED_COMMAND");
}

void pollSerial() {
  while (Serial.available() > 0) {
    const char c = (char)Serial.read();
    if (c == '\r') continue;

    if (c == '\n') {
      if (g_commandOverflow) {
        failClosed("COMMAND_TOO_LONG");
      } else if (g_commandLength > 0) {
        g_commandBuffer[g_commandLength] = '\0';
        handleCommand(g_commandBuffer);
      }
      g_commandLength = 0;
      g_commandOverflow = false;
      continue;
    }

    if (g_commandLength < COMMAND_BUFFER_SIZE - 1U) {
      g_commandBuffer[g_commandLength++] = c;
    } else {
      g_commandOverflow = true;
    }
  }
}

void enforceRunTimeouts(uint32_t nowMs) {
  if (g_motor1.running &&
      (uint32_t)(nowMs - g_motor1.startedAtMs) >= g_motor1.durationMs) {
    stopMotor(g_motor1);
    Serial.println("EVENT,MOTOR1_TIMEOUT_OFF");
  }
  if (g_motor2.running &&
      (uint32_t)(nowMs - g_motor2.startedAtMs) >= g_motor2.durationMs) {
    stopMotor(g_motor2);
    Serial.println("EVENT,MOTOR2_TIMEOUT_OFF");
  }
}

void setup() {
  /* Set output latches LOW before changing the pins to output mode. */
  digitalWrite(PIN_VAC1_VSP, LOW);
  digitalWrite(PIN_VAC1_EN, LOW);
  digitalWrite(PIN_VAC2_VSP, LOW);
  digitalWrite(PIN_VAC2_EN, LOW);

  pinMode(PIN_VAC1_VSP, OUTPUT);
  pinMode(PIN_VAC1_EN, OUTPUT);
  pinMode(PIN_VAC2_VSP, OUTPUT);
  pinMode(PIN_VAC2_EN, OUTPUT);

  /* No internal pull-up: the real driver output type is not yet verified. */
  pinMode(PIN_VAC1_RUN, INPUT);
  pinMode(PIN_VAC1_ERR, INPUT);
  pinMode(PIN_VAC2_RUN, INPUT);
  pinMode(PIN_VAC2_ERR, INPUT);
  pinMode(PIN_PRESSURE, INPUT);

  analogWriteResolution(8);
  analogWriteFrequency(PIN_VAC1_VSP, PWM_FREQUENCY_HZ);
  analogWriteFrequency(PIN_VAC2_VSP, PWM_FREQUENCY_HZ);
  analogWrite(PIN_VAC1_VSP, 0);
  analogWrite(PIN_VAC2_VSP, 0);

  analogReadResolution(12);
  analogReadAveraging(16);

  Serial.begin(115200);
  const uint32_t waitStartedMs = millis();
  while (!Serial && (uint32_t)(millis() - waitStartedMs) < 2000U) {
    /* Motors remain disabled while waiting briefly for Serial Monitor. */
  }

  stopAll();
  Serial.println("SAVER_VACUUM_PRESSURE_BENCH_TEST_READY");
  Serial.println("SAFETY,motors_off=1,armed=0,no_automatic_start=1");
  Serial.print("PRESSURE_DIVIDER,sensor_out--4k7--A8--10k--GND,ratio=");
  Serial.println(PRESSURE_DIVIDER_RATIO, 6);
  Serial.println("WARNING,A8_IS_0_TO_3V3_ONLY");
  Serial.println("WARNING,pressure_kPa_disabled_full_XGZP6847A_part_number_required");
  Serial.println("WARNING,RUN_ERR_RAW_LEVELS_ARE_UNVERIFIED");
  printHelp();
  Serial.println("DATA_HEADER,ms,adc_raw,adc_V,sensor_V,adc_rail,armed,m1_pct,m2_pct,m1_run_raw,m1_err_raw,m2_run_raw,m2_err_raw");
}

void loop() {
  pollSerial();

  const uint32_t nowMs = millis();
  enforceRunTimeouts(nowMs);

  if ((uint32_t)(nowMs - g_lastTelemetryMs) >= TELEMETRY_PERIOD_MS) {
    g_lastTelemetryMs = nowMs;
    printPressureAndStatus();
  }
}
