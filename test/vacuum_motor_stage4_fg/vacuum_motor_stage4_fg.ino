/*
 * SAVER vacuum motor staged test - Stage 4: VSP + EN + ERR + RUN + FG
 * Target: Teensy 4.1, vacuum motor 1 only
 *
 * Added since Stage 3:
 *   FG -> pin 32 interrupt input
 *   One FALLING edge per pulse is counted. The product-label candidate
 *   conversion is 1 Hz = 120 rpm. Verify both with an oscilloscope.
 *
 * This final stage uses every signal pin of vacuum motor 1. ERR/RUN/FG must
 * be proven 0..3.3 V before connection. Open-collector outputs need external
 * pull-ups to 3.3 V; 5 V push-pull outputs need level conversion.
 *
 * Serial Monitor: 115200 baud, Newline or Both NL & CR
 * Commands: RUN [pct] [ms], SET pct, STOP, CLEAR_ERR, STATUS, HELP
 */

#include <Arduino.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

constexpr uint8_t PIN_MOTOR_VSP = 24;
constexpr uint8_t PIN_MOTOR_RUN = 25;
constexpr uint8_t PIN_MOTOR_EN = 26;
constexpr uint8_t PIN_MOTOR_ERR = 27;
constexpr uint8_t PIN_MOTOR_FG = 32;
constexpr uint8_t ERR_ACTIVE_LEVEL = LOW;
constexpr uint8_t RUN_ACTIVE_LEVEL = LOW;

constexpr float PWM_FREQUENCY_HZ = 10000.0f;
constexpr uint8_t PWM_MAX = 255;
constexpr uint8_t DEFAULT_DUTY_PERCENT = 10;
constexpr uint32_t DEFAULT_RUN_MS = 5000;
constexpr uint32_t MIN_RUN_MS = 100;
constexpr uint32_t MAX_RUN_MS = 30000;
constexpr uint32_t ERR_CLEAR_STABLE_MS = 250;
constexpr uint32_t STATUS_PERIOD_MS = 500;
constexpr uint32_t FG_WINDOW_MS = 1000;
constexpr float FG_RPM_PER_HZ_CANDIDATE = 120.0f;
constexpr size_t COMMAND_BUFFER_SIZE = 64;
constexpr uint8_t MAX_SERIAL_BYTES_PER_LOOP = 32;

static_assert(DEFAULT_DUTY_PERCENT >= 1 && DEFAULT_DUTY_PERCENT <= 100,
              "DEFAULT_DUTY_PERCENT must be between 1 and 100");
static_assert(DEFAULT_RUN_MS >= MIN_RUN_MS && DEFAULT_RUN_MS <= MAX_RUN_MS,
              "DEFAULT_RUN_MS is outside the allowed bench-test range");

char g_commandBuffer[COMMAND_BUFFER_SIZE];
size_t g_commandLength = 0;
bool g_commandOverflow = false;
bool g_running = false;
uint8_t g_dutyPct = 0;
uint32_t g_runStartedMs = 0;
uint32_t g_runDurationMs = 0;
uint32_t g_lastStatusMs = 0;
bool g_errFaultLatched = false;
bool g_errInactiveTracking = false;
uint32_t g_errInactiveSinceMs = 0;

volatile uint32_t g_fgPulseCount = 0;
volatile uint32_t g_fgLastPulseUs = 0;
volatile bool g_fgEverSeen = false;
uint32_t g_fgWindowStartedMs = 0;
uint32_t g_fgLastWindowPulses = 0;
float g_fgHz = 0.0f;
float g_rpmCandidate = 0.0f;
bool g_fgMeasurementOk = false;

void onFgPulse() {
  ++g_fgPulseCount;
  g_fgLastPulseUs = micros();
  g_fgEverSeen = true;
}

uint8_t dutyToPwm(uint8_t dutyPct) {
  return (uint8_t)(((uint16_t)dutyPct * PWM_MAX + 50U) / 100U);
}

