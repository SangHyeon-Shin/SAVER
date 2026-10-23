/* Research prototype, secured bench/mannequin fixture ONLY; never human use.
 * Exclusive VACUUM / COMPRESSION / PPG modules. This is a human bench console,
 * NOT Jetson JSON firmware. Mode selection never starts a motor.
 * Physical power-cut E-stop is mandatory; software STOP cannot replace it.
 * See README.md and src/bench_config.h (frozen copy of original bench settings).
 */
#include <Arduino.h>
#include "vacuum_module.h"
#include "compression_module.h"
#include "ppg_module.h"
#if !defined(ARDUINO_TEENSY41)
#error "Select Teensy 4.1 / USB Type Serial"
#endif

namespace selectable {
static_assert(A8 == 22 && A16 == 40, "Teensy 4.1 physical ADC pin mapping changed");
class Bench {
public:
  Bench() : vacuum_(log_), compression_(log_), ppg_(log_) {}
  void begin() {
    analogReadResolution(12);
    analogReadAveraging(16);
    // No I2C sensor initialization or acquisition until selected.
    vacuum_.begin();
    compression_.begin();
    Serial.begin(115200);
  }
  void tick() {
    syncUsb();
    compression_.serviceStop(); // Zero requests only; never another mode's queries.
    if (!connected_)
      return;
    if (mode_ == Mode::Vacuum)
      vacuum_.tick();
    else if (mode_ == Mode::Compression)
      compression_.tick();
    else if (mode_ == Mode::Ppg)
      ppg_.tick();
    pollUsb();
    if (mode_ == Mode::Compression) {
      compression_.tick();
      if (uint32_t(millis() - checkAt_) >= 50) {
        checkAt_ = millis();
        compression_.serviceCheck();
      }
    }
    if (uint32_t(millis() - reportAt_) >= 1000) {
      reportAt_ = millis();
      report();
    }
    log_.flush();
  }

private:
  void stop() {
    vacuum_.stop();
    compression_.stop();
  }
  void invalidateInput() {
    line_.reset();
    // Bound each iteration. Reconnection discards input until the hardware RX
    // buffer is empty; bytes queued while disconnected are never executed.
    draining_ = true;
  }
  void syncUsb() {
    const bool now = bool(Serial);
    if (now == connected_)
      return;
    connected_ = now;
    stop();
    ppg_.stop();
    mode_ = Mode::None;
    invalidateInput();
    log_.clear();
    if (now) {
      log_.emit("SAVER_SELECTABLE_SYSTEM_BENCH_READY,build=exclusive_modules_v1");
      log_.emit("EVENT,USB_CONNECTED,mode=NONE,motors_off_commanded=1,send_fresh_command=1");
      help();
    }
  }
  void switchMode(Mode next) {
    stop();
    const bool quiet = ppg_.stop();
    mode_ = Mode::None;
    // Never enter a different sensing/control module while a release request is
    // still queued or PPG shutdown cannot be confirmed. A fresh MODE retries.
    if (next != Mode::None && (!quiet || compression_.stopPending())) {
      log_.emit("ERROR,MODE_CHANGE_STOP_UNCONFIRMED,mode=NONE");
      return;
    }
    mode_ = next;
    if (next == Mode::Vacuum)
      vacuum_.enter();
    else if (next == Mode::Compression)
      compression_.enter();
    else if (next == Mode::Ppg)
      ppg_.enter();
    log_.emit("OK,MODE,selected=%s,motors_started=0", modeName(mode_));
  }
  void report() {
    log_.emit(
        "STATUS,mode=%s,motors_commanded_running=%u,stop_pending=%u,ppg_quiet=%u,dropped_logs=%lu",
        modeName(mode_), vacuum_.active() || compression_.active(), compression_.stopPending(),
        ppg_.quiet(), static_cast<unsigned long>(log_.dropped));
    if (mode_ == Mode::Vacuum)
      vacuum_.report();
    else if (mode_ == Mode::Compression)
      compression_.report();
    else if (mode_ == Mode::Ppg)
      ppg_.report();
  }
  void help() {
    log_.emit("COMMANDS,MODE VACUUM|MODE COMPRESSION|MODE PPG|MODE NONE|STOP|STATUS|HELP");
    log_.emit("VACUUM,ARM|RUN1 [pct]|RUN2 [pct]|RUNBOTH pct1 pct2|SET1 pct|SET2 pct|"
              "CLEAR_ERR|CLEAR_ERR1|CLEAR_ERR2|ZERO|UNZERO");
    log_.emit("COMPRESSION,ZERO|CAL measured_mm|TARE|FORCE_CAL known_kg|TARGET depth_mm cpm|"
              "ILIMIT A|INLIMIT A|GAINS kp ki kd|CHECK|CLEAR|ARM|RUN [cycles]");
    log_.emit("NOTE,STOP_stops_motors_PPG_acquisition_continues,MODE_NONE_stops_all_acquisition");
  }
  void command(char *text) {
    Command c(text);
    if (!c.count)
      return;
    if (c.is("STOP", 1) || c.is("DISARM", 1)) {
      stop();
      log_.emit("OK,STOP,standstill_verified=0");
      return;
    }
    if (c.is("HELP", 1)) {
      help();
      return;
    }
    if (c.is("STATUS", 1)) {
      report();
      return;
    }
    if (c.is("MODE", 2)) {
      Mode next;
      if (parseMode(c.tokens[1], next)) {
        switchMode(next);
        return;
      }
    } else if (mode_ == Mode::Vacuum && ppg_.quiet() && vacuum_.command(c))
      return;
    else if (mode_ == Mode::Compression && ppg_.quiet() && compression_.command(c))
      return;
    stop();
    log_.emit("ERROR,BAD_COMMAND_OR_WRONG_MODE,selected=%s", modeName(mode_));
  }
  void pollUsb() {
    if (draining_) {
      for (unsigned i = 0; i < 128 && Serial.available(); ++i)
        Serial.read();
      if (!Serial.available())
        draining_ = false;
      return;
    }
    if (line_.expire(millis())) {
      stop();
      log_.emit("ERROR,PARTIAL_COMMAND_TIMEOUT");
    }
    // One complete command per tick; module control keeps running between lines.
    for (unsigned i = 0; i < 64 && Serial.available(); ++i) {
      const auto r = line_.push(char(Serial.read()), millis());
      if (r == saver_vacuum_bench::LineResult::Invalid) {
        stop();
        log_.emit("ERROR,INVALID_COMMAND_LINE");
        return;
      }
      if (r == saver_vacuum_bench::LineResult::Ready) {
        command(line_.data());
        line_.reset();
        return;
      }
    }
  }
  Log log_;
  Vacuum vacuum_;
  Compression compression_;
  Ppg ppg_;
  saver_vacuum_bench::CommandLine line_;
  Mode mode_ = Mode::None;
  bool connected_ = false, draining_ = true;
  uint32_t reportAt_ = 0, checkAt_ = 0;
};
Bench &instance() {
  static Bench value;
  return value;
}
} // namespace selectable
void setup() { selectable::instance().begin(); }
void loop() { selectable::instance().tick(); }
