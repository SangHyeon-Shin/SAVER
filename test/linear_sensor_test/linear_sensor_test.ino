/*
 * SAVER linear-sensor-only bench test, Teensy 4.1 / USB Serial.
 * Research bench only. No motor control, PWM, VESC commands, or safety interlocks.
 * DISCONNECT every motor's power/control wiring before uploading this sketch.
 * This sketch does NOT initialize their EN pins or stop an attached motor.
 *
 * Conditioned sensor OUT -> A16 / physical pin 40 (NOT digital pin 16).
 * Sensor signal GND -> Teensy GND. Supply voltage/pin order: sensor datasheet.
 * A16 must stay within 0..3.3 V; NEVER connect 5 V/10 V output directly.
 * User: YwRobot 75 mm / 10 kohm / 0..VCC module, supplied at 3.3 V.
 * ZERO -> manually measured positive distance -> CAL mm. No automatic movement.
 * position_mm is omitted until two measured points are collected successfully.
 * A floating/disconnected input can produce plausible raw values.
 *
 * Serial Monitor 115200, New Line or Both NL & CR.
 * READ = fresh sample, START/STOP = USB streaming only, STATUS, HELP.
 * ZERO, CAL mm, CAL_SHOW, CAL_RESET, CHECK measured_mm.
 * Successful CAL saves to EEPROM automatically; boot restores a validated record.
 * ZERO/new CAL/CAL_RESET invalidate the previous saved calibration first.
 * Streaming starts automatically (sensor read only, never motor startup).
 */

#include <Arduino.h>
#include <EEPROM.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "linear_reading.h"
#include "linear_calibration.h"
#include "linear_calibration_store.h"

#if !defined(ARDUINO_TEENSY41)
#error "Select Teensy 4.1 and USB Type Serial."
#endif

// Same assignment as teensy/pinmap.h; checker detects any future mismatch.
constexpr uint8_t PIN_LINEAR_SENSOR = A16; // Outer ADC pin 40, reserved for displacement input.

/* ================== 사용자가 수정하는 부분 ================== */
constexpr uint32_t SAMPLE_PERIOD_MS = 100; // <<< 출력 간격: 100 ms = 초당 10회.
constexpr float ADC_REFERENCE_V = 3.3f; // <<< 공칭 ADC 기준 전압. 센서 공급전압을 넣지 마세요.
/* ZERO/CAL 성공 시 EEPROM 자동 저장, 재부팅 시 복원. 자동 모터 이동 없음. */

static_assert(A16 == 40, "Teensy 4.1 A16 must be physical pin 40, not pin 16");
static_assert(PIN_LINEAR_SENSOR == saver_linear::storage::PIN,
              "Update the EEPROM profile if the sensor pin changes");
static_assert(E2END + 1 >= saver_linear::storage::ADDRESS + saver_linear::storage::SIZE,
              "Calibration EEPROM slot does not fit");
static_assert(SAMPLE_PERIOD_MS >= 10 && SAMPLE_PERIOD_MS <= 60000,
              "Sensor-only bench interval must be 10..60000 ms");
static_assert(ADC_REFERENCE_V > 0.0f && ADC_REFERENCE_V <= 3.3f,
              "ADC reference is not the sensor's 5/10/24 V supply");

class LinearSensorTest {
public:
  void begin() {
    pinMode(PIN_LINEAR_SENSOR, INPUT_DISABLE); // Analog only; no pull-up/pull-down bias.
    analogReadResolution(12);
    analogReadAveraging(16);
    const auto loaded = saver_linear::storage::load(EEPROM, calibration_);
    saved_ = loaded == saver_linear::storage::LoadResult::Loaded;
    storageState_ = saved_                                                 ? "LOADED"
                    : loaded == saver_linear::storage::LoadResult::Empty   ? "EMPTY"
                    : loaded == saver_linear::storage::LoadResult::NoSpace ? "NO_SPACE"
                                                                           : "INVALID";
    Serial.begin(115200);
    lastSampleMs_ = millis();
  }

