/*
 * SAVER vacuum + pressure bench experiment, Teensy 4.1 only.
 * Research / secured bench fixture ONLY. Not CPR firmware and not for human use.
 * STOP/fault/USB loss turns vacuum OFF. Never support a load by suction with this
 * sketch: no production vacuum retention, E-stop input, current limiting, pressure
 * regulation, or hardware watchdog is implemented. A physical stop is required.
 *
 * References: vacuum_motor_stage4_fg (now NO FG) and pressure_sensor_test.
 * M1: VSP24, EN26, ERR27 (LOW=fault), RUN25 (LOW=running indication).
 * M2: VSP33, EN37, ERR36, RUN32. FG and motor +5V OUTPUT unused/insulated.
 * 2026-10-02: rewire M2 RUN34->32 / EN35->37 before using this build.
 * Sensor: XGZP6847A100KPGN, regulated 5 V supply, common signal GND.
 * Sensor OUT -> 4.7k ohm -> A8/22 -> 10k ohm -> GND. A8 must stay <=3.3 V.
 * Do not supply the sensor from 3.3 V or tie the motor's +5V output to Teensy VIN.
 * ERR/RUN need verified 0..3.3 V levels and defined external bias, no internal pullup.
 * Motor current must bypass the breadboard and Teensy.
 *
 * USB 115200, newline. ARM then RUN1 10 is a bench command example, not
 * a verified safe current/load setting. Duty percent is NOT current in amperes.
 * RUN/RUN1 [pct], RUN2 [pct], RUNBOTH pct1 pct2 (wire both first).
 * Runs have NO duration/deadline: use STOP; do not leave a running bench unattended.
 * SET/SET1 pct, SET2 pct change duty. SET 0 stops the selected motor.
 * STOP/STOP1/STOP2, DISARM, CLEAR_ERR/CLEAR_ERR1/CLEAR_ERR2, STATUS, READ, HELP.
 * CLEAR_ERR aliases motor 1; ERR HIGH must remain stable for 250 ms; no auto restart.
 * PLOT = Arduino Serial Plotter; TEXT = detailed DATA/events (default).
 * Open plotter BEFORE ARM/RUN. Changing USB clients stops and disarms motors.
 * Invalid pressure stops/disarms and exits PLOT; pressure_Pa is omitted, never zero.
 * See README.md for wiring, formula, graph usage and unverified assumptions.
 */
#include <Arduino.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "bench_logic.h"
#include "pressure_conversion.h"

#if !defined(ARDUINO_TEENSY41)
#error "Select Teensy 4.1 for this bench sketch"
#endif

