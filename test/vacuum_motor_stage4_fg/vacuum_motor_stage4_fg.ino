/*
 * SAVER vacuum motor bench test - Stage 4: VSP + EN + ERR + RUN (NO FG).
 * Teensy 4.1, vacuum motor 1 ONLY. No pressure sensor or current regulation.
 * 기존 폴더/파일 경로는 유지하지만 FG 기능은 제거했습니다 (2026-09-08).
 *
 * 단독 시험(vacuum_motor_single_test.ino)의 현재 기본값 6% / 15초를 반영.
 * RUN만 입력하면 아래 기본값 사용. SET은 실행 중 듀티만 바꾸며 시간을 늘리지 않음.
 * 전원을 넣어도 자동 RUN 없음. 최대 30초 후 소프트웨어 정지.
 *
 * 배선: VSP=24, EN=26, ERR=27, RUN=25, 공통 신호 GND. FG는 미연결/끝 절연.
 * 숫자 24번과 인접한 3.3V 전원 단자를 혼동하지 마세요.
 * RUN 핀은 드라이버 상태 OUTPUT -> Teensy INPUT, 시리얼 RUN 명령과 별개.
 * ERR/RUN은 연결 전 0..3.3V인지 측정. INPUT 모드는 미연결 검출 기능이 아님.
 * 오픈컬렉터라면 3.3V 외부 풀업, 5V push-pull이라면 레벨 변환 필요.
 * 제공된 제품 설명의 FG는 5V 펄스, +5V는 외부 패널용 전원 출력.
 * FG와 +5V는 모두 미연결/끝 절연. +5V를 Teensy VIN/3.3V/GPIO에 연결하지 말 것.
 *
 * ERR LOW를 오류로 래치하고 정지. 해제 후 250ms 이상 HIGH + CLEAR_ERR 필요.
 * RUN LOW는 제공된 제품 표의 동작 상태. 전압/정지 성능은 별도 검증 필요.
 * FG 핀 설정/인터럽트/펄스 집계/RPM 출력 없음. 핀 32는 사용하지 않음.
 * RUN은 관찰용이며 실제 회전속도나 흡착 압력을 보증하지 않음.
 *
 * 정지 출력은 PWM 0 + EN LOW + VSP 정적 LOW. 리셋 이전 구간을 보장하지 않음.
 * EN의 하드웨어 기본 LOW 바이어스와 물리 전원 차단이 별도로 필요.
 * 실제 모터 전압/전류는 코드로 보장하지 않음. 브레드보드에 모터 주전류를 흘리지 말 것.
 * 업로드/배선 변경 시 모든 모터 주전원 분리. 연구용 벤치 시험 전용.
 *
 * USB Serial 115200, Newline 또는 Both NL & CR.
 * RUN [pct] [duration_ms] | SET pct | STOP | CLEAR_ERR | STATUS | HELP
 * 예: RUN -> 6% / 15000ms, RUN 6 15000 -> 명시적 지정, SET 0 -> 정지.
 * USB 연결 해제/잘못된 명령/부분 명령 1초 timeout 시 정지. 재연결해도 자동 재시작 없음.
 * USB 로그는 공간이 없으면 생략하며 dropped_logs로 집계. 출력 지연이 제어를 막지 않음.
 * commanded_running/motor_off는 소프트웨어 지령이며 실제 회전/정지 확인값이 아님.
 */
#include <Arduino.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "bench_logic.h"

#if !defined(ARDUINO_TEENSY41)
#error "Select Teensy 4.1 for this bench sketch"
#endif

