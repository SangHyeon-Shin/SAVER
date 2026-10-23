/*
 * SAVER BTS7960 drive-motor-only bench test for Teensy 4.1.
 *
 * ENCODER INPUTS ARE NOT USED. This sketch cannot detect motion, stall,
 * wrong direction, or travel. Secure and unload the motor before testing.
 * A physical power cut-off and PSU current limit are required. USB STOP is
 * not an E-stop. Motor current must never pass through a breadboard.
 *
 * Drive motor 1 wiring:
 *   Teensy 7  -> 74AHCT125 -> BTS7960 RPWM
 *   Teensy 8  -> 74AHCT125 -> BTS7960 LPWM
 *   Teensy 15 -> 74AHCT125 -> BTS7960 R_EN and L_EN
 *
 * Direct 3.3 V connection to a 5 V 74HC244-based IBT-2 input is not
 * guaranteed. This program cannot correct an electrical logic-level issue.
 */

#include <Arduino.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(ARDUINO_TEENSY41)
#error "Select Teensy 4.1 and USB Type Serial"
#endif

namespace {

constexpr uint8_t PIN_RPWM = 7;
constexpr uint8_t PIN_LPWM = 8;
constexpr uint8_t PIN_ENABLE = 15;

constexpr uint32_t USB_BAUD_LABEL = 115200;
constexpr uint32_t PWM_HZ = 10000;
constexpr uint8_t PWM_MIN_PERCENT = 10;
constexpr uint8_t PWM_MAX_PERCENT = 50;
constexpr uint32_t RUN_MIN_MS = 100;
constexpr uint32_t RUN_MAX_MS = 2000;
constexpr uint32_t ARM_VALID_MS = 10000;
constexpr uint32_t REVERSE_DEADTIME_MS = 750;
constexpr uint32_t STATUS_PERIOD_MS = 250;
constexpr size_t COMMAND_CAPACITY = 80;

enum class State : uint8_t { Disarmed, Armed, Forward, Reverse, Complete };

const char *stateName(State state) {
  switch (state) {
    case State::Disarmed: return "DISARMED";
    case State::Armed: return "ARMED";
    case State::Forward: return "FWD";
    case State::Reverse: return "REV";
    case State::Complete: return "COMPLETE";
  }
  return "UNKNOWN";
}

class BtsOnlyBench {
public:
  void begin() {
    pinMode(PIN_RPWM, OUTPUT);
    pinMode(PIN_LPWM, OUTPUT);
    analogWriteResolution(8);
    analogWriteFrequency(PIN_RPWM, PWM_HZ);
    analogWriteFrequency(PIN_LPWM, PWM_HZ);
    analogWrite(PIN_RPWM, 0);
    analogWrite(PIN_LPWM, 0);
    pinMode(PIN_ENABLE, OUTPUT);
    digitalWriteFast(PIN_ENABLE, LOW);
    lastMotorOffMs_ = millis();
    Serial.begin(USB_BAUD_LABEL);
  }

  void loopOnce() {
    controlTick(); // No Serial writes in the control path.
    serviceUsb();
    pollCommands();
    serviceEvent();
    serviceStatus();
  }

private:
  State state_ = State::Disarmed;
  char command_[COMMAND_CAPACITY] = {};
  size_t commandLength_ = 0;
  bool commandOverflow_ = false;
  bool usbConnected_ = false;
  bool motorEnabled_ = false;
  bool completionPending_ = false;
  int8_t direction_ = 0;
  uint8_t pwmPercent_ = 0;
  uint32_t armedAtMs_ = 0;
  uint32_t runStartedMs_ = 0;
  uint32_t runDurationMs_ = 0;
  uint32_t lastMotorOffMs_ = 0;
  uint32_t lastStatusMs_ = 0;
  uint32_t droppedLogs_ = 0;

  bool running() const { return state_ == State::Forward || state_ == State::Reverse; }

  static uint8_t pwmRaw(uint8_t percent) {
    return static_cast<uint8_t>((static_cast<uint16_t>(percent) * 255U + 50U) / 100U);
  }

  void motorOff() {
    const bool wasEnabled = motorEnabled_;
    analogWrite(PIN_RPWM, 0);
    analogWrite(PIN_LPWM, 0);
    digitalWriteFast(PIN_ENABLE, LOW);
    motorEnabled_ = false;
    direction_ = 0;
    pwmPercent_ = 0;
    if (wasEnabled)
      lastMotorOffMs_ = millis();
  }

