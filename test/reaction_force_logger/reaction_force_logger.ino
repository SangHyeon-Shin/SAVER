/*
 * DYMH-103 (user: 0..100 kg) + NAU7802, Teensy 4.1 recording-only bench sketch.
 * NAU7802 CH1 / gain 128 / internal LDO 3.0 V / nominal 320 SPS; USB summary 20 Hz.
 * No motor GPIO, VESC UART, PID, auto motor start, or Jetson protocol changes.
 * DISCONNECT ALL MOTOR POWER before uploading: other motor EN pins are NOT initialized.
 * Calibration is RAM-only; no invented force scale. Read README.md before wiring.
 */
#include <Arduino.h>
#include <Wire.h>
#include <stdarg.h>
#include "logger_logic.h"
#include "nau7802_reader.h"
#include "sd_recorder.h"

#if !defined(ARDUINO_TEENSY41)
#error "This standalone logger requires Teensy 4.1 and its built-in SDIO slot."
#endif

namespace {
using namespace reaction;
constexpr uint8_t SDA_PIN = 18; // Default Wire SDA; existing I2C bus, NOT reserved A9.
constexpr uint8_t SCL_PIN = 19; // Default Wire SCL; no new GPIO allocation.
constexpr uint8_t NAU_ADDRESS = 0x2A;
constexpr uint32_t POLL_US = 250; // Poll readiness; never create a sample when CR=0.
constexpr uint16_t CAL_SAMPLES = 320;

class WireTransport {
public:
  bool write(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(NAU_ADDRESS);
    Wire.write(reg);
    Wire.write(value);
    return Wire.endTransmission() == 0;
  }
  bool read(uint8_t reg, uint8_t *bytes, size_t n) {
    Wire.beginTransmission(NAU_ADDRESS);
    Wire.write(reg);
    if (Wire.endTransmission() != 0)
      return false;
    const uint8_t got = Wire.requestFrom(NAU_ADDRESS, uint8_t(n));
    if (got != n || size_t(Wire.available()) != n) {
      while (Wire.available())
        Wire.read();
      return false;
    }
    for (size_t i = 0; i < n; ++i)
      bytes[i] = uint8_t(Wire.read());
    return true;
  }
};

class ForceLogger {
public:
  void begin() {
    Serial.begin(115200);
    initWire();
    reader_.begin(millis());
    lastSummary_ = clock_.update(micros());
  }
  void tick() {
    const uint64_t now = clock_.update(micros());
    const bool connected = bool(Serial);
    if (connected != usb_) {
      usb_ = connected;
      command_.reset();
      if (connected) {
        emit("SAVER_REACTION_FORCE_LOGGER_READY,build=nau320_csv_v1");
        help();
      } else {
        recorder_.stop("USB_DISCONNECTED");
        cancelCapture();
      }
    }
    pollSensor(now);
    pollCommands();
    if (captureMode_ && uint32_t(millis() - captureAt_) >= 3000) {
      cancelCapture();
      emit("ERROR,CAL_CAPTURE_TIMEOUT,force_ok=0");
    }
    const uint64_t reportNow = clock_.update(micros());
    if (reportNow - lastSummary_ >= SUMMARY_US) {
      summary(reportNow);
      lastSummary_ = reportNow;
      window_ = Window{};
    }
    recorder_.service();
    if (recorderWasBusy_ && !recorder_.busy()) {
      emit("EVENT,LOG_CLOSED,file=%s,sd_fault=%u,reason=%s,rows=%lu,unwritten_bytes=%lu",
           recorder_.filename(), recorder_.fault(), recorder_.reason(),
           (unsigned long)recorder_.rows(), (unsigned long)recorder_.lostBytes());
    }
    recorderWasBusy_ = recorder_.busy();
  }

private:
  WireTransport bus_;
  Nau7802Reader reader_;
  SdRecorder recorder_;
  Calibration cal_;
  Window window_, capture_;
  MicroClock clock_;
  CommandLine command_;
  bool usb_ = false, recorderWasBusy_ = false, utcKnown_ = false, haveSample_ = false;
  bool lastRail_ = false;
  uint64_t utcBaseUs_ = 0, utcEpoch_ = 0, lastSampleUs_ = 0, lastSummary_ = 0;
  uint32_t pollAt_ = 0, retryAt_ = 0, sequence_ = 0, i2cErrors_ = 0, gapEvents_ = 0;
  uint32_t maxGapUs_ = 0, droppedUsb_ = 0, captureAt_ = 0;
  uint8_t captureMode_ = 0; // 1=tare, 2=known kg.
  double captureKg_ = 0;
  int32_t tareMin_ = 0, tareMax_ = 0;

