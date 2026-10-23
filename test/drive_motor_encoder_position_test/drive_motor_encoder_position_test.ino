/*
 * SAVER drive-motor position bench test
 * Teensy 4.1 + IBT-2/BTS7960 module + 24 V worm-geared DC motor
 *
 * BENCH ONLY: secured, unloaded motor/gearbox.  This is not a CPR controller.
 * Motor power never passes through a breadboard or this Teensy board.
 * A physical power cut-off is required; USB STOP is not an E-stop.
 *
 * Wiring follows teensy/pinmap.h drive motor 1:
 *   pin 7  -> level buffer -> RPWM
 *   pin 8  -> level buffer -> LPWM
 *   pin 15 -> level buffer -> R_EN and L_EN
 *   encoder A -> verified 0..3.3 V interface -> pin 11
 *   encoder B -> verified 0..3.3 V interface -> pin 12
 *
 * This sketch does not auto-start.  Electrical verification, ARM, and RUN are
 * separate operations.  Normal RUN motion is encoder-count controlled; the
 * CAL_* commands are time-limited only for initial count/direction calibration.
 */

#include <Arduino.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bench_logic.h"

#if !defined(ARDUINO_TEENSY41)
#error "Select Teensy 4.1 and USB Type Serial"
#endif

namespace {

using namespace saver_drive_position_bench;

// ===== 반드시 실측 뒤 사용자가 확인/수정할 벤치 설정 =====
constexpr bool ENCODER_ELECTRICAL_INTERFACE_VERIFIED = true;
// true 조건: encoder supply/output topology를 확인했고 A/B가 언제나 0..3.3 V임을 실측.

constexpr bool BTS_CONTROL_INTERFACE_VERIFIED = true;
// true 조건: 3.3 V GPIO를 바로 쓰거나 74(A)HCT 버퍼를 거친 실제 모듈 입력을 검증.

constexpr int8_t ENCODER_SIGN = 1;
// CAL_FWD 때 signed_delta가 음수이면 -1로 바꾸거나 A/B 두 신호를 서로 바꿉니다.

constexpr int32_t MAX_TARGET_COUNTS = 0;
// 출력축/기구 보정 뒤 허용할 최대 편도 카운트. 0이면 RUN은 의도적으로 잠깁니다.

constexpr uint8_t CRUISE_PWM_PERCENT = 15;
constexpr uint8_t SLOW_PWM_PERCENT = 10;
constexpr int32_t SLOW_ZONE_COUNTS = 20;
// 위 3개는 무부하 초기값일 뿐입니다. 보정 결과와 실제 기구로 다시 정해야 합니다.
// ========================================================

constexpr uint8_t PIN_RPWM = 7;
constexpr uint8_t PIN_LPWM = 8;
constexpr uint8_t PIN_ENABLE = 15; // One signal fans out to R_EN and L_EN.
constexpr uint8_t PIN_ENCODER_A = 11;
constexpr uint8_t PIN_ENCODER_B = 12;

constexpr uint32_t USB_BAUD_LABEL = 115200; // Native USB ignores UART baud electrically.
constexpr uint32_t PWM_HZ = 10000;
constexpr uint8_t PWM_MAX_PERCENT = 50;
constexpr uint8_t PWM_MIN_PERCENT = 10;
constexpr uint32_t CONTROL_PERIOD_US = 1000;
constexpr uint32_t STATUS_PERIOD_MS = 250;
constexpr uint32_t ARM_VALID_MS = 10000;
constexpr uint32_t MOVE_TIMEOUT_MS = 10000;
constexpr uint32_t CAL_MAX_RUN_MS = 2000;
constexpr uint32_t REVERSE_DEADTIME_MS = 750;
constexpr uint32_t SETTLE_QUIET_MS = 250;
constexpr uint32_t SETTLE_TIMEOUT_MS = 2000;
constexpr uint32_t ENCODER_STARTUP_GRACE_MS = 500;
constexpr uint32_t ENCODER_STALL_MS = 400;
constexpr int32_t WRONG_DIRECTION_TOLERANCE_COUNTS = 4;
constexpr int32_t FINAL_TOLERANCE_COUNTS = 4;
constexpr uint32_t MAX_INVALID_TRANSITIONS_PER_RUN = 8;
constexpr size_t COMMAND_CAPACITY = 96;

static_assert(PIN_RPWM == 7 && PIN_LPWM == 8 && PIN_ENABLE == 15,
              "Keep this test synchronized with teensy/pinmap.h drive motor 1");
static_assert(PIN_ENCODER_A == 11 && PIN_ENCODER_B == 12,
              "Keep this test synchronized with teensy/pinmap.h drive motor 1");
static_assert(CRUISE_PWM_PERCENT >= PWM_MIN_PERCENT &&
                  CRUISE_PWM_PERCENT <= PWM_MAX_PERCENT,
              "Cruise PWM is outside the initial bench envelope");
static_assert(SLOW_PWM_PERCENT >= PWM_MIN_PERCENT &&
                  SLOW_PWM_PERCENT <= CRUISE_PWM_PERCENT,
              "Slow PWM must be within the initial bench envelope");
static_assert(SLOW_ZONE_COUNTS > 0, "Slow zone must be positive");
static_assert(MAX_TARGET_COUNTS >= 0, "Maximum target cannot be negative");
static_assert(ENCODER_SIGN == 1 || ENCODER_SIGN == -1, "Encoder sign must be +1 or -1");

enum class MotionState : uint8_t {
  Disarmed,
  Armed,
  CalForward,
  CalReverse,
  Outbound,
  ReverseDwell,
  Returning,
  Complete,
  Fault
};

enum class Event : uint8_t { None, Fault, CalibrationDone, MoveDone, Stopped };

const char *stateName(MotionState state) {
  switch (state) {
    case MotionState::Disarmed: return "DISARMED";
    case MotionState::Armed: return "ARMED";
    case MotionState::CalForward: return "CAL_FWD";
    case MotionState::CalReverse: return "CAL_REV";
    case MotionState::Outbound: return "OUTBOUND";
    case MotionState::ReverseDwell: return "REVERSE_DWELL";
    case MotionState::Returning: return "RETURNING";
    case MotionState::Complete: return "COMPLETE";
    case MotionState::Fault: return "FAULT";
  }
  return "UNKNOWN";
}

class QuadratureCounter {
public:
  static QuadratureCounter &instance() {
    static QuadratureCounter counter;
    return counter;
  }

