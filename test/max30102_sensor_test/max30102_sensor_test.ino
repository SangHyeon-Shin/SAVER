/*
 * SAVER MAX30102-only bench test, Teensy 4.1. See README.md for wiring.
 * DISCONNECT ALL MOTOR POWER before upload, reset, or sensor testing.
 * No motor RUN command exists. Known vacuum EN/VSP pins are held LOW as an
 * extra precaution, not a guarantee during reset or against wiring faults.
 *
 * SDA=18, SCL=19, GND=GND; I2C pull-ups must go to 3.3 V (never UNO's 5 V).
 * Use the breakout's documented VIN supply, not the bare chip's VDD pin.
 * INT/RD/IRD are unused. Photo alone does not establish the exact chip model.
 *
 * Library: SparkFun MAX3010x Pulse and Proximity Sensor Library, tested 1.1.2.
 * We use its Maxim SpO2 algorithm, plus a separate repeatability-gated HR estimate,
 * and check I2C transaction lengths ourselves
 * so a failed FIFO read cannot be converted into plausible sensor numbers.
 * Settings follow SparkFun Example8: 100 Hz / averaging 4 = 25 FIFO samples/s,
 * 411 us, ADC range 4096 nA, red + IR. Pulse window: 200 samples / eight seconds.
 * Maxim SpO2 still uses the latest 100 samples / four seconds, only after gating.
 *
 * USB Serial 115200, newline: HELP shows diagnostic/calibration commands.
 * START/STOP control this optical sensor, NOT motors. Acquisition auto-starts.
 * Invalid/stale physical values are omitted, not substituted with zero.
 * HR/SpO2 are unvalidated algorithm estimates for bench investigation only;
 * optical contact gating suppresses estimates after removal, but cannot prove
 * skin/live tissue contact or medical accuracy. See optical_window.h settings.
 * No medical thresholds, diagnoses, or control decisions are implemented.
 *
 * References:
 * https://www.analog.com/media/en/technical-documentation/data-sheets/max30102.pdf
 * https://github.com/sparkfun/SparkFun_MAX3010x_Sensor_Library
 */

#include <Arduino.h>
#include <Wire.h>
#include <spo2_algorithm.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "pinmap.h"
#include "optical_window.h"
#include "ppg_diagnostics.h"
#include "ppg_signal.h"

namespace {
constexpr uint8_t SENSOR_ADDRESS = 0x57;
constexpr uint8_t EXPECTED_PART_ID = 0x15; // Shared by MAX30102 and MAX30105.
constexpr uint32_t I2C_CLOCK_HZ = 100000;  // Conservative breadboard bus rate.
constexpr uint8_t LED_AMPLITUDE = 60;      // EDIT brightness here: 1..255, not percent.
constexpr uint32_t PRINT_PERIOD_MS = 100;  // EDIT log period: 100 ms = 10 Hz.
constexpr uint32_t POLL_PERIOD_MS = 10;
constexpr uint32_t RETRY_PERIOD_MS = 2000;
constexpr uint32_t STALE_MS = 1000;
constexpr uint16_t WINDOW_SAMPLES = saver_max_bench::WINDOW_SAMPLES;
constexpr uint32_t ADC_MAX = saver_max_bench::ADC_MAX;
constexpr uint8_t FIFO_CONFIG = 0x4F; // Average 4, no rollover, almost-full at 17.
constexpr uint8_t SPO2_CONFIG = 0x27; // 4096 nA, 100 Hz, 411 us (18 bits).
static_assert(FreqS == 25 && BUFFER_SIZE == WINDOW_SAMPLES / 2,
              "Algorithm and sensor sample rates must match");
static_assert(LED_AMPLITUDE > 0, "Use STOP to turn off acquisition/LEDs");
static_assert(PRINT_PERIOD_MS >= 50, "Limit bench summaries to at most 20 Hz");

void inhibitVacuumOutputs() {
  // Teensy digitalWriteFast explicitly clears the GPIO latch even before OUTPUT.
  digitalWriteFast(PIN_VAC1_EN_GUARD, LOW);
  digitalWriteFast(PIN_VAC2_EN_GUARD, LOW);
  pinMode(PIN_VAC1_EN_GUARD, OUTPUT);
  pinMode(PIN_VAC2_EN_GUARD, OUTPUT);
  digitalWriteFast(PIN_VAC1_VSP_GUARD, LOW);
  digitalWriteFast(PIN_VAC2_VSP_GUARD, LOW);
  pinMode(PIN_VAC1_VSP_GUARD, OUTPUT);
  pinMode(PIN_VAC2_VSP_GUARD, OUTPUT);
}

class Max30102Bench {
public:
  void begin() {
    Wire.setSDA(PIN_MAX_SDA);
    Wire.setSCL(PIN_MAX_SCL);
    Wire.begin();
    Wire.setClock(I2C_CLOCK_HZ);
    lastAttemptMs_ = millis() - RETRY_PERIOD_MS;
    emit("SAVER_MAX30102_SENSOR_TEST_READY,build=pulse_quality_v3");
    emit("PINS,SDA=18,SCL=19,INT=unused,I2C_HZ=100000,ADDRESS=0x57");
    emit("WARNING,DISCONNECT_ALL_MOTOR_POWER,NO_MOTOR_RUN_COMMAND");
    emit("WARNING,I2C_PULLUPS_TO_3V3_ONLY,HR_SPO2_NOT_MEDICALLY_VALIDATED");
    char contactConfig[160];
    snprintf(contactConfig, sizeof contactConfig,
             "CONTACT_CONFIG,on_raw=%lu,off_raw=%lu,settle_ms=%lu,max_gap_ms=%lu",
             (unsigned long)saver_max_bench::CONTACT_ON_RAW,
             (unsigned long)saver_max_bench::CONTACT_OFF_RAW,
             (unsigned long)saver_max_bench::CONTACT_SETTLE_MS,
             (unsigned long)saver_max_bench::CONTACT_MAX_GAP_MS);
    emit(contactConfig);
    emit("PULSE_CONFIG,window_samples=200,step_samples=25,confirm_windows=2,bench_heuristics=1");
    help();
  }