  void initWire() {
    Wire.begin();
    Wire.setSDA(SDA_PIN);
    Wire.setSCL(SCL_PIN);
    Wire.setClock(400000);
  }
  bool emit(const char *fmt, ...) {
    if (!usb_)
      return false;
    char line[600];
    va_list args;
    va_start(args, fmt);
    const int n = vsnprintf(line, sizeof(line) - 1, fmt, args);
    va_end(args);
    if (n < 0 || size_t(n) >= sizeof(line) - 1 || Serial.availableForWrite() < n + 1) {
      ++droppedUsb_;
      return false;
    }
    line[n] = '\n';
    Serial.write(reinterpret_cast<const uint8_t *>(line), n + 1);
    return true;
  }
  bool fresh(uint64_t now) const {
    return reader_.ready() && haveSample_ && now - lastSampleUs_ < 100000;
  }
  void utcText(uint64_t now, char *out, size_t size) const {
    const uint64_t elapsed = now - utcBaseUs_;
    const DateParts d = dateParts(utcEpoch_ + elapsed / 1000000);
    snprintf(out, size, "%04d-%02d-%02dT%02d:%02d:%02d.%06luZ", d.year, d.month, d.day, d.hour,
             d.minute, d.second, (unsigned long)(elapsed % 1000000));
  }
  void cancelCapture() {
    if (captureMode_) {
      cal_.reset();
      captureMode_ = 0;
      window_ = Window{};
    }
  }
  void pollSensor(uint64_t now) {
    if (reader_.failed()) {
      if (uint32_t(millis() - retryAt_) >= 2000) {
        Wire.end();
        initWire();
        reader_.begin(millis());
        emit("EVENT,SENSOR_RETRY,calibration_reset=1,log_auto_restart=0");
      }
      return;
    }
    if (uint32_t(uint32_t(now) - pollAt_) < POLL_US)
      return;
    pollAt_ = uint32_t(now);
    const bool wasReady = reader_.ready();
    int32_t raw = 0;
    const auto result = reader_.tick(bus_, millis(), raw);
    if (result == Nau7802Reader::Result::Error) {
      ++i2cErrors_;
      retryAt_ = millis();
      haveSample_ = false;
      cal_.reset();
      captureMode_ = 0;
      window_ = Window{};
      recorder_.stop("SENSOR_ERROR");
      emit("ERROR,%s,force_ok=0,calibration_reset=1", reader_.error());
      return;
    }
    if (!wasReady && reader_.ready())
      emit("EVENT,SENSOR_READY,sps_nominal=320,loadcell_presence_verified=0");
    if (result != Nau7802Reader::Result::Sample)
      return;
    const uint64_t readUs =
        clock_.update(micros()); // Read-completion time, NOT ADC conversion instant.
    const uint64_t gap = haveSample_ ? readUs - lastSampleUs_ : 0;
    const bool gapFlag = haveSample_ && gap > GAP_US;
    if (gapFlag)
      ++gapEvents_;
    if (gap > maxGapUs_)
      maxGapUs_ = gap > UINT32_MAX ? UINT32_MAX : uint32_t(gap);
    lastSampleUs_ = readUs;
    haveSample_ = true;
    lastRail_ = rail(raw);
    ++sequence_;
    window_.add(raw, cal_);
    if (captureMode_)
      captureSample(raw, gapFlag);
    if (!recorder_.active())
      return;
    char utc[48], force[40] = {}, line[224];
    utcText(readUs, utc, sizeof(utc));
    double forceN = 0;
    const bool ok = cal_.force(raw, forceN);
    if (ok)
      snprintf(force, sizeof(force), "%.6f", forceN);
    const int n = snprintf(line, sizeof(line), "%s,%llu,%lu,%ld,%u,%s,%u,%u,%llu\n", utc,
                           (unsigned long long)readUs, (unsigned long)sequence_, (long)raw, ok,
                           force, lastRail_, gapFlag, (unsigned long long)gap);
    if (n <= 0 || size_t(n) >= sizeof(line))
      recorder_.stop("ROW_FORMAT_ERROR");
    else
      recorder_.append(line, size_t(n));
  }
  void captureSample(int32_t raw, bool gap) {
    if (rail(raw) || gap) {
      cancelCapture();
      emit("ERROR,CAL_RAIL_OR_SAMPLE_GAP,force_ok=0");
      return;
    }
    capture_.add(raw, Calibration{});
    if (capture_.count < CAL_SAMPLES)
      return;
    const double mean = double(capture_.rawSum) / capture_.validCount;
    if (captureMode_ == 2 && !separated(tareMin_, tareMax_, capture_.rawMin, capture_.rawMax)) {
      captureMode_ = 0;
      emit("ERROR,CAL_SPAN_OVERLAPS_TARE,force_ok=0,check_known_load_and_noise=1");
      return;
    }
    const bool ok = captureMode_ == 1 ? cal_.tare(mean) : cal_.calibrate(mean, captureKg_);
    if (captureMode_ == 1 && ok) {
      tareMin_ = capture_.rawMin;
      tareMax_ = capture_.rawMax;
    }
    emit("CAL_RESULT,ok=%u,kind=%s,n=%lu,raw_mean=%.6f,raw_min=%ld,raw_max=%ld,accuracy_verified=0",
         ok, captureMode_ == 1 ? "TARE" : "KNOWN_KG", (unsigned long)capture_.count, mean,
         (long)capture_.rawMin, (long)capture_.rawMax);
    captureMode_ = 0;
    window_ = Window{};
    showCal();
  }
  void summary(uint64_t now) {
    const uint64_t elapsed = now - lastSummary_;
    char raw[120] = {}, force[140] = {};
    if (window_.validCount)
      snprintf(raw, sizeof(raw), ",raw_mean=%.2f,raw_min=%ld,raw_max=%ld",
               double(window_.rawSum) / window_.validCount, (long)window_.rawMin,
               (long)window_.rawMax);
    const bool forceOk = fresh(now) && window_.forceCount && cal_.confirmed;
    if (forceOk)
      snprintf(force, sizeof(force), ",force_mean_N=%.3f,force_min_N=%.3f,force_max_N=%.3f",
               window_.forceSum / window_.forceCount, window_.forceMin, window_.forceMax);
    emit("DATA,read_us=%llu,window_us=%llu,sensor_ok=%u,n=%lu,valid_n=%lu,rail_n=%lu,force_ok=%u,"
         "sd_logging=%u%s%s",
         (unsigned long long)now, (unsigned long long)elapsed, fresh(now),
         (unsigned long)window_.count, (unsigned long)window_.validCount,
         (unsigned long)window_.rails, forceOk, recorder_.active(), raw, force);
  }
  void help() {
    emit("PINS,SDA=18,SCL=19,I2C_ADDRESS=0x2A,SD=builtin_SDIO,MOTOR_CONTROL=NONE");
    emit("COMMANDS,STATUS|HELP|TARE|CAL kg|CAL_SHOW|CAL_RESET|TIME "
         "YYYY-MM-DDTHH:MM:SSZ|LOG_START|LOG_STOP");
    emit("NOTE,320_SPS_nominal_not_500Hz,force_requires_tare_and_known_mass,raw_logging_allowed");
  }
  void showCal() {
    emit("CAL,zero_known=%u,calibrated=%u,zero_raw=%.9f,counts_per_kg=%.9f,accuracy_verified=0,ram_"
         "only=1",
         cal_.zeroKnown, cal_.confirmed, cal_.zero, cal_.countsPerKg);
  }
  void status() {
    emit("STATUS,sensor_ok=%u,sensor_error=%s,utc_set=%u,sd_logging=%u,sd_closing=%u,sd_fault=%u,"
         "sd_reason=%s,file=%s,rows=%lu,queued_bytes=%u,buffer_high_bytes=%lu,max_write_us=%lu,"
         "unwritten_bytes=%lu,gap_events=%lu,max_gap_us=%lu,i2c_errors=%lu,dropped_usb=%lu,cal_"
         "capture=%u",
         fresh(clock_.update(micros())), reader_.error(), utcKnown_, recorder_.active(),
         recorder_.busy() && !recorder_.active(), recorder_.fault(), recorder_.reason(),
         recorder_.filename(), (unsigned long)recorder_.rows(), unsigned(recorder_.queued()),
         (unsigned long)recorder_.maxQueued(), (unsigned long)recorder_.maxWriteUs(),
         (unsigned long)recorder_.lostBytes(), (unsigned long)gapEvents_, (unsigned long)maxGapUs_,
         (unsigned long)i2cErrors_, (unsigned long)droppedUsb_, captureMode_);
    showCal();
  }
  void startLog() {
    const uint64_t now = clock_.update(micros());
    if (!utcKnown_) {
      emit("ERROR,SET_UTC_WITH_TIME_FIRST");
      return;
    }
    if (!fresh(now) || lastRail_) {
      emit("ERROR,SENSOR_NOT_READY_OR_RAIL");
      return;
    }
    char utc[48], header[1100];
    utcText(now, utc, sizeof(utc));
    snprintf(
        header, sizeof(header),
        "# SAVER_REACTION_CSV_V1,started_utc=%s,time_source=manual_anchor,accuracy_verified=0\n"
        "# "
        "sensor=DYMH-103,user_capacity_kg=100,adc=NAU7802,channel=1,gain=128,ldo_V=3.0,sps_nominal="
        "320\n"
        "# calibrated=%u,zero_raw=%.9f,counts_per_kg=%.9f,g_standard_m_s2=9.80665\n"
        "# "
        "read_us=MCU_read_completion_not_conversion_time,gap_flag=interval_gt_6250us,exact_ADC_"
        "losses_unknown\n"
        "# build=nau320_csv_v1,motor_control=none,uncalibrated_force_is_empty\n"
        "utc,read_us,seq,raw,force_ok,force_N,adc_rail,gap_flag,interval_us\n",
        utc, cal_.confirmed, cal_.zero, cal_.countsPerKg);
    if (!recorder_.start(header))
      emit("ERROR,%s", recorder_.reason());
    else {
      recorderWasBusy_ = true;
      emit("OK,LOG_STARTED,file=%s,force_ok=%u", recorder_.filename(), cal_.confirmed);
    }
  }
  void pollCommands() {
    if (command_.expire(millis()))
      emit("ERROR,COMMAND_TIMEOUT");
    for (unsigned i = 0; i < 64 && Serial.available(); ++i) {
      const auto result = command_.push(char(Serial.read()), millis());
      if (result == LineResult::Invalid)
        emit("ERROR,BAD_OR_LONG_COMMAND");
      if (result == LineResult::Ready) {
        handle(command_.text());
        command_.reset();
      }
    }
  }
  void handle(char *text) {
    char *ctx = nullptr;
    char *cmd = strtok_r(text, " ", &ctx), *a = strtok_r(nullptr, " ", &ctx);
    char *extra = strtok_r(nullptr, " ", &ctx);
    if (!cmd)
      return;
    if (!strcmp(cmd, "STATUS") && !a) {
      status();
      return;
    }
    if (!strcmp(cmd, "HELP") && !a) {
      help();
      return;
    }
    if ((!strcmp(cmd, "LOG_STOP") || !strcmp(cmd, "STOP")) && !a) {
      cancelCapture();
      recorder_.stop("USER_STOP");
      emit("OK,LOG_STOP_REQUESTED");
      return;
    }
    if (!strcmp(cmd, "CAL_SHOW") && !a) {
      showCal();
      return;
    }
    if (recorder_.busy() || captureMode_) {
      emit("ERROR,BUSY_USE_LOG_STOP");
      return;
    }
    if (!strcmp(cmd, "CAL_RESET") && !a) {
      cal_.reset();
      window_ = Window{};
      emit("OK,CAL_RESET,force_ok=0");
    } else if (!strcmp(cmd, "TIME") && a && !extra) {
      uint64_t epoch = 0;
      if (!parseUtc(a, strlen(a), epoch)) {
        emit("ERROR,BAD_UTC_EXPECT_YYYY_MM_DD_T_HH_MM_SS_Z");
        return;
      }
      utcEpoch_ = epoch;
      utcBaseUs_ = clock_.update(micros());
      utcKnown_ = true;
      emit("OK,UTC_ANCHOR_SET,source=manual,accuracy_verified=0");
    } else if ((!strcmp(cmd, "TARE") && !a) || (!strcmp(cmd, "CAL") && a && !extra)) {
      if (!fresh(clock_.update(micros())) || lastRail_) {
        emit("ERROR,SENSOR_NOT_READY_OR_RAIL");
        return;
      }
      const bool isTare = !strcmp(cmd, "TARE");
      if (!isTare && (!cal_.zeroKnown || !parseMass(a, captureKg_))) {
        emit("ERROR,CAL_REQUIRES_TARE_AND_KNOWN_KG_GT_0_LE_100");
        return;
      }
      if (isTare)
        cal_.reset();
      else {
        cal_.confirmed = false;
        cal_.countsPerKg = 0;
      }
      window_ = Window{};
      capture_ = Window{};
      captureMode_ = isTare ? 1 : 2;
      captureAt_ = millis();
      emit("OK,CAL_CAPTURE_STARTED,samples=320,hold_load_still=1");
    } else if (!strcmp(cmd, "LOG_START") && !a)
      startLog();
    else
      emit("ERROR,UNKNOWN_OR_MALFORMED_COMMAND");
  }
};

ForceLogger &logger() {
  static ForceLogger instance;
  return instance;
}
} // namespace
void setup() { logger().begin(); }
void loop() { logger().tick(); }