  void begin() {
    pinMode(PIN_ENCODER_A, INPUT);
    pinMode(PIN_ENCODER_B, INPUT);
    noInterrupts();
    previousState_ = readPins();
    count_ = 0;
    invalidTransitions_ = 0;
    interrupts();
    attachInterrupt(digitalPinToInterrupt(PIN_ENCODER_A), onEdge, CHANGE);
    attachInterrupt(digitalPinToInterrupt(PIN_ENCODER_B), onEdge, CHANGE);
  }

  int32_t count() const {
    noInterrupts();
    const int32_t value = count_;
    interrupts();
    return value;
  }

  uint32_t invalidTransitions() const {
    noInterrupts();
    const uint32_t value = invalidTransitions_;
    interrupts();
    return value;
  }

  void zero() {
    noInterrupts();
    count_ = 0;
    invalidTransitions_ = 0;
    previousState_ = readPins();
    interrupts();
  }

private:
  volatile int32_t count_ = 0;
  volatile uint32_t invalidTransitions_ = 0;
  volatile uint8_t previousState_ = 0;

  static uint8_t readPins() {
    return static_cast<uint8_t>((digitalReadFast(PIN_ENCODER_A) ? 2U : 0U) |
                                (digitalReadFast(PIN_ENCODER_B) ? 1U : 0U));
  }

  static void onEdge() { instance().handleEdge(); }

  void handleEdge() {
    // Index is previous AB in bits 3:2 and current AB in bits 1:0.
    const uint8_t current = readPins();
    const int8_t delta = quadratureDelta(previousState_, current);
    if (delta != 0) {
      count_ += static_cast<int32_t>(delta) * ENCODER_SIGN;
    } else if (illegalQuadratureTransition(previousState_, current)) {
      // Both bits changed between samples: an edge was missed or signal is noisy.
      ++invalidTransitions_;
    }
    previousState_ = current;
  }
};

class DrivePositionBench {
public:
  static DrivePositionBench &instance() {
    static DrivePositionBench bench;
    return bench;
  }