  void tick() {
    const bool usb = bool(Serial);
    if (usb != usbConnected_) {
      usbConnected_ = usb;
      commandLine_.reset();
      calibration_.cancel();
      clearSamples();
      if (usb)
        emit("EVENT,USB_CONNECTED,partial_command_and_capture_cleared");
    }
    const uint32_t now = millis();
    if (enabled_) {
      if (!connected_ && (uint32_t)(now - lastAttemptMs_) >= RETRY_PERIOD_MS) {
        lastAttemptMs_ = now;
        connectSensor();
      } else if (connected_ && (uint32_t)(now - lastPollMs_) >= POLL_PERIOD_MS) {
        lastPollMs_ = now;
        collect();
      }
    }
    reportCaptureEvent(calibration_.expire(millis()));
    optical_.expire(millis());
    if (!optical_.contactReady())
      resetPulseQuality();
    flushDiagnostics();
    pollCommands();
    if (enabled_ && outputMode_ != OutputMode::Plot &&
        (uint32_t)(millis() - lastPrintMs_) >= PRINT_PERIOD_MS) {
      lastPrintMs_ = millis();
      printData();
    }
  }

private:
  void emit(const char *line) {
    const size_t length = strlen(line);
    // Dropping a log must not block FIFO service. No heap or endless USB waits.
    if (!Serial || Serial.availableForWrite() < (int)(length + 1U)) {
      ++droppedLogs_;
      return;
    }
    Serial.write((const uint8_t *)line, length);
    Serial.write((uint8_t)'\n');
  }

  bool readRegisters(uint8_t reg, uint8_t *bytes, uint8_t count) {
    // Teensyduino 1.62 Wire bounds bus-idle/transfer waits internally (~16/50 ms).
    // This sketch has no unbounded wait for bytes and checks every short read.
    Wire.beginTransmission(SENSOR_ADDRESS);
    Wire.write(reg);
    lastI2cStatus_ = Wire.endTransmission(false);
    if (lastI2cStatus_ != 0)
      return false;
    const uint8_t received = Wire.requestFrom(SENSOR_ADDRESS, count);
    if (received != count || Wire.available() != count) {
      lastI2cStatus_ = 6; // Local diagnostic: short read, not a Wire status code.
      return false;
    }
    for (uint8_t i = 0; i < count; ++i)
      bytes[i] = (uint8_t)Wire.read();
    return true;
  }

  bool writeRegister(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(SENSOR_ADDRESS);
    Wire.write(reg);
    Wire.write(value);
    lastI2cStatus_ = Wire.endTransmission();
    return lastI2cStatus_ == 0;
  }

  void clearSamples() {
    haveSample_ = false;
    optical_.reset();
    rawCount_ = 0;
    metricsPending_ = false;
    plotInitialized_ = false;
    resetPulseQuality();
  }