  void motorDrive(int8_t direction, uint8_t percent) {
    const uint8_t raw = pwmRaw(percent);
    analogWrite(PIN_RPWM, 0);
    analogWrite(PIN_LPWM, 0);
    digitalWriteFast(PIN_ENABLE, HIGH);
    if (direction > 0)
      analogWrite(PIN_RPWM, raw);
    else
      analogWrite(PIN_LPWM, raw);
    motorEnabled_ = true;
    direction_ = direction;
    pwmPercent_ = percent;
  }

  void controlTick() {
    const uint32_t now = millis();
    if (state_ == State::Armed && static_cast<uint32_t>(now - armedAtMs_) > ARM_VALID_MS) {
      motorOff();
      state_ = State::Disarmed;
      return;
    }
    if (!running())
      return;
    if (!Serial || !usbConnected_) {
      motorOff();
      state_ = State::Disarmed;
      return;
    }
    if (static_cast<uint32_t>(now - runStartedMs_) >= runDurationMs_) {
      motorOff();
      state_ = State::Complete;
      completionPending_ = true;
    }
  }

  void emit(const char *format, ...) {
    char text[256];
    va_list args;
    va_start(args, format);
    const int length = vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    if (length < 0 || static_cast<size_t>(length) >= sizeof(text) || !Serial ||
        Serial.availableForWrite() < length + 1) {
      ++droppedLogs_;
      return;
    }
    Serial.write(reinterpret_cast<const uint8_t *>(text), static_cast<size_t>(length));
    Serial.write(static_cast<uint8_t>('\n'));
  }

  void serviceUsb() {
    const bool connected = static_cast<bool>(Serial);
    if (!connected) {
      motorOff();
      state_ = State::Disarmed;
      usbConnected_ = false;
      commandLength_ = 0;
      commandOverflow_ = false;
      return;
    }
    if (usbConnected_)
      return;
    motorOff();
    state_ = State::Disarmed;
    usbConnected_ = true;
    emit("SAVER_DRIVE_MOTOR_BTS_ONLY_TEST_READY,build=timed_no_encoder_v1");
    emit("PINS,RPWM=7,LPWM=8,EN=15,PWM_HZ=%lu,ENCODER=UNUSED",
         static_cast<unsigned long>(PWM_HZ));
    emit("LIMITS,pwm_pct=%u..%u,duration_ms=%lu..%lu,arm_valid_ms=%lu",
         static_cast<unsigned>(PWM_MIN_PERCENT), static_cast<unsigned>(PWM_MAX_PERCENT),
         static_cast<unsigned long>(RUN_MIN_MS), static_cast<unsigned long>(RUN_MAX_MS),
         static_cast<unsigned long>(ARM_VALID_MS));
    emit("WARNING,NO_MOTION_STALL_DIRECTION_OR_TRAVEL_FEEDBACK,AHCT_LEVEL_BUFFER_RECOMMENDED=1");
    help();
  }

  void pollCommands() {
    if (!usbConnected_)
      return;
    for (uint8_t budget = 0; budget < 32 && Serial.available(); ++budget) {
      const char c = static_cast<char>(Serial.read());
      if (c == '\r')
        continue;
      if (c == '\n') {
        if (commandOverflow_) {
          motorOff();
          state_ = State::Disarmed;
          emit("ERROR,COMMAND_TOO_LONG,motor_off=1");
        } else if (commandLength_ > 0) {
          command_[commandLength_] = '\0';
          handleCommand(command_);
        }
        commandLength_ = 0;
        commandOverflow_ = false;
      } else if (commandLength_ + 1 < sizeof(command_)) {
        command_[commandLength_++] = c;
      } else {
        commandOverflow_ = true;
      }
    }
  }

  static bool parseLong(const char *text, long minimum, long maximum, long &value) {
    if (!text || *text == '\0')
      return false;
    errno = 0;
    char *end = nullptr;
    const long parsed = strtol(text, &end, 10);
    if (errno == ERANGE || end == text || *end != '\0' || parsed < minimum || parsed > maximum)
      return false;
    value = parsed;
    return true;
  }

  static bool noExtraToken(char *&save) { return strtok_r(nullptr, " \t", &save) == nullptr; }