  void begin() {
    // Establish LOW/OFF before the driver can be enabled.
    pinMode(PIN_RPWM, OUTPUT);
    pinMode(PIN_LPWM, OUTPUT);
    analogWriteResolution(8);
    analogWriteFrequency(PIN_RPWM, PWM_HZ); // Pins 7/8 intentionally share FlexPWM1_3.
    analogWriteFrequency(PIN_LPWM, PWM_HZ);
    analogWrite(PIN_RPWM, 0);
    analogWrite(PIN_LPWM, 0);
    pinMode(PIN_ENABLE, OUTPUT);
    digitalWriteFast(PIN_ENABLE, LOW);
    lastMotorOffMs_ = millis();

    QuadratureCounter::instance().begin();
    lastObservedCount_ = QuadratureCounter::instance().count();
    lastCountChangeMs_ = millis();

    Serial.begin(USB_BAUD_LABEL);
  }

  void loopOnce() {
    const uint32_t nowUs = micros();
    if (static_cast<uint32_t>(nowUs - lastControlUs_) >= CONTROL_PERIOD_US) {
      lastControlUs_ = nowUs;
      controlTick(); // No Serial access in this function.
    }

    serviceUsbConnection();
    pollCommands();
    serviceEvent();
    serviceStatus();
  }

private:
  MotionState state_ = MotionState::Disarmed;
  Event pendingEvent_ = Event::None;
  const char *faultReason_ = "NONE";
  char command_[COMMAND_CAPACITY] = {};
  size_t commandLength_ = 0;
  bool commandOverflow_ = false;
  bool usbConnected_ = false;
  bool motorEnabled_ = false;
  int8_t motorDirection_ = 0;
  uint8_t motorPwmPercent_ = 0;
  uint32_t armedAtMs_ = 0;
  uint32_t operationStartedMs_ = 0;
  uint32_t phaseStartedMs_ = 0;
  uint32_t lastMotorOffMs_ = 0;
  uint32_t lastCountChangeMs_ = 0;
  uint32_t lastStatusMs_ = 0;
  uint32_t lastControlUs_ = 0;
  uint32_t invalidAtStart_ = 0;
  uint32_t calibrationDurationMs_ = 0;
  int8_t calibrationDirection_ = 0;
  int32_t targetCounts_ = 0;
  int32_t operationStartCount_ = 0;
  int32_t returnStartCount_ = 0;
  int32_t lastObservedCount_ = 0;
  int32_t outboundStopCount_ = 0;
  uint32_t droppedLogs_ = 0;

  bool isActive() const {
    return state_ == MotionState::CalForward || state_ == MotionState::CalReverse ||
           state_ == MotionState::Outbound || state_ == MotionState::ReverseDwell ||
           state_ == MotionState::Returning;
  }

  bool electricalInterfacesReady() const {
    return ENCODER_ELECTRICAL_INTERFACE_VERIFIED && BTS_CONTROL_INTERFACE_VERIFIED;
  }

  bool positionConfigurationReady() const {
    return electricalInterfacesReady() && MAX_TARGET_COUNTS > 0;
  }

  void motorOff() {
    // Safety order: remove PWM before disabling both half bridges.
    const bool wasEnabled = motorEnabled_;
    analogWrite(PIN_RPWM, 0);
    analogWrite(PIN_LPWM, 0);
    digitalWriteFast(PIN_ENABLE, LOW);
    motorEnabled_ = false;
    motorDirection_ = 0;
    motorPwmPercent_ = 0;
    // Reissuing OFF while already idle must not indefinitely restart the
    // reversal timer. Only a real energized-to-off transition starts it.
    if (wasEnabled)
      lastMotorOffMs_ = millis();
  }

  void motorDrive(int8_t direction, uint8_t percent) {
    if (direction == 0 || percent == 0) {
      motorOff();
      return;
    }
    if (motorEnabled_ && direction != motorDirection_) {
      trip("INTERNAL_DIRECTION_CHANGE_BLOCKED");
      return;
    }
    if (percent > PWM_MAX_PERCENT)
      percent = PWM_MAX_PERCENT;
    const uint8_t raw = pwmRaw8(percent);
    // Logical FWD uses RPWM. Swap M+/M- or change ENCODER_SIGN after measured calibration,
    // never by reversing both PWM outputs while energized.
    if (direction > 0) {
      analogWrite(PIN_LPWM, 0);
      digitalWriteFast(PIN_ENABLE, HIGH);
      analogWrite(PIN_RPWM, raw);
    } else {
      analogWrite(PIN_RPWM, 0);
      digitalWriteFast(PIN_ENABLE, HIGH);
      analogWrite(PIN_LPWM, raw);
    }
    motorEnabled_ = true;
    motorDirection_ = direction;
    motorPwmPercent_ = percent;
  }