namespace {
using namespace saver_vacuum_bench;

// ===== 사용자가 기본 듀티/시간을 수정하는 곳 (수정 후 재업로드 필요) =====
constexpr uint8_t DEFAULT_DUTY_PERCENT = 6; // <<< 기본 PWM 듀티비: 6% (6A 아님)
constexpr uint32_t DEFAULT_RUN_MS = 15000; // <<< 기본 작동시간: 15000ms = 15초
// ====================================================================

constexpr uint8_t PIN_MOTOR_VSP = 24; // 단독 시험과 같은 FlexPWM1_2 속도 출력.
constexpr uint8_t PIN_MOTOR_EN = 26;  // 단독 시험과 같은 LOW 정지/브레이크 출력.
constexpr uint8_t PIN_MOTOR_ERR = 27; // 모터 1 ERR 입력; EN에 인접.
constexpr uint8_t PIN_MOTOR_RUN = 25; // 모터 1 상태 입력; 절대로 구동 출력으로 설정하지 않음.
constexpr uint8_t ERR_ACTIVE_LEVEL = LOW;
constexpr uint8_t RUN_ACTIVE_LEVEL = LOW;
constexpr float PWM_FREQUENCY_HZ = 10000.0f;
constexpr uint32_t STATUS_PERIOD_MS = 500;
constexpr uint8_t SERIAL_BUDGET = 32;
static_assert(DEFAULT_DUTY_PERCENT >= 1 && DEFAULT_DUTY_PERCENT <= 100,
              "Default duty must be between 1 and 100");
static_assert(DEFAULT_RUN_MS >= MIN_RUN_MS && DEFAULT_RUN_MS <= MAX_RUN_MS,
              "Default duration must be between 100 and 30000 ms");

class VacuumBench {
 public:
  void begin() {
    // digitalWrite(LOW) before OUTPUT only changes an input pulldown on Teensy.
    // digitalWriteFast clears the GPIO output latch before changing direction.
    digitalWriteFast(PIN_MOTOR_EN, LOW);
    pinMode(PIN_MOTOR_EN, OUTPUT);
    digitalWriteFast(PIN_MOTOR_VSP, LOW);
    pinMode(PIN_MOTOR_VSP, OUTPUT);
    analogWriteResolution(8);
    analogWriteFrequency(PIN_MOTOR_VSP, PWM_FREQUENCY_HZ);
    motorOff();
    // Preserve the original INPUT configuration. External wiring must set defined levels.
    pinMode(PIN_MOTOR_ERR, INPUT);
    pinMode(PIN_MOTOR_RUN, INPUT);

    Serial.begin(115200);
    const uint32_t started = millis();
    while (!Serial && (uint32_t)(millis() - started) < 2000U) {}
    usbReady_ = (bool)Serial;
    emit("SAVER_VACUUM_MOTOR_STAGE4_NO_FG_READY,build=no_fg_v1");
    emit("PINS,VSP=24,EN=26,ERR=27,RUN=25,FG=UNUSED,PWM_HZ=10000");
    emit("FEATURES,fg_enabled=0,rpm_available=0");
    emit("SAFETY,motor_off_commanded_at_boot=1,max_run_ms=30000,usb_disconnect_stops=1");
    help();
    checkSafety();
  }

  void tick() {
    checkSafety();
    if (synchronizeUsb()) pollCommands();
    checkSafety();
    const uint32_t now = millis();
    if ((uint32_t)(now - lastStatusMs_) >= STATUS_PERIOD_MS) {
      lastStatusMs_ = now;
      printStatus(); // Idle diagnostics also expose ERR/RUN raw levels.
    }
  }

 private:
  void emit(const char* text) {
    const size_t length = strlen(text);
    if (!Serial || Serial.availableForWrite() < (int)(length + 1U)) {
      ++droppedLogs_;
      return;
    }
    Serial.write((const uint8_t*)text, length);
    Serial.write((uint8_t)'\n');
  }

  void emitf(const char* format, ...) {
    char line[512];
    va_list args;
    va_start(args, format);
    const int length = vsnprintf(line, sizeof line, format, args);
    va_end(args);
    if (length < 0 || (size_t)length >= sizeof line) { ++droppedLogs_; return; }
    emit(line);
  }