  void handleCommand(char *line) {
    char *save = nullptr;
    char *command = strtok_r(line, " \t", &save);
    if (!command)
      return;
    if (strcmp(command, "HELP") == 0 && noExtraToken(save)) {
      help();
    } else if (strcmp(command, "STATUS") == 0 && noExtraToken(save)) {
      status();
    } else if (strcmp(command, "STOP") == 0 && noExtraToken(save)) {
      motorOff();
      state_ = State::Disarmed;
      completionPending_ = false;
      emit("OK,STOPPED");
    } else if (strcmp(command, "DISARM") == 0 && noExtraToken(save)) {
      motorOff();
      state_ = State::Disarmed;
      completionPending_ = false;
      emit("OK,DISARMED");
    } else if (strcmp(command, "ARM") == 0 && noExtraToken(save)) {
      if (running()) {
        emit("ERROR,BUSY");
      } else {
        motorOff();
        state_ = State::Armed;
        armedAtMs_ = millis();
        emit("OK,ARMED,valid_ms=%lu", static_cast<unsigned long>(ARM_VALID_MS));
      }
    } else if (strcmp(command, "FWD") == 0 || strcmp(command, "REV") == 0) {
      const int8_t direction = strcmp(command, "FWD") == 0 ? 1 : -1;
      char *pwmText = strtok_r(nullptr, " \t", &save);
      char *durationText = strtok_r(nullptr, " \t", &save);
      long pwm = 0;
      long duration = 0;
      if (!parseLong(pwmText, PWM_MIN_PERCENT, PWM_MAX_PERCENT, pwm) ||
          !parseLong(durationText, RUN_MIN_MS, RUN_MAX_MS, duration) || !noExtraToken(save)) {
        emit("ERROR,USAGE_%s_pct_duration_ms", direction > 0 ? "FWD" : "REV");
      } else {
        startRun(direction, static_cast<uint8_t>(pwm), static_cast<uint32_t>(duration));
      }
    } else {
      emit("ERROR,UNKNOWN_OR_MALFORMED_COMMAND");
    }
  }

  void startRun(int8_t direction, uint8_t percent, uint32_t durationMs) {
    const uint32_t now = millis();
    if (state_ != State::Armed || static_cast<uint32_t>(now - armedAtMs_) > ARM_VALID_MS) {
      motorOff();
      state_ = State::Disarmed;
      emit("ERROR,FRESH_ARM_REQUIRED");
      return;
    }
    if (static_cast<uint32_t>(now - lastMotorOffMs_) < REVERSE_DEADTIME_MS) {
      emit("ERROR,DEADTIME_ACTIVE,wait_ms=%lu",
           static_cast<unsigned long>(REVERSE_DEADTIME_MS - (now - lastMotorOffMs_)));
      return;
    }
    completionPending_ = false;
    runStartedMs_ = now;
    runDurationMs_ = durationMs;
    state_ = direction > 0 ? State::Forward : State::Reverse;
    motorDrive(direction, percent);
    emit("OK,%s_STARTED,pwm_pct=%u,duration_ms=%lu",
         direction > 0 ? "FWD" : "REV", static_cast<unsigned>(percent),
         static_cast<unsigned long>(durationMs));
  }

  void serviceEvent() {
    if (!completionPending_ || !usbConnected_)
      return;
    completionPending_ = false;
    emit("EVENT,RUN_TIMEOUT_MOTOR_OFF,state=COMPLETE");
  }

  void status() {
    const uint32_t now = millis();
    const uint32_t elapsed = running() ? static_cast<uint32_t>(now - runStartedMs_) : 0;
    const uint32_t remaining = running() && elapsed < runDurationMs_ ? runDurationMs_ - elapsed : 0;
    emit("STATUS,state=%s,motor_enabled=%u,direction=%d,pwm_pct=%u,remaining_ms=%lu,dropped_logs=%lu",
         stateName(state_), motorEnabled_ ? 1U : 0U, static_cast<int>(direction_),
         static_cast<unsigned>(pwmPercent_), static_cast<unsigned long>(remaining),
         static_cast<unsigned long>(droppedLogs_));
  }

  void serviceStatus() {
    if (!usbConnected_)
      return;
    const uint32_t now = millis();
    if (static_cast<uint32_t>(now - lastStatusMs_) < STATUS_PERIOD_MS)
      return;
    lastStatusMs_ = now;
    status();
  }

  void help() {
    emit("COMMANDS,STATUS|ARM|FWD pct ms|REV pct ms|STOP|DISARM|HELP");
    emit("EXAMPLE,ARM_then_FWD_10_500_or_ARM_then_REV_10_500");
  }
};

BtsOnlyBench bench;

} // namespace

void setup() { bench.begin(); }

void loop() { bench.loopOnce(); }