  void trip(const char *reason) {
    motorOff(); // Dangerous errors stop outputs before reporting.
    state_ = MotionState::Fault;
    faultReason_ = reason;
    pendingEvent_ = Event::Fault;
  }

  void observeEncoder(uint32_t now) {
    const int32_t position = QuadratureCounter::instance().count();
    if (position != lastObservedCount_) {
      lastObservedCount_ = position;
      lastCountChangeMs_ = now;
    }
  }

  bool encoderHealthyForMotion(uint32_t now) {
    const uint32_t invalidNow = QuadratureCounter::instance().invalidTransitions();
    if (static_cast<uint32_t>(invalidNow - invalidAtStart_) > MAX_INVALID_TRANSITIONS_PER_RUN) {
      trip("ENCODER_INVALID_TRANSITIONS");
      return false;
    }
    if (motorEnabled_ &&
        static_cast<uint32_t>(now - phaseStartedMs_) >= ENCODER_STARTUP_GRACE_MS &&
        static_cast<uint32_t>(now - lastCountChangeMs_) >= ENCODER_STALL_MS) {
      trip("ENCODER_NO_PROGRESS");
      return false;
    }
    return true;
  }

  uint8_t dutyForRemaining(int32_t remaining) const {
    return saver_drive_position_bench::dutyForRemaining(
        remaining, SLOW_ZONE_COUNTS, CRUISE_PWM_PERCENT, SLOW_PWM_PERCENT);
  }

  void controlTick() {
    const uint32_t now = millis();
    observeEncoder(now);

    if (state_ == MotionState::Armed && static_cast<uint32_t>(now - armedAtMs_) > ARM_VALID_MS) {
      motorOff();
      state_ = MotionState::Disarmed;
      faultReason_ = "ARM_EXPIRED";
      return;
    }
    if (!isActive())
      return;
    if (!usbConnected_ || !Serial) {
      trip("USB_DISCONNECTED");
      return;
    }
    if (!encoderHealthyForMotion(now))
      return;

    const int32_t position = QuadratureCounter::instance().count();
    if (state_ == MotionState::CalForward || state_ == MotionState::CalReverse) {
      if (static_cast<uint32_t>(now - operationStartedMs_) >= calibrationDurationMs_) {
        motorOff();
        state_ = MotionState::Complete;
        pendingEvent_ = Event::CalibrationDone;
      }
      return;
    }

    if (static_cast<uint32_t>(now - operationStartedMs_) >= MOVE_TIMEOUT_MS) {
      trip("MOVE_TIMEOUT");
      return;
    }

    if (state_ == MotionState::Outbound) {
      if (wrongOutboundDirection(position, operationStartCount_, WRONG_DIRECTION_TOLERANCE_COUNTS)) {
        trip("WRONG_DIRECTION_OUTBOUND");
        return;
      }
      const int32_t remaining = targetCounts_ - position;
      if (remaining <= 0) {
        motorOff();
        outboundStopCount_ = position;
        returnStartCount_ = position;
        phaseStartedMs_ = now;
        state_ = MotionState::ReverseDwell;
        return;
      }
      motorDrive(1, dutyForRemaining(remaining));
      return;
    }

    if (state_ == MotionState::ReverseDwell) {
      const uint32_t dwell = static_cast<uint32_t>(now - phaseStartedMs_);
      const uint32_t quiet = static_cast<uint32_t>(now - lastCountChangeMs_);
      if (dwell >= REVERSE_DEADTIME_MS && quiet >= SETTLE_QUIET_MS) {
        phaseStartedMs_ = now;
        invalidAtStart_ = QuadratureCounter::instance().invalidTransitions();
        lastCountChangeMs_ = now;
        state_ = MotionState::Returning;
        motorDrive(-1, dutyForRemaining(position > 0 ? position : 0));
      } else if (dwell >= SETTLE_TIMEOUT_MS) {
        trip("NOT_SETTLED_BEFORE_REVERSE");
      }
      return;
    }

    if (state_ == MotionState::Returning) {
      if (wrongReturnDirection(position, returnStartCount_, WRONG_DIRECTION_TOLERANCE_COUNTS)) {
        trip("WRONG_DIRECTION_RETURN");
        return;
      }
      if (position <= 0) {
        motorOff();
        state_ = MotionState::Complete;
        pendingEvent_ = Event::MoveDone;
        return;
      }
      motorDrive(-1, dutyForRemaining(position));
    }
  }