bool errActive() {
  return digitalRead(PIN_MOTOR_ERR) == ERR_ACTIVE_LEVEL;
}

void resetFgMeasurement() {
  noInterrupts();
  g_fgPulseCount = 0;
  g_fgLastPulseUs = 0;
  g_fgEverSeen = false;
  interrupts();
  g_fgWindowStartedMs = millis();
  g_fgLastWindowPulses = 0;
  g_fgHz = 0.0f;
  g_rpmCandidate = 0.0f;
  g_fgMeasurementOk = false;
}

void motorOff() {
  analogWrite(PIN_MOTOR_VSP, 0);
  digitalWrite(PIN_MOTOR_EN, LOW);
  g_running = false;
  g_dutyPct = 0;
  g_runStartedMs = 0;
  g_runDurationMs = 0;
  g_fgMeasurementOk = false;
}

void updateErrSafety(uint32_t nowMs) {
  if (errActive()) {
    g_errInactiveTracking = false;
    if (!g_errFaultLatched) {
      motorOff();
      g_errFaultLatched = true;
      Serial.println("FAULT,ERR_ACTIVE,motor_off=1,latched=1");
    }
    return;
  }
  if (!g_errInactiveTracking) {
    g_errInactiveTracking = true;
    g_errInactiveSinceMs = nowMs;
  }
}

void motorRun(uint8_t dutyPct, uint32_t durationMs) {
  motorOff();
  resetFgMeasurement();
  digitalWrite(PIN_MOTOR_EN, HIGH);
  analogWrite(PIN_MOTOR_VSP, dutyToPwm(dutyPct));
  g_dutyPct = dutyPct;
  g_runStartedMs = millis();
  g_runDurationMs = durationMs;
  g_running = true;
}

uint32_t remainingRunMs(uint32_t nowMs) {
  if (!g_running) return 0;
  const uint32_t elapsed = (uint32_t)(nowMs - g_runStartedMs);
  return elapsed >= g_runDurationMs ? 0 : g_runDurationMs - elapsed;
}

void updateFgMeasurement(uint32_t nowMs) {
  if (!g_running) return;
  const uint32_t elapsedMs = (uint32_t)(nowMs - g_fgWindowStartedMs);
  if (elapsedMs < FG_WINDOW_MS) return;

  noInterrupts();
  const uint32_t pulses = g_fgPulseCount;
  const bool everSeen = g_fgEverSeen;
  g_fgPulseCount = 0;
  interrupts();

  g_fgWindowStartedMs = nowMs;
  g_fgLastWindowPulses = pulses;
  if (pulses == 0U) {
    g_fgHz = 0.0f;
    g_rpmCandidate = 0.0f;
    g_fgMeasurementOk = false;
    Serial.print("FG,pulses=0,elapsed_ms=");
    Serial.print(elapsedMs);
    Serial.print(",seen_since_run=");
    Serial.print(everSeen ? 1 : 0);
    Serial.println(",fg_ok=0,rpm_candidate=NA");
    return;
  }

  g_fgHz = (float)pulses * 1000.0f / (float)elapsedMs;
  g_rpmCandidate = g_fgHz * FG_RPM_PER_HZ_CANDIDATE;
  g_fgMeasurementOk = true;
  Serial.print("FG,pulses=");
  Serial.print(pulses);
  Serial.print(",elapsed_ms=");
  Serial.print(elapsedMs);
  Serial.print(",frequency_Hz=");
  Serial.print(g_fgHz, 2);
  Serial.print(",fg_ok=1,rpm_candidate=");
  Serial.println(g_rpmCandidate, 1);
}