  void tick() {
    const bool connected = static_cast<bool>(Serial);
    if (connected != usbConnected_) {
      usbConnected_ = connected;
      command_.reset();
      // Discard commands already queued at the USB session boundary, bounded per loop.
      discardUsbBytes_ = connected ? Serial.available() : 0;
      banner_ = 0;
      // Completed calibration survives closing the monitor while still powered.
      // A partial point never survives a USB session boundary.
      capture_.cancel();
      captureMode_ = 0;
    }
    if (connected) {
      pollCommands();
      printBanner();
    }
    if (connected)
      serviceCapture();
    const uint32_t now = millis();
    if (saver_linear::elapsed(now, lastSampleMs_, SAMPLE_PERIOD_MS)) {
      lastSampleMs_ = now; // Do not burst/catch up after a slow host.
      if (streaming_ && connected && banner_ >= BANNER_LINES)
        printMeasurement();
    }
  }

private:
  static constexpr uint8_t BANNER_LINES = 6;
  bool streaming_ = true;
  bool usbConnected_ = false;
  uint8_t banner_ = 0;
  int discardUsbBytes_ = 0;
  uint32_t lastSampleMs_ = 0;
  uint32_t droppedLogs_ = 0;
  saver_linear::CommandLine command_;
  saver_linear::LinearCalibration calibration_;
  saver_linear::PointCapture capture_;
  uint8_t captureMode_ = 0; // 1=ZERO, 2=CAL.
  float captureDistanceMm_ = 0;
  bool saved_ = false;
  const char *storageState_ = "NOT_LOADED";

  // Single USB writer, fixed-length whole lines. Never wait/flush for a slow monitor.
  bool emit(const char *format, ...) {
    char output[224];
    va_list args;
    va_start(args, format);
    const int length = vsnprintf(output, sizeof(output), format, args);
    va_end(args);
    if (length <= 0 || static_cast<size_t>(length) >= sizeof(output) || !Serial ||
        Serial.availableForWrite() < length) {
      ++droppedLogs_;
      return false;
    }
    if (Serial.write(reinterpret_cast<const uint8_t *>(output), length) !=
        static_cast<size_t>(length)) {
      ++droppedLogs_;
      return false;
    }
    return true;
  }

  void printMeasurement() {
    // Each READ/stream line obtains a new sample; no stale data is replayed.
    const int raw = analogRead(PIN_LINEAR_SENSOR);
    const saver_linear::Reading reading = saver_linear::convert(raw, ADC_REFERENCE_V);
    const unsigned long now = millis();
    if (!reading.raw_ok) {
      emit("DATA,ms=%lu,raw_ok=0,position_ok=0,error=ADC_RESULT_INVALID\n", now);
      return; // Do not replace a failed measurement with zero.
    }
    float mm = 0;
    if (calibration_.position(raw, mm)) {
      emit("DATA,ms=%lu,raw_ok=1,position_ok=1,adc_raw=%d,adc_V=%.4f,adc_rail=%u,"
           "position_mm=%.3f,within_cal_span=%u\n",
           now, reading.raw, static_cast<double>(reading.adc_V), reading.rail ? 1U : 0U,
           static_cast<double>(mm), calibration_.within(mm) ? 1U : 0U);
    } else {
      emit("DATA,ms=%lu,raw_ok=1,position_ok=0,adc_raw=%d,adc_V=%.4f,adc_rail=%u,"
           "calibrated=%u,position_raw_usable=%u\n",
           now, reading.raw, static_cast<double>(reading.adc_V), reading.rail ? 1U : 0U,
           calibration_.ready() ? 1U : 0U, saver_linear::usablePositionRaw(raw) ? 1U : 0U);
    }
  }

  void printStatus() {
    emit("STATUS,streaming=%u,period_ms=%lu,zero_known=%u,calibrated=%u,capture_active=%u,"
         "sensor_presence_known=0,dropped_logs=%lu\n",
         streaming_ ? 1U : 0U, static_cast<unsigned long>(SAMPLE_PERIOD_MS),
         calibration_.zeroKnown() ? 1U : 0U, calibration_.ready() ? 1U : 0U,
         capture_.active() ? 1U : 0U, static_cast<unsigned long>(droppedLogs_));
  }