  void writeOutputsOff() {
    analogWrite(PIN_MOTOR_VSP, 0);
    digitalWriteFast(PIN_MOTOR_EN, LOW);
    // Switch away from the PWM mux, making STOP explicitly static LOW.
    digitalWriteFast(PIN_MOTOR_VSP, LOW);
    pinMode(PIN_MOTOR_VSP, OUTPUT);
  }

  void motorOff() {
    writeOutputsOff(); // Physical output commands precede every fault/error log.
    control_.stop();
  }

  bool errActive() const { return digitalRead(PIN_MOTOR_ERR) == ERR_ACTIVE_LEVEL; }

  void checkSafety() {
    const uint32_t now = millis();
    const bool newlyLatched = control_.observeErr(errActive(), now);
    if (newlyLatched) {
      motorOff();
      emit("FAULT,ERR_ACTIVE,motor_off=1,latched=1");
    }
    if (control_.expire(now)) {
      motorOff();
      emit("EVENT,RUN_TIMEOUT_MOTOR_OFF");
    }
  }

  bool synchronizeUsb() {
    if (!Serial) {
      if (usbReady_ || control_.running()) {
        motorOff();
        ++usbDisconnects_;
      }
      usbReady_ = false;
      line_.reset();
      return false;
    }
    if (!usbReady_) {
      // Drop queued bytes of the old session in bounded batches.
      for (uint8_t i = 0; i < SERIAL_BUDGET && Serial.available(); ++i) Serial.read();
      if (Serial.available()) return false;
      motorOff();
      line_.reset();
      usbReady_ = true;
      emit("EVENT,USB_CONNECTED,motor_off=1,send_fresh_command=1");
      help();
      return false;
    }
    return true;
  }

  void printStatus() {
    const int err = digitalRead(PIN_MOTOR_ERR);
    const int run = digitalRead(PIN_MOTOR_RUN);
    emitf("STATUS,commanded_running=%u,pwm_pct=%u,pwm_raw=%u,remaining_ms=%lu,"
          "en_pin_raw=%u,err_raw=%u,err_active_candidate=%u,err_fault_latched=%u,"
          "run_raw=%u,run_active_candidate=%u,"
          "usb_disconnects=%lu,dropped_logs=%lu",
          control_.running() ? 1U : 0U, (unsigned)control_.duty(),
          (unsigned)dutyToPwm(control_.duty()), (unsigned long)control_.remaining(millis()),
          (unsigned)digitalRead(PIN_MOTOR_EN), (unsigned)err,
          err == ERR_ACTIVE_LEVEL ? 1U : 0U, control_.fault() ? 1U : 0U,
          (unsigned)run, run == RUN_ACTIVE_LEVEL ? 1U : 0U,
          (unsigned long)usbDisconnects_, (unsigned long)droppedLogs_);
  }

  void help() {
    emit("COMMANDS,RUN [pct] [duration_ms]|SET pct|STOP|CLEAR_ERR|STATUS|HELP");
    emitf("DEFAULTS,pwm_pct=%u,duration_ms=%lu",
          (unsigned)DEFAULT_DUTY_PERCENT, (unsigned long)DEFAULT_RUN_MS);
    emit("LIMITS,pct=1..100,duration_ms=100..30000,command_timeout_ms=1000");
    emit("NOTE,pwm_pct_is_not_motor_current_A,commanded_running_is_not_measured_rotation");
    emit("NOTE,ERR_LOW_latches_stop,CLEAR_ERR_requires_HIGH_for_250ms,no_automatic_restart");
    emit("NOTE,RUN_pin_is_input,FG_is_disabled,no_RPM_measurement,pin32_is_unused");
    emit("WARNING,ERR_RUN_require_defined_0_to_3V3_levels,unconnected_INPUT_is_not_detectable");
    emit("WARNING,leave_motor_FG_and_5V_output_disconnected_and_insulated,physical_stop_required");
  }

  void reject(const char* reason) {
    motorOff();
    emitf("ERROR,%s,motor_off=1", reason);
  }