  void emit(const char *format, ...) {
    char text[320];
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

  void serviceUsbConnection() {
    const bool connected = static_cast<bool>(Serial);
    if (!connected) {
      if (usbConnected_ && isActive())
        trip("USB_DISCONNECTED");
      usbConnected_ = false;
      commandLength_ = 0;
      commandOverflow_ = false;
      return;
    }
    if (usbConnected_)
      return;
    motorOff();
    if (state_ != MotionState::Fault)
      state_ = MotionState::Disarmed;
    usbConnected_ = true;
    emit("SAVER_DRIVE_MOTOR_ENCODER_POSITION_TEST_READY,build=count_roundtrip_v1");
    emit("PINS,RPWM=7,LPWM=8,EN=15,ENC_A=11,ENC_B=12,PWM_HZ=%lu",
         static_cast<unsigned long>(PWM_HZ));
    emit("LOCKS,encoder_interface=%u,bts_interface=%u,max_target_counts=%ld",
         ENCODER_ELECTRICAL_INTERFACE_VERIFIED ? 1U : 0U,
         BTS_CONTROL_INTERFACE_VERIFIED ? 1U : 0U,
         static_cast<long>(MAX_TARGET_COUNTS));
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
          trip("COMMAND_TOO_LONG");
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
      const bool wasActive = isActive();
      motorOff();
      if (state_ == MotionState::Fault) {
        emit("ERROR,FAULT_LATCHED_USE_CLEAR_FAULT,reason=%s", faultReason_);
      } else {
        state_ = MotionState::Disarmed;
        pendingEvent_ = wasActive ? Event::Stopped : Event::None;
        emit("OK,STOPPED");
      }
    } else if (strcmp(command, "DISARM") == 0 && noExtraToken(save)) {
      motorOff();
      if (state_ == MotionState::Fault) {
        emit("ERROR,FAULT_LATCHED_USE_CLEAR_FAULT,reason=%s", faultReason_);
      } else {
        state_ = MotionState::Disarmed;
        emit("OK,DISARMED");
      }
    } else if (strcmp(command, "CLEAR_FAULT") == 0 && noExtraToken(save)) {
      clearFault();
    } else if (strcmp(command, "ZERO") == 0 && noExtraToken(save)) {
      zeroEncoder();
    } else if (strcmp(command, "ARM") == 0 && noExtraToken(save)) {
      arm();
    } else if (strcmp(command, "RUN") == 0) {
      char *targetText = strtok_r(nullptr, " \t", &save);
      long target = 0;
      if (!parseLong(targetText, 1, LONG_MAX, target) || !noExtraToken(save)) {
        emit("ERROR,USAGE_RUN_target_counts");
      } else {
        startPositionRun(static_cast<int32_t>(target));
      }
    } else if (strcmp(command, "CAL_FWD") == 0 || strcmp(command, "CAL_REV") == 0) {
      const bool forward = strcmp(command, "CAL_FWD") == 0;
      char *pwmText = strtok_r(nullptr, " \t", &save);
      char *timeText = strtok_r(nullptr, " \t", &save);
      long pwm = 0;
      long duration = 0;
      if (!parseLong(pwmText, PWM_MIN_PERCENT, PWM_MAX_PERCENT, pwm) ||
          !parseLong(timeText, 100, CAL_MAX_RUN_MS, duration) || !noExtraToken(save)) {
        emit("ERROR,USAGE_%s_pct_duration_ms", forward ? "CAL_FWD" : "CAL_REV");
      } else {
        startCalibration(forward ? 1 : -1, static_cast<uint8_t>(pwm),
                         static_cast<uint32_t>(duration));
      }
    } else {
      emit("ERROR,UNKNOWN_OR_MALFORMED_COMMAND");
    }
  }