  void printBanner() {
    bool sent = false;
    switch (banner_) {
    case 0:
      sent = emit("SAVER_LINEAR_SENSOR_TEST_READY,build=calibration_eeprom_v3\n");
      break;
    case 1:
      sent =
          emit("PIN,A16=40,ADC_BITS=12,ADC_REFERENCE_V=%.4f,period_ms=%lu\n",
               static_cast<double>(ADC_REFERENCE_V), static_cast<unsigned long>(SAMPLE_PERIOD_MS));
      break;
    case 2:
      sent = emit("COMMANDS,READ|START|STOP|STATUS|HELP|ZERO|CAL mm|CAL_SHOW|CAL_RESET|CHECK mm\n");
      break;
    case 3:
      sent = emit("NOTE,ZERO_then_CAL_measured_distance_mm,auto_save=1,sensor_presence_known=0\n");
      break;
    case 4:
      sent = emit("WARNING,A16_max_3V3,disconnect_all_motor_power_and_controls\n");
      break;
    case 5:
      sent =
          emit("CAL_STORAGE,state=%s,saved=%u,calibrated=%u,eeprom_address=%d,bytes=%u\n",
               storageState_, saved_ ? 1U : 0U, calibration_.ready() ? 1U : 0U,
               saver_linear::storage::ADDRESS, static_cast<unsigned>(saver_linear::storage::SIZE));
      break;
    default:
      return;
    }
    if (sent)
      ++banner_;
  }

  void showCalibration() {
    emit("CAL,zero_known=%u,calibrated=%u,capture_active=%u,saved=%u,storage_state=%s,"
         "accuracy_verified=0\n",
         calibration_.zeroKnown() ? 1U : 0U, calibration_.ready() ? 1U : 0U,
         capture_.active() ? 1U : 0U, saved_ ? 1U : 0U, storageState_);
    if (calibration_.zeroKnown()) {
      const auto p = calibration_.zero();
      emit("CAL_ZERO,raw_mean=%.3f,raw_min=%d,raw_max=%d,samples=%u\n", static_cast<double>(p.mean),
           p.low, p.high, static_cast<unsigned>(p.count));
    }
    if (calibration_.ready()) {
      const auto p = calibration_.span();
      emit("CAL_SPAN,raw_mean=%.3f,raw_min=%d,raw_max=%d,samples=%u,distance_mm=%.3f,"
           "mm_per_count=%.8f\n",
           static_cast<double>(p.mean), p.low, p.high, static_cast<unsigned>(p.count),
           static_cast<double>(calibration_.distance()),
           static_cast<double>(calibration_.mmPerCount()));
    }
  }

  void startCapture(uint8_t mode, float distanceMm) {
    // Invalidate BEFORE new measurements so cancelled/failed recalibration cannot
    // resurrect an old installation's calibration after power cycling.
    if (!clearSavedCalibration()) {
      calibration_.reset();
      return;
    }
    if (mode == 1)
      calibration_.reset();
    else
      calibration_.invalidateScale();
    captureMode_ = mode;
    captureDistanceMm_ = distanceMm;
    capture_.start(millis());
    emit("OK,CAPTURE_STARTED,kind=%s,samples=50,hold_still_ms=500,timeout_ms=2000\n",
         mode == 1 ? "ZERO" : "CAL");
  }

  bool clearSavedCalibration() {
    saved_ = false;
    const bool ok = saver_linear::storage::invalidate(EEPROM);
    storageState_ = ok ? "CLEARED" : "CLEAR_FAILED";
    if (!ok)
      emit("ERROR,EEPROM_CLEAR_FAILED,calibrated=0,old_record_may_remain=1\n");
    return ok;
  }

  void serviceCapture() {
    if (!captureMode_)
      return;
    const uint32_t now = millis();
    capture_.expire(now);
    bool complete = false;
    if (capture_.due(now)) {
      const int raw = analogRead(PIN_LINEAR_SENSOR);
      complete = capture_.add(raw, millis());
    }
    if (capture_.error()) {
      emit("ERROR,%s,calibrated=0\n", capture_.error());
      captureMode_ = 0;
    } else if (complete) {
      const bool zero = captureMode_ == 1;
      const bool ok = zero ? calibration_.setZero(capture_.point())
                           : calibration_.setSpan(capture_.point(), captureDistanceMm_);
      captureMode_ = 0;
      if (!ok) {
        emit("ERROR,CAL_POINTS_TOO_CLOSE_OR_OVERLAPPING,calibrated=0\n");
      } else if (zero) {
        emit("OK,ZERO_SET,calibrated=0,saved=0\n");
      } else {
        saved_ = saver_linear::storage::save(EEPROM, calibration_);
        storageState_ = saved_ ? "SAVED" : "SAVE_FAILED";
        if (saved_)
          emit("OK,CAL_APPLIED,calibrated=1,saved=1\n");
        else {
          calibration_.invalidateScale();
          emit("ERROR,EEPROM_SAVE_VERIFY_FAILED,calibrated=0,saved=0\n");
        }
      }
      showCalibration();
    }
  }

