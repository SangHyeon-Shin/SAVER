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
 * We use its Maxim SpO2 algorithm but check I2C transaction lengths ourselves
 * so a failed FIFO read cannot be converted into plausible sensor numbers.
 * Settings follow SparkFun Example8: 100 Hz / averaging 4 = 25 FIFO samples/s,
 * 411 us, ADC range 4096 nA, red + IR. 100 samples = approximately 4 seconds.
 *
 * USB Serial 115200, newline: HELP, STATUS, READ, START, STOP, RETRY.
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
#include <stdio.h>
#include <string.h>
#include "pinmap.h"
#include "optical_window.h"

namespace {
constexpr uint8_t SENSOR_ADDRESS = 0x57;
constexpr uint8_t EXPECTED_PART_ID = 0x15;  // Shared by MAX30102 and MAX30105.
constexpr uint32_t I2C_CLOCK_HZ = 100000;   // Conservative breadboard bus rate.
constexpr uint8_t LED_AMPLITUDE = 60;      // EDIT brightness here: 1..255, not percent.
constexpr uint32_t PRINT_PERIOD_MS = 100;  // EDIT log period: 100 ms = 10 Hz.
constexpr uint32_t POLL_PERIOD_MS = 10;
constexpr uint32_t RETRY_PERIOD_MS = 2000;
constexpr uint32_t STALE_MS = 1000;
constexpr uint16_t WINDOW_SAMPLES = saver_max_bench::WINDOW_SAMPLES;
constexpr uint32_t ADC_MAX = saver_max_bench::ADC_MAX;
constexpr uint8_t FIFO_CONFIG = 0x4F;  // Average 4, no rollover, almost-full at 17.
constexpr uint8_t SPO2_CONFIG = 0x27;  // 4096 nA, 100 Hz, 411 us (18 bits).
static_assert(FreqS == 25 && BUFFER_SIZE == WINDOW_SAMPLES,
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
    emit("SAVER_MAX30102_SENSOR_TEST_READY,build=contact_gate_v1");
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
    help();
  }

  void tick() {
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
    optical_.expire(millis());
    pollCommands();
    if (enabled_ && (uint32_t)(millis() - lastPrintMs_) >= PRINT_PERIOD_MS) {
      lastPrintMs_ = millis();
      printData();
    }
  }

 private:
  void emit(const char* line) {
    const size_t length = strlen(line);
    // Dropping a log must not block FIFO service. No heap or endless USB waits.
    if (!Serial || Serial.availableForWrite() < (int)(length + 1U)) {
      ++droppedLogs_;
      return;
    }
    Serial.write((const uint8_t*)line, length);
    Serial.write((uint8_t)'\n');
  }

  bool readRegisters(uint8_t reg, uint8_t* bytes, uint8_t count) {
    // Teensyduino 1.62 Wire bounds bus-idle/transfer waits internally (~16/50 ms).
    // This sketch has no unbounded wait for bytes and checks every short read.
    Wire.beginTransmission(SENSOR_ADDRESS);
    Wire.write(reg);
    lastI2cStatus_ = Wire.endTransmission(false);
    if (lastI2cStatus_ != 0) return false;
    const uint8_t received = Wire.requestFrom(SENSOR_ADDRESS, count);
    if (received != count || Wire.available() != count) {
      lastI2cStatus_ = 6;  // Local diagnostic: short read, not a Wire status code.
      return false;
    }
    for (uint8_t i = 0; i < count; ++i) bytes[i] = (uint8_t)Wire.read();
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
  }