  void help() {
    emit("COMMANDS,STATUS|ZERO|ARM|CAL_FWD pct ms|CAL_REV pct ms|RUN target_counts|STOP|DISARM|CLEAR_FAULT|HELP");
    emit("NORMAL_RUN,0_to_target_then_deadtime_then_return_to_0,count_controlled=1");
    emit("CAL_ONLY,time_limited=1,pct=%u..%u,ms=100..%lu",
         static_cast<unsigned>(PWM_MIN_PERCENT), static_cast<unsigned>(PWM_MAX_PERCENT),
         static_cast<unsigned long>(CAL_MAX_RUN_MS));
  }

  void arm() {
    if (state_ == MotionState::Fault) {
      emit("ERROR,FAULT_LATCHED_USE_CLEAR_FAULT,reason=%s", faultReason_);
      return;
    }
    if (isActive()) {
      emit("ERROR,BUSY");
      return;
    }
    if (!electricalInterfacesReady()) {
      emit("ERROR,ELECTRICAL_INTERFACE_NOT_VERIFIED,encoder=%u,bts=%u",
           ENCODER_ELECTRICAL_INTERFACE_VERIFIED ? 1U : 0U,
           BTS_CONTROL_INTERFACE_VERIFIED ? 1U : 0U);
      return;
    }
    motorOff();
    state_ = MotionState::Armed;
    armedAtMs_ = millis();
    emit("OK,ARMED,valid_ms=%lu", static_cast<unsigned long>(ARM_VALID_MS));
  }

  void zeroEncoder() {
    if (isActive() || state_ == MotionState::Armed) {
      emit("ERROR,ZERO_REQUIRES_DISARMED");
      return;
    }
    if (state_ == MotionState::Fault) {
      emit("ERROR,FAULT_LATCHED_USE_CLEAR_FAULT");
      return;
    }
    motorOff();
    QuadratureCounter::instance().zero();
    lastObservedCount_ = 0;
    lastCountChangeMs_ = millis();
    emit("OK,ZEROED,position_counts=0");
  }

  void clearFault() {
    motorOff();
    if (state_ != MotionState::Fault) {
      emit("ERROR,NOT_IN_FAULT");
      return;
    }
    state_ = MotionState::Disarmed;
    faultReason_ = "NONE";
    pendingEvent_ = Event::None;
    emit("OK,FAULT_CLEARED,state=DISARMED");
  }

  bool consumeFreshArm() {
    const uint32_t now = millis();
    if (state_ == MotionState::Fault) {
      motorOff();
      emit("ERROR,FAULT_LATCHED_USE_CLEAR_FAULT,reason=%s", faultReason_);
      return false;
    }
    if (isActive()) {
      emit("ERROR,BUSY");
      return false;
    }
    if (state_ != MotionState::Armed || static_cast<uint32_t>(now - armedAtMs_) > ARM_VALID_MS) {
      motorOff();
      state_ = MotionState::Disarmed;
      emit("ERROR,FRESH_ARM_REQUIRED");
      return false;
    }
    if (static_cast<uint32_t>(now - lastMotorOffMs_) < REVERSE_DEADTIME_MS) {
      emit("ERROR,DEADTIME_ACTIVE,wait_ms=%lu",
           static_cast<unsigned long>(REVERSE_DEADTIME_MS - (now - lastMotorOffMs_)));
      return false;
    }
    return true;
  }

  void prepareMotion() {
    const uint32_t now = millis();
    operationStartedMs_ = now;
    phaseStartedMs_ = now;
    operationStartCount_ = QuadratureCounter::instance().count();
    lastObservedCount_ = operationStartCount_;
    lastCountChangeMs_ = now;
    invalidAtStart_ = QuadratureCounter::instance().invalidTransitions();
    pendingEvent_ = Event::None;
  }

  void startCalibration(int8_t direction, uint8_t pwm, uint32_t duration) {
    if (!consumeFreshArm())
      return;
    prepareMotion();
    calibrationDurationMs_ = duration;
    calibrationDirection_ = direction;
    state_ = direction > 0 ? MotionState::CalForward : MotionState::CalReverse;
    motorDrive(direction, pwm);
    emit("OK,%s_STARTED,pwm_pct=%u,duration_ms=%lu,start_counts=%ld",
         direction > 0 ? "CAL_FWD" : "CAL_REV", static_cast<unsigned>(pwm),
         static_cast<unsigned long>(duration), static_cast<long>(operationStartCount_));
  }