  void checkPosition(float expectedMm) {
    const int raw = analogRead(PIN_LINEAR_SENSOR);
    float mm = 0;
    if (!calibration_.position(raw, mm)) {
      emit("ERROR,CHECK_REQUIRES_CALIBRATION_AND_NONRAIL_ADC\n");
      return;
    }
    emit("CHECK,expected_mm=%.3f,position_mm=%.3f,error_mm=%.3f,adc_raw=%d,"
         "within_cal_span=%u,accuracy_verified=0\n",
         static_cast<double>(expectedMm), static_cast<double>(mm),
         static_cast<double>(mm - expectedMm), raw, calibration_.within(mm) ? 1U : 0U);
  }

  void reportLineEvent(saver_linear::LineEvent event) {
    using saver_linear::LineEvent;
    switch (event) {
    case LineEvent::Invalid:
      emit("ERROR,COMMAND_INVALID_BYTE\n");
      break;
    case LineEvent::Overflow:
      emit("ERROR,COMMAND_TOO_LONG\n");
      break;
    case LineEvent::Timeout:
      emit("ERROR,COMMAND_TIMEOUT_DISCARD_UNTIL_LF\n");
      break;
    default:
      break;
    }
  }

  void handleCommand(const char *text) {
    using saver_linear::matches;
    if (matches(text, "READ")) {
      printMeasurement();
    } else if (matches(text, "START")) {
      streaming_ = true;
      lastSampleMs_ = millis();
      emit("OK,STREAMING_STARTED\n");
    } else if (matches(text, "STOP")) {
      streaming_ = false;
      capture_.cancel();
      captureMode_ = 0;
      emit("OK,STREAMING_STOPPED,capture_cancelled=1,motor_control=0\n");
    } else if (matches(text, "STATUS")) {
      printStatus();
    } else if (matches(text, "HELP")) {
      banner_ = 0;
    } else if (matches(text, "CAL_SHOW")) {
      showCalibration();
    } else if (matches(text, "CAL_RESET")) {
      capture_.cancel();
      captureMode_ = 0;
      calibration_.reset();
      if (clearSavedCalibration())
        emit("OK,CAL_RESET,calibrated=0,saved=0\n");
    } else if (!matches(text, "")) {
      if (capture_.active()) {
        emit("ERROR,CAPTURE_BUSY_WAIT_OR_STOP\n");
        return;
      }
      if (matches(text, "ZERO")) {
        startCapture(1, 0);
        return;
      }
      char copy[32];
      snprintf(copy, sizeof(copy), "%s", text);
      char *context = nullptr;
      char *cmd = strtok_r(copy, " ", &context);
      char *arg = strtok_r(nullptr, " ", &context);
      char *extra = strtok_r(nullptr, " ", &context);
      float mm = 0;
      if (!cmd || !arg || extra || !saver_linear::parseMm(arg, mm)) {
        emit("ERROR,USE_CAL_distance_mm_OR_CHECK_measured_mm\n");
      } else if (!strcmp(cmd, "CAL")) {
        if (!calibration_.zeroKnown() || mm < 5) {
          emit("ERROR,CAL_REQUIRES_ZERO_AND_MEASURED_5_TO_75_MM\n");
          return;
        }
        startCapture(2, mm);
      } else if (!strcmp(cmd, "CHECK"))
        checkPosition(mm);
      else
        emit("ERROR,UNKNOWN_COMMAND\n");
    }
  }

  void pollCommands() {
    reportLineEvent(command_.expire(millis()));
    for (uint8_t count = 0; count < 32 && Serial.available() > 0; ++count) {
      const int value = Serial.read();
      if (value < 0)
        break;
      if (discardUsbBytes_ > 0) {
        --discardUsbBytes_;
        continue;
      }
      const auto event = command_.feed(static_cast<char>(value), millis());
      if (event == saver_linear::LineEvent::Ready)
        handleCommand(command_.text());
      else
        reportLineEvent(event);
    }
  }
};

LinearSensorTest &linearTest() {
  static LinearSensorTest instance; // Mutable state scoped to the test object, not g_* globals.
  return instance;
}

void setup() { linearTest().begin(); }
void loop() { linearTest().tick(); }