  bool noMoreTokens(char*& context) { return strtok_r(nullptr, " \t", &context) == nullptr; }

  void handleRun(char*& context) {
    const char* dutyText = strtok_r(nullptr, " \t", &context);
    const char* durationText = strtok_r(nullptr, " \t", &context);
    uint32_t duty = DEFAULT_DUTY_PERCENT, duration = DEFAULT_RUN_MS;
    if (dutyText && !parseUnsigned(dutyText, duty)) { reject("BAD_RUN_DUTY"); return; }
    if (durationText && !parseUnsigned(durationText, duration)) { reject("BAD_RUN_DURATION"); return; }
    if (!noMoreTokens(context)) { reject("RUN_OUT_OF_RANGE"); return; }
    checkSafety();
    const char* error = control_.start(duty, duration, millis());
    if (error) { reject(error); return; }
    writeOutputsOff();
    lastStatusMs_ = millis();
    digitalWriteFast(PIN_MOTOR_EN, HIGH);
    analogWrite(PIN_MOTOR_VSP, dutyToPwm(control_.duty()));
    emitf("OK,RUNNING,pwm_pct=%lu,duration_ms=%lu", (unsigned long)duty, (unsigned long)duration);
  }

  void handleSet(char*& context) {
    uint32_t duty = 0;
    if (!parseUnsigned(strtok_r(nullptr, " \t", &context), duty) || !noMoreTokens(context)) {
      reject("BAD_SET_DUTY"); return;
    }
    checkSafety();
    const char* error = control_.set(duty);
    if (error) { reject(error); return; }
    if (!control_.running()) { motorOff(); emit("OK,MOTOR_OFF"); return; }
    analogWrite(PIN_MOTOR_VSP, dutyToPwm(control_.duty()));
    emitf("OK,DUTY_CHANGED,pwm_pct=%lu", (unsigned long)duty);
  }

  void handleCommand(char* text) {
    char* context = nullptr;
    char* cmd = strtok_r(text, " \t", &context);
    if (!cmd) return;
    for (char* p = cmd; *p; ++p) *p = (char)toupper((unsigned char)*p);
    if (strcmp(cmd, "RUN") == 0) handleRun(context);
    else if (strcmp(cmd, "SET") == 0) handleSet(context);
    else if (strcmp(cmd, "STOP") == 0 && noMoreTokens(context)) {
      motorOff(); emit("OK,MOTOR_OFF");
    } else if (strcmp(cmd, "CLEAR_ERR") == 0 && noMoreTokens(context)) {
      checkSafety();
      const char* error = control_.clearErr(millis());
      if (error) reject(error);
      else { motorOff(); emit("OK,ERR_FAULT_CLEARED"); }
    } else if (strcmp(cmd, "STATUS") == 0 && noMoreTokens(context)) printStatus();
    else if (strcmp(cmd, "HELP") == 0 && noMoreTokens(context)) help();
    else reject("UNKNOWN_OR_MALFORMED_COMMAND");
  }

  void pollCommands() {
    if (line_.expire(millis())) reject("COMMAND_TIMEOUT");
    for (uint8_t i = 0; i < SERIAL_BUDGET && Serial.available() > 0; ++i) {
      const LineResult result = line_.push((char)Serial.read(), millis());
      if (result == LineResult::Invalid) {
        reject("INVALID_OR_TOO_LONG_COMMAND");
        return;
      }
      if (result == LineResult::Ready) {
        handleCommand(line_.data());
        line_.reset();
        return; // Re-check ERR and run timeout between complete commands.
      }
    }
  }

  RunState control_;
  CommandLine line_;
  bool usbReady_ = false;
  uint32_t lastStatusMs_ = 0, droppedLogs_ = 0, usbDisconnects_ = 0;
};

VacuumBench& bench() {
  static VacuumBench instance; // All mutable state is owned by the bench instance.
  return instance;
}
} // namespace

void setup() { bench().begin(); }
void loop() { bench().tick(); }
