#ifndef SAVER_REACTION_LOGIC_H
#define SAVER_REACTION_LOGIC_H

#include <stddef.h>
#include <stdint.h>
#include <float.h>

namespace reaction {
constexpr uint32_t SENSOR_SPS = 320;
constexpr uint32_t SUMMARY_US = 50000;
constexpr uint32_t GAP_US = 6250; // Diagnostic only: >2 nominal conversion periods.
constexpr int32_t RAW_MIN = -8388608, RAW_MAX = 8388607;
constexpr double STANDARD_G = 9.80665; // kg calibration mass -> N, not a safety limit.
constexpr bool finite(double v) { return v == v && v <= DBL_MAX && v >= -DBL_MAX; }
constexpr int32_t decode24(uint8_t hi, uint8_t mid, uint8_t lo) {
  const uint32_t u = (uint32_t(hi) << 16) | (uint32_t(mid) << 8) | lo;
  return u & 0x800000U ? int32_t(u) - 16777216 : int32_t(u);
}
constexpr bool rail(int32_t raw) { return raw <= RAW_MIN || raw >= RAW_MAX; }
constexpr bool separated(int32_t aMin, int32_t aMax, int32_t bMin, int32_t bMax) {
  return aMin <= aMax && bMin <= bMax && (bMin > aMax || bMax < aMin);
}

struct Calibration {
  double zero = 0, countsPerKg = 0;
  bool zeroKnown = false, confirmed = false;
  constexpr void reset() { *this = Calibration{}; }
  constexpr bool tare(double raw) {
    reset();
    if (!finite(raw) || raw <= RAW_MIN || raw >= RAW_MAX)
      return false;
    zero = raw;
    zeroKnown = true;
    return true;
  }
  constexpr bool calibrate(double loadedRaw, double massKg) {
    confirmed = false;
    countsPerKg = 0;
    if (!zeroKnown || !finite(loadedRaw) || loadedRaw <= RAW_MIN || loadedRaw >= RAW_MAX ||
        !finite(massKg) || massKg <= 0 || massKg > 100 || loadedRaw == zero)
      return false;
    countsPerKg = (loadedRaw - zero) / massKg;
    confirmed = finite(countsPerKg) && countsPerKg != 0;
    return confirmed;
  }
  constexpr bool force(int32_t raw, double &out) const {
    if (!confirmed || !finite(countsPerKg) || countsPerKg == 0 || rail(raw))
      return false;
    const double value = (raw - zero) * STANDARD_G / countsPerKg;
    if (!finite(value))
      return false;
    out = value; // Negative values are preserved, not clamped to fake zero.
    return true;
  }
};

struct Window {
  uint32_t count = 0, validCount = 0, forceCount = 0, rails = 0;
  int64_t rawSum = 0;
  int32_t rawMin = RAW_MAX, rawMax = RAW_MIN;
  double forceSum = 0, forceMin = 0, forceMax = 0;
  constexpr void add(int32_t raw, const Calibration &cal) {
    ++count;
    if (rail(raw)) {
      ++rails;
      return;
    }
    ++validCount;
    rawSum += raw;
    if (raw < rawMin)
      rawMin = raw;
    if (raw > rawMax)
      rawMax = raw;
    double f = 0;
    if (cal.force(raw, f)) {
      if (!forceCount || f < forceMin)
        forceMin = f;
      if (!forceCount || f > forceMax)
        forceMax = f;
      forceSum += f;
      ++forceCount;
    }
  }
};

// Single producer/consumer in loop(), NOT shared with an interrupt.
template <size_t N> class ByteQueue {
public:
  constexpr size_t used() const { return used_; }
  constexpr size_t free() const { return N - used_; }
  constexpr void clear() { head_ = tail_ = used_ = 0; }
  constexpr bool push(const char *p, size_t n) {
    if (n > free())
      return false; // Whole line or nothing.
    for (size_t i = 0; i < n; ++i) {
      data_[head_] = p[i];
      head_ = (head_ + 1) % N;
    }
    used_ += n;
    return true;
  }
  constexpr size_t peek(char *p, size_t n) const {
    if (n > used_)
      n = used_;
    for (size_t i = 0; i < n; ++i)
      p[i] = data_[(tail_ + i) % N];
    return n;
  }
  constexpr void consume(size_t n) {
    if (n > used_)
      n = used_;
    tail_ = (tail_ + n) % N;
    used_ -= n;
  }

private:
  char data_[N] = {};
  size_t head_ = 0, tail_ = 0, used_ = 0;
};

class MicroClock {
public:
  constexpr uint64_t update(uint32_t low) {
    total_ += uint32_t(low - previous_);
    previous_ = low;
    return total_;
  }

private:
  uint32_t previous_ = 0;
  uint64_t total_ = 0; // Loop must run at least once per micros() wrap (~71 min).
};

constexpr bool leap(int y) { return y % 4 == 0 && (y % 100 != 0 || y % 400 == 0); }
constexpr int monthDays(int y, int m) {
  return m == 2 ? (leap(y) ? 29 : 28) : (m == 4 || m == 6 || m == 9 || m == 11 ? 30 : 31);
}
constexpr int digits(const char *s, size_t n) {
  int v = 0;
  for (size_t i = 0; i < n; ++i) {
    if (s[i] < '0' || s[i] > '9')
      return -1;
    v = v * 10 + s[i] - '0';
  }
  return v;
}
// Exact UTC ISO 8601 input. No automatic claim that PC/RTC time is synchronized.
constexpr bool parseUtc(const char *s, size_t n, uint64_t &epochSeconds) {
  if (n != 20 || s[4] != '-' || s[7] != '-' || s[10] != 'T' || s[13] != ':' || s[16] != ':' ||
      s[19] != 'Z')
    return false;
  const int y = digits(s, 4), m = digits(s + 5, 2), d = digits(s + 8, 2);
  const int h = digits(s + 11, 2), min = digits(s + 14, 2), sec = digits(s + 17, 2);
  if (y < 2020 || y > 2099 || m < 1 || m > 12 || d < 1 || d > monthDays(y, m) || h < 0 || h > 23 ||
      min < 0 || min > 59 || sec < 0 || sec > 59)
    return false;
  uint64_t days = 0;
  for (int yr = 1970; yr < y; ++yr)
    days += leap(yr) ? 366 : 365;
  for (int mo = 1; mo < m; ++mo)
    days += monthDays(y, mo);
  epochSeconds = (days + d - 1) * 86400 + h * 3600 + min * 60 + sec;
  return true;
}
struct DateParts {
  int year, month, day, hour, minute, second;
};
constexpr DateParts dateParts(uint64_t seconds) {
  uint64_t days = seconds / 86400;
  int y = 1970, m = 1;
  while (days >= uint64_t(leap(y) ? 366 : 365)) {
    days -= leap(y) ? 366 : 365;
    ++y;
  }
  while (days >= uint64_t(monthDays(y, m))) {
    days -= monthDays(y, m);
    ++m;
  }
  return {y, m, int(days) + 1, int(seconds / 3600 % 24), int(seconds / 60 % 60), int(seconds % 60)};
}
constexpr bool parseMass(const char *s, double &out) {
  if (!s || !*s)
    return false;
  double value = 0, fraction = 1;
  bool dot = false, digit = false;
  for (; *s; ++s) {
    if (*s == '.' && !dot) {
      dot = true;
      continue;
    }
    if (*s < '0' || *s > '9')
      return false;
    digit = true;
    if (dot) {
      fraction *= 0.1;
      value += (*s - '0') * fraction;
    } else
      value = value * 10 + *s - '0';
    if (!finite(value) || value > 100)
      return false;
  }
  if (!digit || value <= 0)
    return false;
  out = value;
  return true;
}

enum class LineResult { Pending, Ready, Invalid };
class CommandLine {
public:
  constexpr void reset() {
    used_ = 0;
    discard_ = false;
    text_[0] = 0;
  }
  constexpr bool expire(uint32_t now) {
    if (!used_ || uint32_t(now - started_) < 1000)
      return false;
    used_ = 0;
    discard_ = true;
    return true;
  }
  constexpr LineResult push(char c, uint32_t now) {
    if (c == '\n') {
      if (discard_) {
        reset();
        return LineResult::Pending;
      }
      text_[used_] = 0;
      return used_ ? LineResult::Ready : LineResult::Pending;
    }
    if (discard_)
      return LineResult::Pending;
    if (c == '\r' || c == '\t')
      c = ' ';
    if (c < 32 || c > 126 || used_ >= sizeof(text_) - 1) {
      used_ = 0;
      discard_ = true;
      return LineResult::Invalid;
    }
    if (!used_)
      started_ = now;
    text_[used_++] = c;
    return LineResult::Pending;
  }
  constexpr char *text() { return text_; }

private:
  char text_[80] = {};
  size_t used_ = 0;
  uint32_t started_ = 0;
  bool discard_ = false;
};
} // namespace reaction
#endif