void printStatus() {
  const int errRaw = digitalRead(PIN_MOTOR_ERR);
  const int runRaw = digitalRead(PIN_MOTOR_RUN);
  Serial.print("STATUS,commanded_running=");
  Serial.print(g_running ? 1 : 0);
  Serial.print(",pwm_pct=");
  Serial.print(g_dutyPct);
  Serial.print(",remaining_ms=");
  Serial.print(remainingRunMs(millis()));
  Serial.print(",err_raw=");
  Serial.print(errRaw);
  Serial.print(",err_active_candidate=");
  Serial.print(errRaw == ERR_ACTIVE_LEVEL ? 1 : 0);
  Serial.print(",err_fault_latched=");
  Serial.print(g_errFaultLatched ? 1 : 0);
  Serial.print(",run_raw=");
  Serial.print(runRaw);
  Serial.print(",run_active_candidate=");
  Serial.print(runRaw == RUN_ACTIVE_LEVEL ? 1 : 0);
  Serial.print(",fg_ok=");
  Serial.print(g_fgMeasurementOk ? 1 : 0);
  Serial.print(",fg_window_pulses=");
  Serial.print(g_fgLastWindowPulses);
  Serial.print(",rpm_candidate=");
  if (g_fgMeasurementOk) Serial.println(g_rpmCandidate, 1);
  else Serial.println("NA");
}