  void fail(const char* reason) {
    connected_ = false;
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
    if (!readRegisters(0xFF, &partId_, 1)) { fail("I2C_NO_SENSOR"); return; }
    if (partId_ != EXPECTED_PART_ID) { fail("UNSUPPORTED_PART_ID"); return; }
    if (!writeRegister(0x09, 0x40)) { fail("RESET_WRITE_FAILED"); return; }
    const uint32_t resetStarted = millis();
    uint8_t mode = 0x40;
    do {
      if (!readRegisters(0x09, &mode, 1)) { fail("RESET_READ_FAILED"); return; }
      if ((mode & 0x40) == 0) break;
      delay(1);
    } while ((uint32_t)(millis() - resetStarted) < 150U);
    if (mode & 0x40) { fail("RESET_TIMEOUT"); return; }

    // Stay shut down while configuring; red then IR is the SpO2 FIFO order.
    if (!writeRegister(0x09, 0x80) || !writeRegister(0x02, 0x80) ||
        !writeRegister(0x03, 0x00) || !writeRegister(0x04, 0x00) ||
        !writeRegister(0x05, 0x00) || !writeRegister(0x06, 0x00) ||
        !writeRegister(0x08, FIFO_CONFIG) || !writeRegister(0x0A, SPO2_CONFIG) ||
        !writeRegister(0x0C, LED_AMPLITUDE) || !writeRegister(0x0D, LED_AMPLITUDE) ||
        !writeRegister(0x09, 0x03)) { fail("CONFIG_WRITE_FAILED"); return; }
    uint8_t config[3];
    uint8_t leds[2];
    uint8_t status[2];
    if (!readRegisters(0x08, config, 3) || !readRegisters(0x0C, leds, 2) ||
        !readRegisters(0x00, status, 2)) { fail("CONFIG_READ_FAILED"); return; }
    if (config[0] != FIFO_CONFIG || config[1] != 0x03 || config[2] != SPO2_CONFIG ||
        leds[0] != LED_AMPLITUDE || leds[1] != LED_AMPLITUDE) {
      fail("CONFIG_MISMATCH"); return;
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
        !readRegisters(0x09, &mode, 1)) { fail("I2C_READ_FAILED"); return; }
    if (mode != 0x03 || (status[0] & 0x01)) { fail("SENSOR_RESET_OR_MODE_CHANGED"); return; }
    if ((status[0] & 0xA0) || (pointers[1] & 0x1F)) {
      // A_FULL, ambient-light overflow, or lost FIFO samples invalidate a window.
      fail("FIFO_OR_AMBIENT_OVERFLOW"); return;
    }
    uint8_t pending = (uint8_t)((pointers[0] - pointers[2]) & 0x1F);
    if (pending > 16) { fail("FIFO_BACKLOG"); return; }
    if (pending > 8) pending = 8;  // At most 48 bytes per poll, below Teensy Wire's limit.
    if (pending != 0) {
      uint8_t bytes[48];
      if (!readRegisters(0x07, bytes, pending * 6U)) { fail("FIFO_SHORT_READ"); return; }
      for (uint8_t i = 0; i < pending; ++i) {
        const uint8_t* p = bytes + i * 6U;
        latestRed_ = (((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2]) & ADC_MAX;
        latestIr_ = (((uint32_t)p[3] << 16) | ((uint32_t)p[4] << 8) | p[5]) & ADC_MAX;
        haveSample_ = true;
        lastSampleMs_ = millis();
        ++samples_;
        addSample();
      }
    }
    if ((uint32_t)(millis() - lastSampleMs_) >= STALE_MS) fail("NO_NEW_SAMPLES");
  }

  void addSample() {
    rail_ = latestRed_ == 0 || latestIr_ == 0 || latestRed_ == ADC_MAX || latestIr_ == ADC_MAX;
    // Both low light and ADC rails invalidate the window on the first sample.
    // Re-entry requires stable contact and a completely new 100-sample window.
    if (!optical_.push(latestRed_, latestIr_, lastSampleMs_)) return;
    int8_t hrValid = 0;
    int8_t spo2Valid = 0;
    int32_t hr = 0;
    int32_t spo2 = 0;
    maxim_heart_rate_and_oxygen_saturation(optical_.irData(), WINDOW_SAMPLES, optical_.redData(),
                                         &spo2, &spo2Valid, &hr, &hrValid);
    optical_.completeCalculation(hr, hrValid != 0, spo2, spo2Valid != 0);
  }

  void printData() {
    optical_.expire(millis());
    const bool fresh = connected_ && haveSample_ &&
                       (uint32_t)(millis() - lastSampleMs_) < STALE_MS;
    char line[320];
    int n = snprintf(line, sizeof line,
        "DATA,ms=%lu,sensor_ok=%u,raw_ok=%u,contact=%s,hr_ok=%u,spo2_ok=%u,window_samples=%u",
        (unsigned long)millis(), connected_ ? 1U : 0U, fresh ? 1U : 0U,
        optical_.contactName(), fresh && optical_.hrOk() ? 1U : 0U,
        fresh && optical_.spo2Ok() ? 1U : 0U, (unsigned)optical_.count());
    if (fresh) n += snprintf(line + n, sizeof line - n, ",red_raw=%lu,ir_raw=%lu,adc_rail=%u",
        (unsigned long)latestRed_, (unsigned long)latestIr_, rail_ ? 1U : 0U);
    if (fresh && optical_.hrOk()) n += snprintf(line + n, sizeof line - n, ",hr_bpm=%ld", (long)optical_.hr());
    if (fresh && optical_.spo2Ok()) snprintf(line + n, sizeof line - n, ",spo2_pct=%ld", (long)optical_.spo2());
    emit(line);
  }

  void printStatus() {
    optical_.expire(millis());
    char line[240];
    snprintf(line, sizeof line,
        "STATUS,enabled=%u,connected=%u,contact=%s,last_part_id=0x%02X,samples=%lu,errors=%lu,dropped_logs=%lu,last_error=%s",
        enabled_ ? 1U : 0U, connected_ ? 1U : 0U, optical_.contactName(), (unsigned)partId_,
        (unsigned long)samples_, (unsigned long)errors_, (unsigned long)droppedLogs_, lastError_);
    emit(line);
  }

  void help() {
    emit("COMMANDS,HELP|STATUS|READ|START|STOP|RETRY");
    emit("NOTE,READ_reports_latest_sample,START_STOP_control_optical_sensor_only");
    emit("NOTE,contact_settling_then_100_new_samples_about_4s,invalid_values_omitted");
    emit("NOTE,contact_CANDIDATE_is_optical_only_NOT_verified_skin_or_valid_pulse");
  }

  void command() {
    command_[commandLength_] = '\0';
    for (size_t i = 0; i < commandLength_; ++i) command_[i] = (char)toupper((unsigned char)command_[i]);
    if (strcmp(command_, "HELP") == 0) help();
    else if (strcmp(command_, "STATUS") == 0) printStatus();
    else if (strcmp(command_, "READ") == 0) printData();
    else if (strcmp(command_, "STOP") == 0) {
      enabled_ = false;
      if (connected_ && !writeRegister(0x09, 0x80)) fail("SHUTDOWN_FAILED");
      connected_ = false;
      clearSamples();
      emit("OK,ACQUISITION_STOPPED,LED_shutdown_requires_successful_I2C");
    } else if (strcmp(command_, "START") == 0 || strcmp(command_, "RETRY") == 0) {
      enabled_ = true;
      connected_ = false;
      clearSamples();
      lastAttemptMs_ = millis() - RETRY_PERIOD_MS;
      emit("OK,SENSOR_RECONNECT_REQUESTED");
    } else emit("ERROR,code=UNKNOWN_COMMAND");
  }

  void pollCommands() {
    if ((commandLength_ != 0 || badCommand_) &&
        (uint32_t)(millis() - lastCharMs_) >= 1000U) {
      commandLength_ = 0;
      badCommand_ = false;
      emit("ERROR,code=COMMAND_TIMEOUT");
    }
    for (uint8_t budget = 0; budget < 32 && Serial.available() > 0; ++budget) {
      const char c = (char)Serial.read();
      lastCharMs_ = millis();
      if (c == '\r') continue;
      if (c == '\n') {
        if (badCommand_) emit("ERROR,code=INVALID_OR_TOO_LONG_COMMAND");
        else if (commandLength_ != 0) command();
        commandLength_ = 0;
        badCommand_ = false;
        return;
      }
      if (c < 32 || c > 126 || commandLength_ >= sizeof command_ - 1U) badCommand_ = true;
      else if (!badCommand_) command_[commandLength_++] = c;
    }
  }

  bool enabled_ = true, connected_ = false, haveSample_ = false;
  bool rail_ = false, badCommand_ = false;
  uint8_t partId_ = 0, lastI2cStatus_ = 0;
  saver_max_bench::OpticalWindow optical_;
  uint32_t latestIr_ = 0, latestRed_ = 0;
  uint32_t lastAttemptMs_ = 0, lastPollMs_ = 0, lastPrintMs_ = 0, lastSampleMs_ = 0;
  uint32_t samples_ = 0, errors_ = 0, droppedLogs_ = 0, lastCharMs_ = 0;
  const char* lastError_ = "NOT_STARTED";
  char command_[32] = {};
  size_t commandLength_ = 0;
};

Max30102Bench& bench() {
  static Max30102Bench instance;
  return instance;
}
}  // namespace

void setup() {
  inhibitVacuumOutputs();
  Serial.begin(115200);
  const uint32_t started = millis();
  while (!Serial && (uint32_t)(millis() - started) < 2000U) {}
  bench().begin();
}

void loop() { bench().tick(); }