  void resetPulseQuality() {
    pulseQuality_ = {};
    confirmation_.reset();
    pulseConfirmed_ = false;
    spo2Reason_ = "WAITING_WINDOW";
  }

  const char *pulseReason() const {
    if (pulseQuality_.accepted && !pulseConfirmed_)
      return "CONFIRMING";
    return saver_max_bench::signalReasonName(pulseQuality_.reason);
  }

  void fail(const char *reason) {
    connected_ = false;
    resetCalibration();
    clearSamples();
    lastError_ = reason;
    lastAttemptMs_ = millis();
    ++errors_;
    char line[160];
    snprintf(line, sizeof line, "ERROR,ms=%lu,code=%s,i2c_status=%u,retry_ms=%lu",
             (unsigned long)millis(), reason, (unsigned)lastI2cStatus_,
             (unsigned long)RETRY_PERIOD_MS);
    emit(line);
  }

  void connectSensor() {
    clearSamples();
    if (!readRegisters(0xFF, &partId_, 1)) {
      fail("I2C_NO_SENSOR");
      return;
    }
    if (partId_ != EXPECTED_PART_ID) {
      fail("UNSUPPORTED_PART_ID");
      return;
    }
    if (!writeRegister(0x09, 0x40)) {
      fail("RESET_WRITE_FAILED");
      return;
    }
    const uint32_t resetStarted = millis();
    uint8_t mode = 0x40;
    do {
      if (!readRegisters(0x09, &mode, 1)) {
        fail("RESET_READ_FAILED");
        return;
      }
      if ((mode & 0x40) == 0)
        break;
      delay(1);
    } while ((uint32_t)(millis() - resetStarted) < 150U);
    if (mode & 0x40) {
      fail("RESET_TIMEOUT");
      return;
    }

    // Stay shut down while configuring; red then IR is the SpO2 FIFO order.
    if (!writeRegister(0x09, 0x80) || !writeRegister(0x02, 0x80) || !writeRegister(0x03, 0x00) ||
        !writeRegister(0x04, 0x00) || !writeRegister(0x05, 0x00) || !writeRegister(0x06, 0x00) ||
        !writeRegister(0x08, FIFO_CONFIG) || !writeRegister(0x0A, SPO2_CONFIG) ||
        !writeRegister(0x0C, ledCode_) || !writeRegister(0x0D, ledCode_) ||
        !writeRegister(0x09, 0x03)) {
      fail("CONFIG_WRITE_FAILED");
      return;
    }
    uint8_t config[3];
    uint8_t leds[2];
    uint8_t status[2];
    if (!readRegisters(0x08, config, 3) || !readRegisters(0x0C, leds, 2) ||
        !readRegisters(0x00, status, 2)) {
      fail("CONFIG_READ_FAILED");
      return;
    }
    if (config[0] != FIFO_CONFIG || config[1] != 0x03 || config[2] != SPO2_CONFIG ||
        leds[0] != ledCode_ || leds[1] != ledCode_) {
      fail("CONFIG_MISMATCH");
      return;
    }
    connected_ = true;
    lastSampleMs_ = millis();
    lastError_ = "NONE";
    emit("EVENT,SENSOR_CONNECTED,part_id=0x15,chip_family=MAX30102_or_MAX30105,fifo_hz=25");
  }