void printHelp() {
  Serial.println("COMMANDS,RUN [pct] [duration_ms]|SET pct|STOP|CLEAR_ERR|STATUS|HELP");
  Serial.println("NOTE,ERR_RUN_LOW_and_FG_FALLING_are_unverified_candidates");
  Serial.println("NOTE,FG_conversion_candidate_is_1Hz_equals_120rpm");
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

void rejectAndStop(const char* reason) {
  motorOff();
  Serial.print("ERROR,");
  Serial.print(reason);
  Serial.println(",motor_off=1");
}

void handleRun(char*& context) {
  if (g_errFaultLatched || errActive()) {
    rejectAndStop("ERR_FAULT_USE_CLEAR_ERR");
    return;
  }
  if (g_running) {
    rejectAndStop("ALREADY_RUNNING_USE_SET");
    return;
  }
  const char* dutyText = strtok_r(nullptr, " \t", &context);
  const char* durationText = strtok_r(nullptr, " \t", &context);
  uint32_t duty = DEFAULT_DUTY_PERCENT;
  uint32_t duration = DEFAULT_RUN_MS;
  if ((dutyText != nullptr && !parseUnsigned(dutyText, duty)) ||
      (durationText != nullptr && !parseUnsigned(durationText, duration)) ||
      !noMoreTokens(context) || duty < 1U || duty > 100U ||
      duration < MIN_RUN_MS || duration > MAX_RUN_MS) {
    rejectAndStop("BAD_RUN_ARGUMENTS");
    return;
  }
  motorRun((uint8_t)duty, duration);
  Serial.print("OK,RUNNING,pwm_pct=");
  Serial.print(duty);
  Serial.print(",duration_ms=");
  Serial.println(duration);
}

void handleSet(char*& context) {
  const char* dutyText = strtok_r(nullptr, " \t", &context);
  uint32_t duty = 0;
  if (!parseUnsigned(dutyText, duty) || !noMoreTokens(context) || duty > 100U) {
    rejectAndStop("BAD_SET_DUTY");
    return;
  }
  if (g_errFaultLatched || errActive()) {
    rejectAndStop("ERR_FAULT_USE_CLEAR_ERR");
    return;
  }
  if (duty == 0U) {
    motorOff();
    Serial.println("OK,MOTOR_OFF");
    return;
  }
  if (!g_running) {
    rejectAndStop("NOT_RUNNING_USE_RUN");
    return;
  }
  analogWrite(PIN_MOTOR_VSP, dutyToPwm((uint8_t)duty));
  g_dutyPct = (uint8_t)duty;
  Serial.print("OK,DUTY_CHANGED,pwm_pct=");
  Serial.println(duty);
}

void handleClearErr(char*& context) {
  if (!noMoreTokens(context)) {
    rejectAndStop("BAD_CLEAR_ERR_COMMAND");
    return;
  }
  const uint32_t nowMs = millis();
  updateErrSafety(nowMs);
  const bool stableInactive = g_errInactiveTracking &&
      (uint32_t)(nowMs - g_errInactiveSinceMs) >= ERR_CLEAR_STABLE_MS;
  if (errActive() || !stableInactive) {
    motorOff();
    Serial.println("ERROR,ERR_NOT_INACTIVE_FOR_250MS,motor_off=1");
    return;
  }
  g_errFaultLatched = false;
  Serial.println("OK,ERR_FAULT_CLEARED");
}

void handleCommand(char* line) {
  char* context = nullptr;
  char* command = strtok_r(line, " \t", &context);
  if (command == nullptr) return;
  for (char* p = command; *p != '\0'; ++p) {
    *p = (char)toupper((unsigned char)*p);
  }
  if (strcmp(command, "RUN") == 0) {
    handleRun(context);
  } else if (strcmp(command, "SET") == 0) {
    handleSet(context);
  } else if (strcmp(command, "STOP") == 0 && noMoreTokens(context)) {
    motorOff();
    Serial.println("OK,MOTOR_OFF");
  } else if (strcmp(command, "CLEAR_ERR") == 0) {
    handleClearErr(context);
  } else if (strcmp(command, "STATUS") == 0 && noMoreTokens(context)) {
    printStatus();
  } else if (strcmp(command, "HELP") == 0 && noMoreTokens(context)) {
    printHelp();
  } else {
    rejectAndStop("UNKNOWN_OR_MALFORMED_COMMAND");
  }
}

void pollSerial() {
  uint8_t processed = 0;
  while (Serial.available() > 0 && processed < MAX_SERIAL_BYTES_PER_LOOP) {
    ++processed;
    const char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      if (g_commandOverflow) {
        rejectAndStop("COMMAND_TOO_LONG");
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

void enforceRunTimeout(uint32_t nowMs) {
  if (!g_running || (uint32_t)(nowMs - g_runStartedMs) < g_runDurationMs) return;
  motorOff();
  Serial.println("EVENT,RUN_TIMEOUT_MOTOR_OFF");
}

void setup() {
  digitalWrite(PIN_MOTOR_VSP, LOW);
  digitalWrite(PIN_MOTOR_EN, LOW);
  pinMode(PIN_MOTOR_VSP, OUTPUT);
  pinMode(PIN_MOTOR_EN, OUTPUT);
  pinMode(PIN_MOTOR_ERR, INPUT);
  pinMode(PIN_MOTOR_RUN, INPUT);
  pinMode(PIN_MOTOR_FG, INPUT);

  analogWriteResolution(8);
  analogWriteFrequency(PIN_MOTOR_VSP, PWM_FREQUENCY_HZ);
  motorOff();
  resetFgMeasurement();
  attachInterrupt(digitalPinToInterrupt(PIN_MOTOR_FG), onFgPulse, FALLING);

  Serial.begin(115200);
  const uint32_t waitStartedMs = millis();
  while (!Serial && (uint32_t)(millis() - waitStartedMs) < 2000U) {}

  Serial.println("SAVER_VACUUM_MOTOR_STAGE4_FG_READY");
  Serial.println("PINS,VSP=24,EN=26,ERR=27,RUN=25,FG=32,PWM_HZ=10000");
  Serial.println("WARNING,ERR_RUN_FG_voltage_output_type_and_polarity_require_real_test");
  printHelp();
}

void loop() {
  uint32_t nowMs = millis();
  updateErrSafety(nowMs);
  enforceRunTimeout(nowMs);
  updateFgMeasurement(nowMs);
  pollSerial();
  nowMs = millis();
  updateErrSafety(nowMs);
  enforceRunTimeout(nowMs);
  updateFgMeasurement(nowMs);

  if (g_running && (uint32_t)(nowMs - g_lastStatusMs) >= STATUS_PERIOD_MS) {
    g_lastStatusMs = nowMs;
    printStatus();
  }
}
