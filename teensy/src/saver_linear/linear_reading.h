#ifndef SAVER_LINEAR_READING_H
#define SAVER_LINEAR_READING_H
// Shared by main firmware and the standalone calibration sketch.

#include <stddef.h>
#include <stdint.h>

namespace saver_linear {

constexpr int ADC_MAX = 4095;
constexpr uint32_t COMMAND_TIMEOUT_MS = 1000;

constexpr bool elapsed(uint32_t now, uint32_t since, uint32_t interval) {
  return static_cast<uint32_t>(now - since) >= interval;
}

struct Reading {
  bool raw_ok;
  int raw;
  float adc_V;
  bool rail;
};

// raw_ok describes the ADC result, NOT sensor presence or position accuracy.
constexpr Reading convert(int raw, float reference_V) {
  return {raw >= 0 && raw <= ADC_MAX && reference_V > 0.0f && reference_V <= 3.3f, raw,
          static_cast<float>(raw) * reference_V / ADC_MAX, raw <= 1 || raw >= ADC_MAX - 1};
}

enum class LineEvent { None, Ready, Invalid, Overflow, Timeout };

// After a damaged/timed-out line, discard its suffix through LF. No partial command execution.
class CommandLine {
public:
  constexpr CommandLine() : text_{}, length_(0), started_(0), active_(false), discard_(false) {}

  constexpr void reset() {
    length_ = 0;
    started_ = 0;
    active_ = false;
    discard_ = false;
    text_[0] = '\0';
  }

  constexpr LineEvent expire(uint32_t now) {
    if (active_ && elapsed(now, started_, COMMAND_TIMEOUT_MS)) {
      active_ = false;
      length_ = 0;
      discard_ = true;
      return LineEvent::Timeout;
    }
    return LineEvent::None;
  }

  constexpr LineEvent feed(char c, uint32_t now) {
    const LineEvent timeout = expire(now);
    if (c == '\n') {
      const bool ready = !discard_ && length_ > 0;
      text_[length_] = '\0';
      length_ = 0;
      active_ = false;
      discard_ = false;
      return timeout != LineEvent::None ? timeout : (ready ? LineEvent::Ready : LineEvent::None);
    }
    if (discard_)
      return timeout;
    if (!active_) {
      active_ = true;
      started_ = now;
    }
    // Preserve CR as whitespace; embedded CR cannot silently join two tokens.
    if (c == '\r' || c == '\t')
      c = ' ';
    if (c < 32 || c > 126) {
      length_ = 0;
      active_ = false;
      discard_ = true;
      return LineEvent::Invalid;
    }
    if (length_ >= sizeof(text_) - 1) {
      length_ = 0;
      active_ = false;
      discard_ = true;
      return LineEvent::Overflow;
    }
    text_[length_++] = c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c;
    return LineEvent::None;
  }

  constexpr const char *text() const { return text_; }

private:
  char text_[32];
  size_t length_;
  uint32_t started_;
  bool active_;
  bool discard_;
};

constexpr bool matches(const char *line, const char *command) {
  while (*line == ' ')
    ++line;
  while (*command && *line == *command) {
    ++line;
    ++command;
  }
  if (*command)
    return false;
  while (*line == ' ')
    ++line;
  return *line == '\0';
}

} // namespace saver_linear
#endif