  void collect() {
    uint8_t status[2];
    uint8_t pointers[3];
    uint8_t mode = 0;
    if (!readRegisters(0x00, status, 2) || !readRegisters(0x04, pointers, 3) ||
        !readRegisters(0x09, &mode, 1)) {
      fail("I2C_READ_FAILED");
      return;
    }
    if (mode != 0x03 || (status[0] & 0x01)) {
      fail("SENSOR_RESET_OR_MODE_CHANGED");
      return;
    }
    if ((status[0] & 0xA0) || (pointers[1] & 0x1F)) {
      // A_FULL, ambient-light overflow, or lost FIFO samples invalidate a window.
      fail("FIFO_OR_AMBIENT_OVERFLOW");
      return;
    }
    uint8_t pending = (uint8_t)((pointers[0] - pointers[2]) & 0x1F);
    if (pending > 16) {
      fail("FIFO_BACKLOG");
      return;
    }
    if (pending > 8)
      pending = 8; // At most 48 bytes per poll, below Teensy Wire's limit.
    if (pending != 0) {
      uint8_t bytes[48];
      if (!readRegisters(0x07, bytes, pending * 6U)) {
        fail("FIFO_SHORT_READ");
        return;
      }
      for (uint8_t i = 0; i < pending; ++i) {
        const uint8_t *p = bytes + i * 6U;
        latestRed_ = (((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2]) & ADC_MAX;
        latestIr_ = (((uint32_t)p[3] << 16) | ((uint32_t)p[4] << 8) | p[5]) & ADC_MAX;
        haveSample_ = true;
        lastSampleMs_ = millis();
        ++samples_;
        addSample();
      }
    }
    if ((uint32_t)(millis() - lastSampleMs_) >= STALE_MS)
      fail("NO_NEW_SAMPLES");
  }

  void addSample() {
    rail_ = latestRed_ == 0 || latestIr_ == 0 || latestRed_ == ADC_MAX || latestIr_ == ADC_MAX;
    if (outputMode_ != OutputMode::Data && rawCount_ < 8) {
      raw_[rawCount_++] = {samples_, lastSampleMs_, latestRed_, latestIr_};
    }
    if (calibration_.active()) {
      reportCaptureEvent(calibration_.push(latestRed_, latestIr_, lastSampleMs_));
      // Calibration captures raw light, not physiological estimates.
      optical_.reset();
      resetPulseQuality();
      return;
    }
    // Both low light and ADC rails invalidate the window on the first sample.
    // Re-entry requires stable contact and a completely new 200-sample window.
    if (!optical_.push(latestRed_, latestIr_, lastSampleMs_)) {
      if (!optical_.contactReady())
        resetPulseQuality();
      return;
    }
    const uint32_t calculationStartedUs = micros();
    int8_t legacyHrValid = 0;
    int8_t spo2Valid = 0;
    int32_t legacyHr = 0;
    int32_t spo2 = 0;
    metrics_ =
        saver_max_bench::measureWindow(optical_.redData(), optical_.irData(), WINDOW_SAMPLES);
    metricsSeq_ = samples_;
    metricsPending_ = true;
    pulseQuality_ = saver_max_bench::inspectPulse(optical_.redData(), optical_.irData(),
                                                WINDOW_SAMPLES);
    pulseConfirmed_ = confirmation_.accept(pulseQuality_);
    bool spo2Accepted = false;
    spo2Reason_ = pulseQuality_.accepted ? "CONFIRMING" : "SIGNAL_REJECTED";
    if (pulseConfirmed_) {
      uint32_t legacyRed[BUFFER_SIZE] = {}, legacyIr[BUFFER_SIZE] = {};
      const unsigned shift = saver_max_bench::prepareLegacyWindow(
          optical_.redData(), optical_.irData(), legacyRed, legacyIr, BUFFER_SIZE);
      if (shift < 32) {
        maxim_heart_rate_and_oxygen_saturation(legacyIr, BUFFER_SIZE, legacyRed, &spo2,
                                             &spo2Valid, &legacyHr, &legacyHrValid);
        const bool periodMatches = legacyHrValid &&
            saver_max_bench::legacyPeriodAgrees(legacyHr, pulseQuality_);
        spo2Accepted = periodMatches && spo2Valid && spo2 >= 0 && spo2 <= 100;
        spo2Reason_ = !periodMatches ? "LEGACY_PERIOD_MISMATCH"
                                    : spo2Accepted ? "ALGORITHM_ONLY" : "ALGORITHM_REJECTED";
      } else {
        spo2Reason_ = "ARITHMETIC_REJECTED";
      }
    }
    // No old-value fallback, HR clamp, or calibration offset. Both are unvalidated
    // bench estimates. Rejecting one window immediately revokes its old numbers.
    optical_.completeCalculation(pulseQuality_.hrBpm, pulseConfirmed_, spo2, spo2Accepted);
    lastCalculationUs_ = uint32_t(micros() - calculationStartedUs);
    lastEstimateMs_ = lastSampleMs_;
  }

  void reportCaptureEvent(saver_max_bench::CaptureEvent event) {
    using saver_max_bench::CaptureEvent;
    if (event == CaptureEvent::None)
      return;
    // Defer USB output until after FIFO processing; at most one event per capture.
    captureEvent_ = event;
  }

  void flushDiagnostics() {
    using saver_max_bench::CaptureEvent;
    if (captureEvent_ != CaptureEvent::None) {
      if (captureEvent_ == CaptureEvent::Complete) {
        emit("EVENT,CAPTURE_COMPLETE,see_CAL_SHOW,accuracy_verified=0");
        printCalibration();
      } else {
        emit(captureEvent_ == CaptureEvent::Gap ? "ERROR,code=CAL_SAMPLE_GAP,capture_cancelled=1"
                                                : "ERROR,code=CAL_TIMEOUT,capture_cancelled=1");
      }
      captureEvent_ = CaptureEvent::None;
    }
    char line[512];
    for (uint8_t n = 0; n < rawCount_; ++n) {
      const RawSample &r = raw_[n];
      if (outputMode_ == OutputMode::Raw) {
        snprintf(line, sizeof line, "RAW,seq=%lu,rx_ms=%lu,red_raw=%lu,ir_raw=%lu,adc_rail=%u",
                 (unsigned long)r.seq, (unsigned long)r.ms, (unsigned long)r.red,
                 (unsigned long)r.ir,
                 (r.red == 0 || r.ir == 0 || r.red == ADC_MAX || r.ir == ADC_MAX) ? 1U : 0U);
        emit(line);
      } else if (outputMode_ == OutputMode::Plot) {
        if (!plotInitialized_) {
          plotRed_ = r.red;
          plotIr_ = r.ir;
          plotInitialized_ = true;
        }
        // Display-only DC removal, nominal ~1 s baseline; estimator input is unchanged.
        plotRed_ += (double(r.red) - plotRed_) / 25.0;
        plotIr_ += (double(r.ir) - plotIr_) / 25.0;
        snprintf(line, sizeof line, "red_ac:%.1f\tir_ac:%.1f", double(r.red) - plotRed_,
                 double(r.ir) - plotIr_);
        emit(line);
      }
    }
    rawCount_ = 0;
    if (!metricsPending_)
      return;
    metricsPending_ = false;
    if (outputMode_ == OutputMode::Plot)
      return;
    const auto &m = metrics_;
    int n = snprintf(line, sizeof line,
                     "WINDOW,seq_end=%lu,samples=%u,red_dc=%.1f,ir_dc=%.1f,red_pp=%lu,ir_pp=%lu,"
                     "red_ac_rms=%.2f,ir_ac_rms=%.2f,corr_ok=%u,quality_verified=0",
                     (unsigned long)metricsSeq_, unsigned(WINDOW_SAMPLES), m.redMean, m.irMean,
                     (unsigned long)m.redPp,
                     (unsigned long)m.irPp, sqrt(m.redVariance), sqrt(m.irVariance),
                     m.correlationOk ? 1U : 0U);
    if (m.correlationOk && n > 0 && size_t(n) < sizeof line)
      snprintf(line + n, sizeof line - size_t(n), ",red_ir_corr=%.4f",
               m.covariance / sqrt(m.redVariance * m.irVariance));
    emit(line);
    n = snprintf(line, sizeof line,
                 "PULSE,seq_end=%lu,signal_ok=%u,reason=%s,confirmation=%u,calc_us=%lu,"
                 "quality_verified=0,spo2_reason=%s",
                 (unsigned long)metricsSeq_, pulseConfirmed_ ? 1U : 0U, pulseReason(),
                 confirmation_.count(), (unsigned long)lastCalculationUs_, spo2Reason_);
    if (pulseQuality_.ir.found && pulseQuality_.red.found)
      n += snprintf(line + n, sizeof line - size_t(n),
                    ",ir_period_samples=%.2f,red_period_samples=%.2f,ir_period_corr2=%.3f",
                    pulseQuality_.ir.samples, pulseQuality_.red.samples, pulseQuality_.ir.corr2);
    if (pulseQuality_.channels.available)
      snprintf(line + n, sizeof line - size_t(n), ",channel_corr2_signed=%.3f",
               pulseQuality_.channels.squaredSigned);
    emit(line);
  }

  void resetCalibration() {
    if (calibration_.active())
      emit("EVENT,CAPTURE_CANCELLED");
    calibration_.reset();
    calibrationApplied_ = false;
    captureEvent_ = saver_max_bench::CaptureEvent::None;
    optical_.configureContact(saver_max_bench::CONTACT_ON_RAW, saver_max_bench::CONTACT_OFF_RAW);
  }

  void printCapture(const char *phase, const saver_max_bench::CaptureStats &s) {
    char line[300];
    snprintf(line, sizeof line, "CAL_%s,complete=%u,samples=%u", phase, s.complete ? 1U : 0U,
             unsigned(s.red.count));
    emit(line);
    // No fabricated zero-valued measurements for a missing capture.
    if (!s.red.count)
      return;
    snprintf(line, sizeof line,
             "CAL_%s_STATS,red_mean=%.1f,red_min=%lu,red_max=%lu,ir_mean=%.1f,ir_min=%lu,ir_max=%"
             "lu,red_zero=%u,ir_zero=%u,red_high=%u,ir_high=%u",
             phase, s.red.mean(), (unsigned long)s.red.minimum, (unsigned long)s.red.maximum,
             s.ir.mean(), (unsigned long)s.ir.minimum, (unsigned long)s.ir.maximum,
             unsigned(s.red.zeroCount), unsigned(s.ir.zeroCount), unsigned(s.red.highCount),
             unsigned(s.ir.highCount));
    emit(line);
  }

  void printCalibration() {
    char line[240];
    snprintf(line, sizeof line,
             "CAL,active=%u,proposal_ok=%u,applied=%u,led_code=%u,on_raw=%lu,off_raw=%lu,ram_only="
             "1,accuracy_verified=0",
             calibration_.active() ? 1U : 0U, calibration_.proposalOk() ? 1U : 0U,
             calibrationApplied_ ? 1U : 0U, unsigned(ledCode_), (unsigned long)optical_.onRaw(),
             (unsigned long)optical_.offRaw());
    emit(line);
    printCapture("OPEN", calibration_.open());
    printCapture("FINGER", calibration_.finger());
    if (calibration_.proposalOk()) {
      snprintf(line, sizeof line, "CAL_PROPOSAL,on_raw=%lu,off_raw=%lu,apply_manually=CAL_APPLY",
               (unsigned long)calibration_.proposedOn(), (unsigned long)calibration_.proposedOff());
      emit(line);
    } else if (calibration_.open().complete && calibration_.finger().complete) {
      emit("CAL_REJECTED,reason=OVERLAP_OR_RAIL,optical_thresholds_not_applied");
    }
  }

  void printData() {
    optical_.expire(millis());
    if (!optical_.contactReady())
      resetPulseQuality();
    const bool fresh = connected_ && haveSample_ && (uint32_t)(millis() - lastSampleMs_) < STALE_MS;
    char line[512];
    int n = snprintf(line, sizeof line,
                     "DATA,ms=%lu,sensor_ok=%u,raw_ok=%u,contact=%s,hr_ok=%u,spo2_ok=%u,window_"
                     "samples=%u,quality_verified=0,signal_ok=%u,signal_reason=%s,spo2_reason=%s",
                     (unsigned long)millis(), connected_ ? 1U : 0U, fresh ? 1U : 0U,
                     optical_.contactName(), fresh && optical_.hrOk() ? 1U : 0U,
                     fresh && optical_.spo2Ok() ? 1U : 0U, (unsigned)optical_.count(),
                     fresh && optical_.hrOk() ? 1U : 0U, pulseReason(), spo2Reason_);
    if (fresh)
      n += snprintf(line + n, sizeof line - n, ",red_raw=%lu,ir_raw=%lu,adc_rail=%u",
                    (unsigned long)latestRed_, (unsigned long)latestIr_, rail_ ? 1U : 0U);
    if (fresh && optical_.hrOk())
      n += snprintf(line + n, sizeof line - n, ",hr_bpm=%ld", (long)optical_.hr());
    if (fresh && optical_.spo2Ok())
      n += snprintf(line + n, sizeof line - n, ",spo2_pct=%ld", (long)optical_.spo2());
    if (fresh && (optical_.hrOk() || optical_.spo2Ok()))
      snprintf(line + n, sizeof line - n, ",estimate_age_ms=%lu",
               (unsigned long)(millis() - lastEstimateMs_));
    emit(line);
  }

  void printStatus() {
    optical_.expire(millis());
    char line[240];
    snprintf(line, sizeof line,
             "STATUS,enabled=%u,connected=%u,contact=%s,last_part_id=0x%02X,samples=%lu,errors=%lu,"
             "dropped_logs=%lu,last_error=%s",
             enabled_ ? 1U : 0U, connected_ ? 1U : 0U, optical_.contactName(), (unsigned)partId_,
             (unsigned long)samples_, (unsigned long)errors_, (unsigned long)droppedLogs_,
             lastError_);
    emit(line);
    printCalibration();
  }

  void help() {
    emit("COMMANDS,HELP|STATUS|READ|START|STOP|RETRY|LED 1..255|MODE DATA/RAW/PLOT");
    emit("COMMANDS,CAL OPEN|CAL FINGER|CAL_SHOW|CAL_APPLY|CAL_RESET");
    emit("NOTE,CAL_captures_250_new_samples_about_10s_each,RAM_only,not_medical_calibration");
    emit("NOTE,READ_reports_latest_sample,START_STOP_control_optical_sensor_only");
    emit("NOTE,contact_settling_then_200_new_samples_about_8s_plus_confirmation,invalid_values_omitted");
    emit("NOTE,signal_ok_is_bench_repeatability_ONLY,quality_verified_and_accuracy_verified_remain_0");
    emit("NOTE,contact_CANDIDATE_is_optical_only_NOT_verified_skin_or_valid_pulse");
  }

  void command() {
    char *save = nullptr;
    char *name = strtok_r(commandLine_.text(), " ", &save);
    char *arg = strtok_r(nullptr, " ", &save);
    if (!name)
      return;
    if (strtok_r(nullptr, " ", &save)) {
      emit("ERROR,code=EXTRA_ARGUMENTS");
      return;
    }
    const bool needsArg = !strcmp(name, "LED") || !strcmp(name, "MODE") || !strcmp(name, "CAL");
    if (bool(arg) != needsArg) {
      emit("ERROR,code=ARGUMENT_COUNT");
      return;
    }
    if (strcmp(name, "HELP") == 0)
      help();
    else if (strcmp(name, "STATUS") == 0)
      printStatus();
    else if (strcmp(name, "READ") == 0)
      printData();
    else if (strcmp(name, "CAL_SHOW") == 0)
      printCalibration();
    else if (strcmp(name, "CAL_RESET") == 0) {
      resetCalibration();
      clearSamples();
      emit("OK,CAL_RESET,defaults_restored=1");
    } else if (strcmp(name, "CAL_APPLY") == 0) {
      if (!calibration_.proposalOk()) {
        emit("ERROR,code=NO_VALID_CAL_PROPOSAL");
        return;
      }
      optical_.configureContact(calibration_.proposedOn(), calibration_.proposedOff());
      calibrationApplied_ = true;
      clearSamples();
      emit("OK,OPTICAL_THRESHOLDS_APPLIED,ram_only=1,accuracy_verified=0");
      printCalibration();
    } else if (strcmp(name, "CAL") == 0) {
      using saver_max_bench::CapturePhase;
      CapturePhase phase = !strcmp(arg, "OPEN")     ? CapturePhase::Open
                           : !strcmp(arg, "FINGER") ? CapturePhase::Finger
                                                    : CapturePhase::None;
      if (!connected_ || !enabled_) {
        emit("ERROR,code=CAL_REQUIRES_CONNECTED_SENSOR");
        return;
      }
      if (phase == CapturePhase::None) {
        emit("ERROR,code=USE_CAL_OPEN_OR_FINGER");
        return;
      }
      if (phase == CapturePhase::Finger && !calibration_.open().complete) {
        emit("ERROR,code=COMPLETE_CAL_OPEN_FIRST");
        return;
      }
      if (!calibration_.start(phase, millis())) {
        emit("ERROR,code=CAL_ALREADY_ACTIVE");
        return;
      }
      // Start discards old estimates. Capture only FIFO samples collected later.
      calibrationApplied_ = false;
      optical_.configureContact(saver_max_bench::CONTACT_ON_RAW, saver_max_bench::CONTACT_OFF_RAW);
      clearSamples();
      outputMode_ = OutputMode::Data;
      emit(phase == CapturePhase::Open ? "OK,CAL_OPEN_STARTED,keep_sensor_uncovered,samples=250"
                                       : "OK,CAL_FINGER_STARTED,keep_finger_still,samples=250");
    } else if (strcmp(name, "LED") == 0) {
      uint8_t code = 0;
      if (!saver_max_bench::parseLedCode(arg, code)) {
        emit("ERROR,code=LED_REQUIRES_INTEGER_1_TO_255");
        return;
      }
      ledCode_ = code;
      resetCalibration();
      clearSamples();
      // Full reconfiguration prevents mixing FIFO samples from different LED settings.
      if (connected_ && !writeRegister(0x09, 0x80))
        fail("SHUTDOWN_FAILED");
      connected_ = false;
      lastAttemptMs_ = millis() - RETRY_PERIOD_MS;
      emit("OK,LED_CODE_SET,calibration_cleared=1,configuration_pending=1");
      printCalibration();
    } else if (strcmp(name, "MODE") == 0) {
      if (!strcmp(arg, "DATA"))
        outputMode_ = OutputMode::Data;
      else if (!strcmp(arg, "RAW"))
        outputMode_ = OutputMode::Raw;
      else if (!strcmp(arg, "PLOT")) {
        if (calibration_.active()) {
          emit("ERROR,code=WAIT_FOR_CAL_COMPLETION");
          return;
        }
        outputMode_ = OutputMode::Plot;
      } else {
        emit("ERROR,code=USE_MODE_DATA_RAW_OR_PLOT");
        return;
      }
      rawCount_ = 0;
      plotInitialized_ = false;
      emit("OK,OUTPUT_MODE_SET,RAW_is_25Hz_rx_timestamps,PLOT_is_display_only_AC");
    } else if (strcmp(name, "STOP") == 0) {
      enabled_ = false;
      if (calibration_.active())
        emit("EVENT,CAPTURE_CANCELLED");
      calibration_.cancel();
      if (connected_ && !writeRegister(0x09, 0x80))
        fail("SHUTDOWN_FAILED");
      connected_ = false;
      clearSamples();
      emit("OK,ACQUISITION_STOPPED,LED_shutdown_requires_successful_I2C");
    } else if (strcmp(name, "START") == 0 || strcmp(name, "RETRY") == 0) {
      enabled_ = true;
      connected_ = false;
      resetCalibration();
      clearSamples();
      lastAttemptMs_ = millis() - RETRY_PERIOD_MS;
      emit("OK,SENSOR_RECONNECT_REQUESTED");
    } else
      emit("ERROR,code=UNKNOWN_COMMAND");
  }

  void pollCommands() {
    using saver_max_bench::LineEvent;
    if (commandLine_.expire(millis()) == LineEvent::Timeout) {
      emit("ERROR,code=COMMAND_TIMEOUT");
    }
    for (uint8_t budget = 0; budget < 32 && Serial.available() > 0; ++budget) {
      const LineEvent event = commandLine_.push(char(Serial.read()), millis());
      if (event == LineEvent::Invalid) {
        emit("ERROR,code=INVALID_OR_TOO_LONG_COMMAND");
        return;
      }
      if (event == LineEvent::Ready) {
        command();
        return;
      }
    }
  }

  bool enabled_ = true, connected_ = false, haveSample_ = false;
  bool rail_ = false, usbConnected_ = false, calibrationApplied_ = false;
  uint8_t partId_ = 0, lastI2cStatus_ = 0, ledCode_ = LED_AMPLITUDE;
  saver_max_bench::OpticalWindow optical_;
  saver_max_bench::OpticalCalibration calibration_;
  saver_max_bench::CommandLine commandLine_;
  saver_max_bench::CaptureEvent captureEvent_ = saver_max_bench::CaptureEvent::None;
  saver_max_bench::WindowMetrics metrics_;
  saver_max_bench::PulseQuality pulseQuality_;
  saver_max_bench::PulseConfirmation confirmation_;
  bool pulseConfirmed_ = false;
  const char *spo2Reason_ = "WAITING_WINDOW";
  bool metricsPending_ = false, plotInitialized_ = false;
  double plotRed_ = 0, plotIr_ = 0;
  uint32_t metricsSeq_ = 0, lastEstimateMs_ = 0, lastCalculationUs_ = 0;
  enum class OutputMode : uint8_t { Data, Raw, Plot };
  OutputMode outputMode_ = OutputMode::Data;
  struct RawSample {
    uint32_t seq, ms, red, ir;
  };
  RawSample raw_[8] = {};
  uint8_t rawCount_ = 0;
  uint32_t latestIr_ = 0, latestRed_ = 0;
  uint32_t lastAttemptMs_ = 0, lastPollMs_ = 0, lastPrintMs_ = 0, lastSampleMs_ = 0;
  uint32_t samples_ = 0, errors_ = 0, droppedLogs_ = 0;
  const char *lastError_ = "NOT_STARTED";
};

Max30102Bench &bench() {
  static Max30102Bench instance;
  return instance;
}
} // namespace

void setup() {
  inhibitVacuumOutputs();
  Serial.begin(115200);
  const uint32_t started = millis();
  while (!Serial && (uint32_t)(millis() - started) < 2000U) {
  }
  bench().begin();
}

void loop() { bench().tick(); }
