/*
 * SAVER single vacuum-motor bench test for Teensy 4.1.
 *
 * Uses vacuum motor 1 only:
 *   VSP -> Teensy pin 24 (10 kHz PWM speed command)
 *   EN  -> Teensy pin 26 (HIGH run, LOW stop/brake candidate behavior)
 *   GND -> common signal ground
 *
 * The motor is OFF after boot. It starts only after an explicit RUN command,
 * and every run stops automatically after at most 30 seconds.
 *
 * Serial Monitor: 115200 baud, Newline or Both NL & CR
 *
 * Commands:
 *   RUN [duty_pct] [duration_ms]  - start; omitted values use defaults below
 *   SET <duty_pct>                - change PWM while already running
 *   STOP                          - PWM 0, then EN LOW
 *   STATUS
 *   HELP
 *
 * Examples:
 *   RUN                -> use DEFAULT_DUTY_PERCENT for DEFAULT_RUN_MS
 *   RUN 10             -> 10% duty for 5 seconds
 *   RUN 15 10000       -> 15% duty for 10 seconds
 *   SET 20             -> change the active run to 20% duty
 *   STOP
 *
 * Important: PWM duty is a speed/voltage command, not a current command.
 * This sketch has no current sensor and cannot regulate amperes. Set the real
 * current limit on the bench power supply and measure current externally.
 */

#include <Arduino.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/* Current SAVER Teensy 4.1 outer-header pin assignment. */
constexpr uint8_t PIN_MOTOR_VSP = 24;
constexpr uint8_t PIN_MOTOR_EN = 26;

constexpr float PWM_FREQUENCY_HZ = 10000.0f;
constexpr uint8_t PWM_MAX = 255;

/* ==========================================================================
 * 사용자가 수정하는 부분
 *
 * 기본 듀티를 바꾸려면 아래 숫자 10만 1~100 사이로 수정한 뒤 다시 업로드하세요.
 * 예: 10% -> 20%로 바꾸려면 DEFAULT_DUTY_PERCENT = 20;
 * 기본 작동시간도 필요하면 DEFAULT_RUN_MS를 밀리초 단위로 수정할 수 있습니다.
 * ======================================================================== */
constexpr uint8_t DEFAULT_DUTY_PERCENT = 6;  // <<< 기본 PWM 듀티비: 여기 수정
constexpr uint32_t DEFAULT_RUN_MS = 15000;     // <<< 기본 작동시간: 5000 ms = 5초

constexpr uint32_t MIN_RUN_MS = 100;
constexpr uint32_t MAX_RUN_MS = 30000;
constexpr size_t COMMAND_BUFFER_SIZE = 64;
constexpr uint8_t MAX_SERIAL_BYTES_PER_LOOP = 32;

static_assert(DEFAULT_DUTY_PERCENT >= 1 && DEFAULT_DUTY_PERCENT <= 100,
              "DEFAULT_DUTY_PERCENT must be between 1 and 100");
static_assert(DEFAULT_RUN_MS >= MIN_RUN_MS && DEFAULT_RUN_MS <= MAX_RUN_MS,
              "DEFAULT_RUN_MS must be between MIN_RUN_MS and MAX_RUN_MS");

char g_commandBuffer[COMMAND_BUFFER_SIZE];
size_t g_commandLength = 0;
bool g_commandOverflow = false;

bool g_running = false;
uint8_t g_dutyPct = 0;
uint32_t g_runStartedMs = 0;
uint32_t g_runDurationMs = 0;

uint8_t dutyToPwm(uint8_t dutyPct) {
  return (uint8_t)(((uint16_t)dutyPct * PWM_MAX + 50U) / 100U);
}

void motorOff() {
  /* Remove the speed command first, then disable/brake the driver. */
  analogWrite(PIN_MOTOR_VSP, 0);
  digitalWrite(PIN_MOTOR_EN, LOW);
  g_running = false;
  g_dutyPct = 0;
  g_runStartedMs = 0;
  g_runDurationMs = 0;
}

void applyDuty(uint8_t dutyPct) {
  analogWrite(PIN_MOTOR_VSP, dutyToPwm(dutyPct));
  g_dutyPct = dutyPct;
}

