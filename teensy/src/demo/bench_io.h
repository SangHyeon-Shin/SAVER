#ifndef SAVER_SELECTABLE_BENCH_IO_H
#define SAVER_SELECTABLE_BENCH_IO_H
#include <Arduino.h>
#include <Wire.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "src/bench_logic.h"
#include "src/logger_logic.h"

namespace selectable {
// Whole-line priority queues. USB congestion never waits for the host.
class Log {
public:
  void clear() {
    urgent_.clear();
    telemetry_.clear();
    remaining_ = 0;
  }
  void emit(const char *format, ...) {
    char b[320];
    va_list args;
    va_start(args, format);
    const int n = vsnprintf(b, sizeof(b), format, args);
    va_end(args);
    if (n < 0 || n >= int(sizeof(b) - 1)) {
      ++dropped;
      return;
    }
    if (!strncmp(b, "ERROR,", 6) || !strncmp(b, "FAULT,", 6)) {
      failed_ = true;
      snprintf(reason_, sizeof(reason_), "%.95s", b + 6);
      char *comma = strchr(reason_, ',');
      if (comma)
        *comma = 0;
    }
    char line[512];
    size_t p = size_t(
        snprintf(line, sizeof(line), "{\"v\":1,\"t\":\"evt\",\"evt\":\"BENCH_LOG\",\"msg\":\""));
    for (int i = 0; i < n && p + 5 < sizeof(line); ++i) {
      if (b[i] == '"' || b[i] == '\\')
        line[p++] = '\\';
      line[p++] = b[i] >= 32 ? b[i] : ' ';
    }
    line[p++] = '"';
    line[p++] = '}';
    line[p++] = '\n';
    line[p] = 0;
    if (!urgent_.push(line, p))
      ++dropped;
  }
  void data(const char *text) {
    if (!telemetry_.push(text, strlen(text)))
      ++dropped;
  }
  void flush() {
    if (!Serial)
      return;
    // Select one queue until a whole queued fragment has drained, so two lines
    // are never interleaved when an urgent event arrives during a partial write.
    if (!remaining_) {
      urgentActive_ = urgent_.used() != 0;
      remaining_ = (urgentActive_ ? urgent_.used() : telemetry_.used());
    }
    const int free = Serial.availableForWrite();
    if (free <= 0 || !remaining_)
      return;
    char b[64];
    size_t n = remaining_ < sizeof(b) ? remaining_ : sizeof(b);
    if (n > size_t(free))
      n = size_t(free);
    if (urgentActive_)
      urgent_.peek(b, n);
    else
      telemetry_.peek(b, n);
    const size_t sent = Serial.write(reinterpret_cast<const uint8_t *>(b), n);
    if (urgentActive_)
      urgent_.consume(sent);
    else
      telemetry_.consume(sent);
    remaining_ -= sent;
  }
  uint32_t dropped = 0;
  void raw(const char *line) {
    const size_t n = strlen(line);
    char b[513];
    if (n > 511) {
      ++dropped;
      return;
    }
    memcpy(b, line, n);
    b[n] = '\n';
    if (!urgent_.push(b, n + 1))
      ++dropped;
  }
  void beginCommand() {
    failed_ = false;
    reason_[0] = 0;
  }
  bool failed() const { return failed_; }
  const char *reason() const { return reason_; }

private:
  reaction::ByteQueue<8192> urgent_;
  reaction::ByteQueue<4096> telemetry_;
  size_t remaining_ = 0;
  bool urgentActive_ = false;
  bool failed_ = false;
  char reason_[96] = {};
};

// Teensy Wire has bounded core transactions (not an Arduino Stream timeout).
// A stuck transaction can still miss the PID deadline: control checks again
// immediately after I2C and never sends nonzero current from a stale tick.
template <typename WireType> class RegisterBus {
public:
  RegisterBus(WireType &wire, uint8_t address) : wire_(wire), address_(address) {}
  bool write(uint8_t reg, uint8_t value) {
    wire_.beginTransmission(address_);
    wire_.write(reg);
    wire_.write(value);
    return wire_.endTransmission() == 0;
  }
  bool read(uint8_t reg, uint8_t *bytes, size_t n) {
    if (!n || n > 64)
      return false;
    wire_.beginTransmission(address_);
    wire_.write(reg);
    if (wire_.endTransmission(false) != 0)
      return false;
    const auto got = wire_.requestFrom(address_, uint8_t(n));
    if (got != n)
      return false;
    for (size_t i = 0; i < n; ++i) {
      if (!wire_.available())
        return false;
      bytes[i] = uint8_t(wire_.read());
    }
    return true;
  }

private:
  WireType &wire_;
  uint8_t address_;
};

struct Command {
  char *tokens[8] = {};
  unsigned count = 0;
  explicit Command(char *line) {
    char *save = nullptr;
    for (char *p = strtok_r(line, " \t", &save); p; p = strtok_r(nullptr, " \t", &save)) {
      if (count == 8) {
        count = 9;
        break;
      }
      tokens[count++] = p;
    }
  }
  bool is(const char *name, unsigned arity) const {
    return count == arity && strcmp(tokens[0], name) == 0;
  }
};
enum class Mode { None, Vacuum, Compression, Ppg };
inline const char *modeName(Mode m) {
  return m == Mode::Vacuum        ? "VACUUM"
         : m == Mode::Compression ? "COMPRESSION"
         : m == Mode::Ppg         ? "PPG"
                                  : "NONE";
}
inline bool parseMode(const char *s, Mode &out) {
  if (!strcmp(s, "VACUUM"))
    out = Mode::Vacuum;
  else if (!strcmp(s, "COMPRESSION"))
    out = Mode::Compression;
  else if (!strcmp(s, "PPG"))
    out = Mode::Ppg;
  else if (!strcmp(s, "NONE"))
    out = Mode::None;
  else
    return false;
  return true;
}
} // namespace selectable
#endif