namespace {
using namespace saver_vacuum_bench;
using namespace saver_pressure_bench;

// ===== 기본 듀티 수정 위치: 수정 후 재업로드 (자동 시작하지 않음) =====
constexpr uint8_t DEFAULT_DUTY_PERCENT = 6; // PWM 6%이며 6 A가 아님.
// 작동시간 설정 없음: RUN 후 STOP/오류/USB 단절/압력 이상 전까지 계속 구동.
// =======================================================================
constexpr uint8_t PIN_VAC1_VSP = 24; // FlexPWM1_2: motor 2와 다른 주파수 그룹.
constexpr uint8_t PIN_VAC1_EN = 26;  // motor 1 LOW disable; Stage4와 동일.
constexpr uint8_t PIN_VAC1_ERR = 27; // motor 1 active-LOW 오류 입력.
constexpr uint8_t PIN_VAC1_RUN = 25; // motor 1 active-LOW 상태 입력, 출력으로 쓰지 않음.
constexpr uint8_t PIN_VAC2_VSP = 33; // FlexPWM2_0: motor 1과 다른 그룹.
constexpr uint8_t PIN_VAC2_EN = 37;  // motor 2 독립 LOW disable; Serial8 TX35와 분리.
constexpr uint8_t PIN_VAC2_ERR = 36; // motor 2 active-LOW 오류 입력.
constexpr uint8_t PIN_VAC2_RUN = 32; // motor 2 active-LOW 상태 입력; Serial8 RX34와 분리.
constexpr uint8_t PIN_PRESSURE = A8; // pressure_sensor_test와 같은 물리 핀 22.
constexpr uint8_t VSP_PINS[2] = {PIN_VAC1_VSP, PIN_VAC2_VSP};
constexpr uint8_t EN_PINS[2] = {PIN_VAC1_EN, PIN_VAC2_EN};
constexpr uint8_t ERR_PINS[2] = {PIN_VAC1_ERR, PIN_VAC2_ERR};
constexpr uint8_t RUN_PINS[2] = {PIN_VAC1_RUN, PIN_VAC2_RUN};
// Existing setting retained: supplied sheets disagree (10 vs 30..150 kHz).
// Confirm the actual driver version before considering this a verified frequency.
constexpr float PWM_FREQUENCY_HZ = 10000.0f;
constexpr uint32_t SAMPLE_PERIOD_MS = 100; // 10 Hz, no interpolated/fabricated samples.
constexpr uint8_t SERIAL_BUDGET = 32;
static_assert(A8 == 22, "Teensy 4.1 A8 mapping required");
static_assert(DEFAULT_DUTY_PERCENT >= 1 && DEFAULT_DUTY_PERCENT <= 100, "Invalid default duty");

class VacuumPressureBench {
 public:
  void begin() {
    // Fast write clears the actual output latch BEFORE OUTPUT on Teensy 4.x.
    for (unsigned i = 0; i < 2; ++i) {
      digitalWriteFast(EN_PINS[i], LOW); pinMode(EN_PINS[i], OUTPUT);
      digitalWriteFast(VSP_PINS[i], LOW); pinMode(VSP_PINS[i], OUTPUT);
    }
    analogWriteResolution(8);
    for (unsigned i = 0; i < 2; ++i) {
      analogWriteFrequency(VSP_PINS[i], PWM_FREQUENCY_HZ);
      pinMode(ERR_PINS[i], INPUT); pinMode(RUN_PINS[i], INPUT);
    }
    stopOutputs(3);
    pinMode(PIN_PRESSURE, INPUT);
    analogReadResolution(12);
    analogReadAveraging(16);
    Serial.begin(115200);
    const uint32_t started = millis();
    while (!Serial && (uint32_t)(millis() - started) < 2000U) {}
    usbReady_ = (bool)Serial;
    samplePressure();
    emit("SAVER_VACUUM_PRESSURE_BENCH_TEST_READY,build=no_fg_pa_manual_v3_pinmap");
    emitf("PINS,M1_VSP=%u,M1_EN=%u,M1_ERR=%u,M1_RUN=%u,M2_VSP=%u,M2_EN=%u,M2_ERR=%u,M2_RUN=%u,A8=%u,FG=UNUSED",
          (unsigned)PIN_VAC1_VSP, (unsigned)PIN_VAC1_EN, (unsigned)PIN_VAC1_ERR, (unsigned)PIN_VAC1_RUN,
          (unsigned)PIN_VAC2_VSP, (unsigned)PIN_VAC2_EN, (unsigned)PIN_VAC2_ERR, (unsigned)PIN_VAC2_RUN,
          (unsigned)PIN_PRESSURE);
    emit("SAFETY,motors_off_commanded=1,armed=0,usb_disconnect_stops=1,run_mode=MANUAL_STOP");
    help();
    checkSafety();
  }

  void tick() {
    checkSafety();
    if (synchronizeUsb()) pollCommands();
    checkSafety();
    if ((uint32_t)(millis() - sampledAt_) >= SAMPLE_PERIOD_MS) {
      samplePressure();
      checkSafety();
      printMeasurement();
    }
  }