  void startPositionRun(int32_t target) {
    if (!consumeFreshArm())
      return;
    if (!positionConfigurationReady()) {
      motorOff();
      state_ = MotionState::Disarmed;
      emit("ERROR,POSITION_CONFIG_LOCKED,max_target_counts=%ld",
           static_cast<long>(MAX_TARGET_COUNTS));
      return;
    }
    if (!targetAllowed(target, MAX_TARGET_COUNTS)) {
      motorOff();
      state_ = MotionState::Disarmed;
      emit("ERROR,TARGET_EXCEEDS_LIMIT,target=%ld,max=%ld", static_cast<long>(target),
           static_cast<long>(MAX_TARGET_COUNTS));
      return;
    }
    const int32_t position = QuadratureCounter::instance().count();
    if (position < -FINAL_TOLERANCE_COUNTS || position > FINAL_TOLERANCE_COUNTS) {
      motorOff();
      state_ = MotionState::Disarmed;
      emit("ERROR,NOT_AT_ZERO,position_counts=%ld", static_cast<long>(position));
      return;
    }
    QuadratureCounter::instance().zero();
    targetCounts_ = target;
    outboundStopCount_ = 0;
    prepareMotion();
    state_ = MotionState::Outbound;
    motorDrive(1, dutyForRemaining(targetCounts_));
    emit("OK,RUN_STARTED,target_counts=%ld,cruise_pct=%u,slow_pct=%u,slow_zone_counts=%ld",
         static_cast<long>(targetCounts_), static_cast<unsigned>(CRUISE_PWM_PERCENT),
         static_cast<unsigned>(SLOW_PWM_PERCENT), static_cast<long>(SLOW_ZONE_COUNTS));
  }

  void serviceEvent() {
    if (pendingEvent_ == Event::None || !usbConnected_)
      return;
    const Event event = pendingEvent_;
    pendingEvent_ = Event::None;
    const int32_t position = QuadratureCounter::instance().count();
    if (event == Event::Fault) {
      emit("ERROR,%s,motor_off=1,position_counts=%ld", faultReason_, static_cast<long>(position));
    } else if (event == Event::CalibrationDone) {
      emit("EVENT,CAL_DONE,direction=%s,start_counts=%ld,end_counts=%ld,signed_delta=%ld,invalid=%lu",
           calibrationDirection_ > 0 ? "FWD" : "REV",
           static_cast<long>(operationStartCount_), static_cast<long>(position),
           static_cast<long>(position - operationStartCount_),
           static_cast<unsigned long>(QuadratureCounter::instance().invalidTransitions() - invalidAtStart_));
    } else if (event == Event::MoveDone) {
      const int32_t error = position;
      emit("EVENT,MOVE_DONE,result=%s,target_counts=%ld,outbound_stop_counts=%ld,final_counts=%ld,final_error_counts=%ld",
           withinTolerance(error, FINAL_TOLERANCE_COUNTS) ? "OK" : "OVERSHOOT",
           static_cast<long>(targetCounts_), static_cast<long>(outboundStopCount_),
           static_cast<long>(position), static_cast<long>(error));
    } else if (event == Event::Stopped) {
      emit("EVENT,USER_STOPPED,position_counts=%ld", static_cast<long>(position));
    }
  }

  void status() {
    const int32_t position = QuadratureCounter::instance().count();
    emit("STATUS,state=%s,position_counts=%ld,target_counts=%ld,motor_enabled=%u,direction=%d,pwm_pct=%u,invalid=%lu,fault=%s,dropped_logs=%lu",
         stateName(state_), static_cast<long>(position), static_cast<long>(targetCounts_),
         motorEnabled_ ? 1U : 0U, static_cast<int>(motorDirection_),
         static_cast<unsigned>(motorPwmPercent_),
         static_cast<unsigned long>(QuadratureCounter::instance().invalidTransitions()), faultReason_,
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
};

} // namespace

void setup() { DrivePositionBench::instance().begin(); }

void loop() { DrivePositionBench::instance().loopOnce(); }