void motorRun(uint8_t dutyPct, uint32_t durationMs) {
  motorOff();
  digitalWrite(PIN_MOTOR_EN, HIGH);
  applyDuty(dutyPct);
  g_runStartedMs = millis();
  g_runDurationMs = durationMs;
  g_running = true;
}

uint32_t remainingRunMs(uint32_t nowMs) {
  if (!g_running) return 0;
  const uint32_t elapsed = (uint32_t)(nowMs - g_runStartedMs);
  return elapsed >= g_runDurationMs ? 0 : g_runDurationMs - elapsed;
}

void printStatus() {
  Serial.print("STATUS,running=");
  Serial.print(g_running ? 1 : 0);
  Serial.print(",pwm_pct=");
  Serial.print(g_dutyPct);
  Serial.print(",pwm_raw=");
  Serial.print(dutyToPwm(g_dutyPct));
  Serial.print(",remaining_ms=");
  Serial.println(remainingRunMs(millis()));
}

void printHelp() {
  Serial.println("COMMANDS,RUN [pct] [duration_ms]|SET pct|STOP|STATUS|HELP");
  Serial.print("DEFAULTS,pwm_pct=");
  Serial.print(DEFAULT_DUTY_PERCENT);
  Serial.print(",duration_ms=");
  Serial.println(DEFAULT_RUN_MS);
  Serial.println("LIMITS,pct=1..100,duration_ms=100..30000");
  Serial.println("NOTE,pwm_pct_is_not_motor_current_A");
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
  if (g_running) {
    rejectAndStop("ALREADY_RUNNING_USE_SET");
    return;
  }

  const char* dutyText = strtok_r(nullptr, " \t", &context);
  const char* durationText = strtok_r(nullptr, " \t", &context);
  uint32_t duty = DEFAULT_DUTY_PERCENT;
  uint32_t duration = DEFAULT_RUN_MS;

  if (dutyText != nullptr && !parseUnsigned(dutyText, duty)) {
    rejectAndStop("BAD_RUN_DUTY");
    return;
  }
  if (durationText != nullptr && !parseUnsigned(durationText, duration)) {
    rejectAndStop("BAD_RUN_DURATION");
    return;
  }
  if (!noMoreTokens(context) ||
      duty < 1U || duty > 100U ||
      duration < MIN_RUN_MS || duration > MAX_RUN_MS) {
    rejectAndStop("RUN_OUT_OF_RANGE");
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

  if (!parseUnsigned(dutyText, duty) ||
      !noMoreTokens(context) ||
      duty > 100U) {
    rejectAndStop("BAD_SET_DUTY");
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

  applyDuty((uint8_t)duty);
  Serial.print("OK,DUTY_CHANGED,pwm_pct=");
  Serial.println(duty);
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
  if (!g_running) return;
  if ((uint32_t)(nowMs - g_runStartedMs) < g_runDurationMs) return;

  motorOff();
  Serial.println("EVENT,RUN_TIMEOUT_MOTOR_OFF");
}

void setup() {
  /* Set LOW latches before making either control pin an output. */
  digitalWrite(PIN_MOTOR_VSP, LOW);
  digitalWrite(PIN_MOTOR_EN, LOW);
  pinMode(PIN_MOTOR_VSP, OUTPUT);
  pinMode(PIN_MOTOR_EN, OUTPUT);

  analogWriteResolution(8);
  analogWriteFrequency(PIN_MOTOR_VSP, PWM_FREQUENCY_HZ);
  motorOff();

  Serial.begin(115200);
  const uint32_t waitStartedMs = millis();
  while (!Serial && (uint32_t)(millis() - waitStartedMs) < 2000U) {
    /* Motor remains off while waiting briefly for USB Serial. */
  }

  Serial.println("SAVER_SINGLE_VACUUM_MOTOR_TEST_READY");
  Serial.println("PINS,VSP=24,EN=26,PWM_HZ=10000");
  Serial.println("SAFETY,motor_off_at_boot=1,max_run_ms=30000");
  printHelp();
}

void loop() {
  enforceRunTimeout(millis());
  pollSerial();
  enforceRunTimeout(millis());
}