 private:
  void emit(const char* text) {
    const size_t length = strlen(text);
    if (!Serial || Serial.availableForWrite() < (int)(length + 1U)) { ++droppedLogs_; return; }
    Serial.write((const uint8_t*)text, length);
    Serial.write((uint8_t)'\n');
  }
  void emitf(const char* format, ...) {
    char text[512];
    va_list args; va_start(args, format);
    const int n = vsnprintf(text, sizeof text, format, args);
    va_end(args);
    if (n < 0 || (size_t)n >= sizeof text) { ++droppedLogs_; return; }
    emit(text);
  }
  void stopOutputs(uint8_t mask) {
    for (unsigned i = 0; i < 2; ++i) if (mask & (1U << i)) {
      analogWrite(VSP_PINS[i], 0);
      digitalWriteFast(EN_PINS[i], LOW);
      digitalWriteFast(VSP_PINS[i], LOW);
      pinMode(VSP_PINS[i], OUTPUT); // STOP leaves static LOW, not PWM mux.
    }
  }
  void stop(uint8_t mask = 3, bool disarm = false) {
    stopOutputs(mask);
    control_.stop(mask, disarm);
  }
  void reject(const char* reason) {
    stop(3, true); // Outputs BEFORE logs even if the log cannot be sent.
    plot_ = false; // No fake plot zero; return to readable diagnosis.
    emitf("ERROR,%s,motors_off=1,armed=0,mode=TEXT", reason);
  }
  void checkSafety() {
    const SafetyEvent event = control_.observe(digitalRead(PIN_VAC1_ERR) == LOW,
                                               digitalRead(PIN_VAC2_ERR) == LOW, millis());
    stopOutputs(event.stopMask);
    if (event.faultMask) {
      plot_ = false;
      emitf("FAULT,ERR_LOW,motor_mask=%u,latched=1,mode=TEXT", (unsigned)event.faultMask);
    }
  }
  void samplePressure() {
    pressure_ = convertPressure(analogRead(PIN_PRESSURE));
    sampledAt_ = millis();
    if (!pressure_.ok && (control_.armed() || control_.runningMask() || plot_))
      reject(pressure_.reason);
  }
  bool synchronizeUsb() {
    if (!Serial) {
      if (usbReady_ || control_.runningMask() || control_.armed()) {
        stop(3, true); ++usbDisconnects_;
      }
      usbReady_ = false; line_.reset(); plot_ = false;
      return false;
    }
    if (!usbReady_) {
      for (uint8_t i = 0; i < SERIAL_BUDGET && Serial.available(); ++i) Serial.read();
      if (Serial.available()) return false;
      stop(3, true); line_.reset(); usbReady_ = true; plot_ = false;
      emit("EVENT,USB_CONNECTED,motors_off=1,armed=0,send_fresh_command=1");
      help();
      return false;
    }
    return true;
  }
  void printMeasurement() {
    if (plot_) {
      if (!pressure_.ok) { reject(pressure_.reason); return; }
      // Fixed labeled columns: no ms/ADC/volts to spoil the Pa scale.
      emitf("pressure_Pa:%.1f,pressure_ok:1,armed:%u,m1_commanded:%u,m2_commanded:%u,m1_fault:%u,m2_fault:%u",
            (double)pressure_.pressurePa, control_.armed() ? 1U : 0U,
            control_.motor(0).running() ? 1U : 0U, control_.motor(1).running() ? 1U : 0U,
            control_.motor(0).fault() ? 1U : 0U, control_.motor(1).fault() ? 1U : 0U);
      return;
    }
    char value[48] = "";
    if (pressure_.ok) snprintf(value, sizeof value, ",pressure_Pa=%.1f", (double)pressure_.pressurePa);
    if (!pressure_.adcOk) {
      emitf("DATA,ms=%lu,pressure_ok=0,reason=%s", (unsigned long)sampledAt_, pressure_.reason);
      return;
    }
    emitf("DATA,ms=%lu,adc_raw=%d,adc_V=%.4f,sensor_V=%.4f,adc_rail=%u,"
          "pressure_ok=%u,pressure_in_nominal_range=%u,reason=%s%s,armed=%u,m1_pct=%u,m2_pct=%u",
          (unsigned long)sampledAt_, pressure_.raw, (double)pressure_.adcV, (double)pressure_.sensorV,
          pressure_.rail ? 1U : 0U, pressure_.ok ? 1U : 0U, pressure_.inNominalRange ? 1U : 0U,
          pressure_.reason, value, control_.armed() ? 1U : 0U,
          (unsigned)control_.motor(0).duty(), (unsigned)control_.motor(1).duty());
  }
  void printStatus() {
    emitf("STATUS,armed=%u,mode=%s,pressure_ok=%u,usb_disconnects=%lu,dropped_logs=%lu",
          control_.armed() ? 1U : 0U, plot_ ? "PLOT" : "TEXT", pressure_.ok ? 1U : 0U,
          (unsigned long)usbDisconnects_, (unsigned long)droppedLogs_);
    for (unsigned i = 0; i < 2; ++i) {
      const ManualRunState& motor = control_.motor(i);
      emitf("MOTOR,id=%u,commanded_running=%u,pwm_pct=%u,pwm_raw=%u,run_mode=MANUAL_STOP,"
            "en_pin_raw=%u,err_raw=%u,err_fault_latched=%u,run_raw=%u",
            i + 1U, motor.running() ? 1U : 0U, (unsigned)motor.duty(), (unsigned)dutyToPwm(motor.duty()),
            (unsigned)digitalRead(EN_PINS[i]),
            (unsigned)digitalRead(ERR_PINS[i]), motor.fault() ? 1U : 0U, (unsigned)digitalRead(RUN_PINS[i]));
    }
  }
  void help() {
    emit("COMMANDS,ARM|RUN/RUN1 [pct]|RUN2 [pct]|RUNBOTH pct1 pct2|SET/SET1 pct|SET2 pct");
    emit("COMMANDS,STOP|STOP1|STOP2|DISARM|CLEAR_ERR/CLEAR_ERR1|CLEAR_ERR2|READ|STATUS|HELP|PLOT|TEXT");
    emitf("DEFAULTS,pwm_pct=%u,run_mode=MANUAL_STOP,PWM_HZ=10000", (unsigned)DEFAULT_DUTY_PERCENT);
    emit("LIMITS,pct=1..100,partial_command_timeout_ms=1000");
    emit("NOTE,no_run_timer,use_STOP,legacy_RUN_duration_argument_is_rejected");
    emit("PRESSURE,XGZP6847A100KPGN_5V,unit=Pa_gauge,divider=4k7_top_10k_bottom,nominal_ADC_V=3.3");
    emit("NOTE,PLOT_is_10Hz,uncheck_other_traces_for_pressure_only,TEXT_for_diagnosis");
    emit("WARNING,ERR_RUN_require_0_to_3V3,FG_and_motor_5V_output_unused,pwm_pct_is_not_current_A");
    emit("WARNING,secured_bench_only,no_vacuum_retention,no_pressure_regulation,no_hardware_stop_replacement");
  }
  bool noMore(char*& context) { return strtok_r(nullptr, " \t", &context) == nullptr; }
  void runCommand(uint8_t mask, char*& context) {
    uint32_t duty1 = DEFAULT_DUTY_PERCENT, duty2 = DEFAULT_DUTY_PERCENT;
    const char* first = strtok_r(nullptr, " \t", &context);
    const char* second = strtok_r(nullptr, " \t", &context);
    const char* extra = strtok_r(nullptr, " \t", &context);
    const char* parseError = parseRunDuties(mask, first, second, extra, DEFAULT_DUTY_PERCENT, duty1, duty2);
    if (parseError) { reject(parseError); return; }
    samplePressure(); // Never start from stale cached ADC data.
    checkSafety();
    const char* error = control_.start(mask, duty1, duty2, pressure_.ok);
    if (error) { reject(error); return; }
    stopOutputs(3);
    for (unsigned i = 0; i < 2; ++i) if (mask & (1U << i)) {
      digitalWriteFast(EN_PINS[i], HIGH);
      analogWrite(VSP_PINS[i], dutyToPwm(control_.motor(i).duty()));
    }
    if (!plot_) emitf("OK,RUNNING,motor_mask=%u,m1_pct=%u,m2_pct=%u,run_mode=MANUAL_STOP",
                     (unsigned)mask, (unsigned)control_.motor(0).duty(),
                     (unsigned)control_.motor(1).duty());
    printMeasurement();
  }
  void setCommand(unsigned index, char*& context) {
    uint32_t duty = 0;
    if (!parseUnsigned(strtok_r(nullptr, " \t", &context), duty) || !noMore(context)) {
      reject("BAD_SET_ARGUMENTS"); return;
    }
    checkSafety();
    const char* error = control_.set(index, duty);
    if (error) { reject(error); return; }
    if (!control_.motor(index).running()) stopOutputs((uint8_t)(1U << index));
    else analogWrite(VSP_PINS[index], dutyToPwm(control_.motor(index).duty()));
    if (!plot_) emitf("OK,DUTY_CHANGED,motor=%u,pwm_pct=%lu", index + 1U, (unsigned long)duty);
    printMeasurement();
  }
  void handleCommand(char* text) {
    char* context = nullptr;
    char* cmd = strtok_r(text, " \t", &context);
    if (!cmd) return;
    for (char* p = cmd; *p; ++p) *p = (char)toupper((unsigned char)*p);
    if (!strcmp(cmd, "RUN") || !strcmp(cmd, "RUN1")) { runCommand(1, context); return; }
    if (!strcmp(cmd, "RUN2")) { runCommand(2, context); return; }
    if (!strcmp(cmd, "RUNBOTH")) { runCommand(3, context); return; }
    if (!strcmp(cmd, "SET") || !strcmp(cmd, "SET1")) { setCommand(0, context); return; }
    if (!strcmp(cmd, "SET2")) { setCommand(1, context); return; }
    if (!noMore(context)) { reject("UNEXPECTED_ARGUMENTS"); return; }
    if (!strcmp(cmd, "ARM")) {
      stop(3, true); samplePressure(); checkSafety();
      const char* error = control_.arm(pressure_.ok);
      if (error) { reject(error); return; }
      if (!plot_) emit("OK,ARMED,motors_off=1");
      printMeasurement();
    } else if (!strcmp(cmd, "STOP") || !strcmp(cmd, "STOP1") || !strcmp(cmd, "STOP2") || !strcmp(cmd, "DISARM")) {
      const uint8_t mask = !strcmp(cmd, "STOP1") ? 1 : (!strcmp(cmd, "STOP2") ? 2 : 3);
      stop(mask, !strcmp(cmd, "DISARM"));
      if (!plot_) emitf("OK,MOTOR_OFF,motor_mask=%u", (unsigned)mask);
      printMeasurement();
    } else if (!strcmp(cmd, "CLEAR_ERR") || !strcmp(cmd, "CLEAR_ERR1") || !strcmp(cmd, "CLEAR_ERR2")) {
      checkSafety();
      const uint8_t mask = !strcmp(cmd, "CLEAR_ERR2") ? 2 : 1;
      const char* error = control_.clear(mask, millis());
      if (error) reject(error);
      else { stopOutputs(mask); if (!plot_) emitf("OK,ERR_CLEARED,motor_mask=%u", (unsigned)mask); }
    } else if (!strcmp(cmd, "PLOT")) {
      samplePressure();
      if (!pressure_.ok) { reject(pressure_.reason); return; }
      plot_ = true; printMeasurement();
    } else if (!strcmp(cmd, "TEXT")) {
      plot_ = false; emit("OK,MODE_TEXT"); printStatus();
    } else if (!strcmp(cmd, "READ")) { samplePressure(); printMeasurement(); }
    else if (!strcmp(cmd, "STATUS")) { plot_ = false; printStatus(); }
    else if (!strcmp(cmd, "HELP")) { plot_ = false; help(); }
    else reject("UNKNOWN_OR_MALFORMED_COMMAND");
  }
  void pollCommands() {
    if (line_.expire(millis())) reject("COMMAND_TIMEOUT");
    for (uint8_t i = 0; i < SERIAL_BUDGET && Serial.available(); ++i) {
      const LineResult result = line_.push((char)Serial.read(), millis());
      if (result == LineResult::Invalid) { reject("INVALID_OR_TOO_LONG_COMMAND"); return; }
      if (result == LineResult::Ready) {
        handleCommand(line_.data()); line_.reset(); return;
      }
    }
  }

  CombinedState control_;
  CommandLine line_;
  PressureSample pressure_;
  bool usbReady_ = false, plot_ = false;
  uint32_t sampledAt_ = 0, usbDisconnects_ = 0, droppedLogs_ = 0;
};

VacuumPressureBench& bench() {
  static VacuumPressureBench instance;
  return instance;
}
} // namespace

void setup() { bench().begin(); }
void loop() { bench().tick(); }
