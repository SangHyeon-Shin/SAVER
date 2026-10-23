// Bench-only Stage4 parser/baseline plus manual-run logic; no production dependency.
#ifndef SAVER_COMBINED_BENCH_LOGIC_H
#define SAVER_COMBINED_BENCH_LOGIC_H

#include <stddef.h>
#include <stdint.h>

namespace saver_vacuum_bench {

// Existing bench limits, not medical or motor-current limits.
constexpr uint32_t MIN_RUN_MS = 100;
constexpr uint32_t MAX_RUN_MS = 30000;
constexpr uint32_t ERR_CLEAR_STABLE_MS = 250;
constexpr uint32_t COMMAND_TIMEOUT_MS = 1000;

constexpr uint8_t dutyToPwm(uint8_t percent) {
  return (uint8_t)(((uint16_t)percent * 255U + 50U) / 100U);
}

constexpr bool parseUnsigned(const char* text, uint32_t& value) {
  if (text == nullptr || *text == '\0') return false;
  uint32_t parsed = 0;
  for (const char* p = text; *p; ++p) {
    if (*p < '0' || *p > '9') return false;
    const uint32_t digit = (uint32_t)(*p - '0');
    if (parsed > (UINT32_MAX - digit) / 10U) return false;
    parsed = parsed * 10U + digit;
  }
  value = parsed; // A failed parse does not alter the caller's value.
  return true;
}

// Shared with compile-time tests. This class makes decisions, not GPIO writes.
// The sketch must synchronize outputs BEFORE reporting any rejection/fault.
class RunState {
 public:
  constexpr void stop() { running_ = false; duty_ = 0; duration_ = 0; }
  constexpr bool observeErr(bool active, uint32_t now) {
    if (active) {
      const bool newlyLatched = !fault_;
      fault_ = true;
      inactiveTracking_ = false;
      stop();
      return newlyLatched;
    }
    if (!inactiveTracking_) { inactiveTracking_ = true; inactiveSince_ = now; }
    return false;
  }
  constexpr const char* start(uint32_t duty, uint32_t duration, uint32_t now) {
    if (fault_) { stop(); return "ERR_FAULT_USE_CLEAR_ERR"; }
    if (running_) { stop(); return "ALREADY_RUNNING_USE_SET"; }
    if (duty < 1 || duty > 100 || duration < MIN_RUN_MS || duration > MAX_RUN_MS) {
      stop(); return "RUN_OUT_OF_RANGE";
    }
    duty_ = (uint8_t)duty; duration_ = duration; started_ = now; running_ = true;
    return nullptr;
  }
  constexpr const char* set(uint32_t duty) {
    if (duty > 100) { stop(); return "BAD_SET_DUTY"; }
    if (duty == 0) { stop(); return nullptr; } // Always permit a stop request.
    if (fault_) { stop(); return "ERR_FAULT_USE_CLEAR_ERR"; }
    if (!running_) return "NOT_RUNNING_USE_RUN";
    duty_ = (uint8_t)duty;
    return nullptr; // SET deliberately does not extend the run deadline.
  }
  constexpr const char* clearErr(uint32_t now) {
    if (running_) { stop(); return "CLEAR_ERR_REQUIRES_STOP"; }
    if (!inactiveTracking_ || (uint32_t)(now - inactiveSince_) < ERR_CLEAR_STABLE_MS)
      return "ERR_NOT_INACTIVE_FOR_250MS";
    fault_ = false;
    return nullptr; // Clearing an error never starts a motor.
  }
  constexpr bool expire(uint32_t now) {
    if (!running_ || (uint32_t)(now - started_) < duration_) return false;
    stop(); return true;
  }
  constexpr uint32_t remaining(uint32_t now) const {
    if (!running_) return 0;
    const uint32_t elapsed = (uint32_t)(now - started_);
    return elapsed >= duration_ ? 0 : duration_ - elapsed;
  }
  constexpr bool running() const { return running_; }
  constexpr bool fault() const { return fault_; }
  constexpr uint8_t duty() const { return duty_; }
 private:
  bool running_ = false, fault_ = false, inactiveTracking_ = false;
  uint8_t duty_ = 0;
  uint32_t started_ = 0, duration_ = 0, inactiveSince_ = 0;
};

enum class LineResult : uint8_t { Pending, Ready, Invalid };

class CommandLine {
 public:
  constexpr void reset() { length_ = 0; discard_ = false; bytes_[0] = '\0'; }
  constexpr bool expire(uint32_t now) {
    if (length_ == 0 || (uint32_t)(now - lastByte_) < COMMAND_TIMEOUT_MS) return false;
    length_ = 0; discard_ = true;
    return true; // Discard the late suffix until newline, never execute it.
  }
  constexpr LineResult push(char c, uint32_t now) {
    if (c == '\n') {
      if (discard_) { reset(); return LineResult::Pending; }
      bytes_[length_] = '\0';
      return length_ ? LineResult::Ready : LineResult::Pending;
    }
    if (discard_) return LineResult::Pending;
    // CRLF is accepted, but CR alone does not complete a command or extend its timeout.
    if (c == '\r') return LineResult::Pending;
    if (((unsigned char)c < 32 && c != '\t') || (unsigned char)c > 126 ||
        length_ >= sizeof bytes_ - 1) {
      length_ = 0; discard_ = true;
      return LineResult::Invalid;
    }
    lastByte_ = now;
    bytes_[length_++] = c;
    return LineResult::Pending;
  }
  constexpr char* data() { return bytes_; }
 private:
  char bytes_[64] = {};
  size_t length_ = 0;
  uint32_t lastByte_ = 0;
  bool discard_ = false;
};

// Manual-run state: only this model drives the combined sketch. The Stage4
// baseline above stays unchanged for its existing tests and timed sketches.
class ManualRunState {
 public:
  constexpr void stop() { running_ = false; duty_ = 0; }
  constexpr bool observeErr(bool active, uint32_t now) {
    if (active) {
      const bool newlyLatched = !fault_;
      fault_ = true;
      inactiveTracking_ = false;
      stop();
      return newlyLatched;
    }
    if (!inactiveTracking_) { inactiveTracking_ = true; inactiveSince_ = now; }
    return false;
  }
  constexpr const char* start(uint32_t duty) {
    if (fault_) { stop(); return "ERR_FAULT_USE_CLEAR_ERR"; }
    if (running_) { stop(); return "ALREADY_RUNNING_USE_SET"; }
    if (duty < 1 || duty > 100) { stop(); return "RUN_OUT_OF_RANGE"; }
    duty_ = (uint8_t)duty; running_ = true;
    return nullptr;
  }
  constexpr const char* set(uint32_t duty) {
    if (duty > 100) { stop(); return "BAD_SET_DUTY"; }
    if (duty == 0) { stop(); return nullptr; } // Always permit a stop request.
    if (fault_) { stop(); return "ERR_FAULT_USE_CLEAR_ERR"; }
    if (!running_) return "NOT_RUNNING_USE_RUN";
    duty_ = (uint8_t)duty;
    return nullptr;
  }
  constexpr const char* clearErr(uint32_t now) {
    if (running_) { stop(); return "CLEAR_ERR_REQUIRES_STOP"; }
    if (!inactiveTracking_ || (uint32_t)(now - inactiveSince_) < ERR_CLEAR_STABLE_MS)
      return "ERR_NOT_INACTIVE_FOR_250MS";
    fault_ = false;
    return nullptr; // Clearing an error never starts a motor.
  }
  constexpr bool running() const { return running_; }
  constexpr bool fault() const { return fault_; }
  constexpr uint8_t duty() const { return duty_; }
 private:
  bool running_ = false, fault_ = false, inactiveTracking_ = false;
  uint8_t duty_ = 0;
  uint32_t inactiveSince_ = 0; // Only the ERR-clear interval needs a clock.
};

// RUN/RUN1/RUN2 [pct]; RUNBOTH pct1 pct2. Never reinterpret an old timed
// command as an unlimited run. Commit parsed duties only after full validation.
constexpr const char* parseRunDuties(uint8_t mask, const char* first, const char* second,
                                    const char* extra, uint32_t defaultDuty,
                                    uint32_t& duty1, uint32_t& duty2) {
  if (mask < 1 || mask > 3) return "BAD_MOTOR_SELECTION";
  if (extra || (mask != 3 && second)) return "RUN_TIME_ARGUMENT_REMOVED";
  uint32_t next1 = defaultDuty, next2 = defaultDuty;
  if (mask == 3) {
    if (!parseUnsigned(first, next1) || !parseUnsigned(second, next2))
      return "BAD_RUN_ARGUMENTS";
  } else {
    if (first && !parseUnsigned(first, next1)) return "BAD_RUN_ARGUMENTS";
    next2 = next1;
  }
  duty1 = next1; duty2 = next2;
  return nullptr;
}

struct SafetyEvent {
  uint8_t faultMask = 0, stopMask = 0;
};

// Validate both channels before any GPIO output; idle channel faults remain local.
class CombinedState {
 public:
  constexpr const ManualRunState& motor(unsigned i) const { return motors_[i]; }
  constexpr bool armed() const { return armed_; }
  constexpr uint8_t runningMask() const {
    return (motors_[0].running() ? 1 : 0) | (motors_[1].running() ? 2 : 0);
  }
  constexpr void stop(uint8_t mask = 3, bool disarm = false) {
    for (unsigned i = 0; i < 2; ++i) if (mask & (1U << i)) motors_[i].stop();
    if (disarm) armed_ = false;
  }
  constexpr const char* fail(const char* reason) { stop(3, true); return reason; }
  constexpr const char* arm(bool pressureOk) {
    stop(3, true);
    if (!pressureOk) return "PRESSURE_UNAVAILABLE";
    armed_ = true;
    return nullptr;
  }
  constexpr SafetyEvent observe(bool err1, bool err2, uint32_t now) {
    SafetyEvent result;
    const uint8_t running = runningMask();
    if (motors_[0].observeErr(err1, now)) result.faultMask |= 1;
    if (motors_[1].observeErr(err2, now)) result.faultMask |= 2;
    result.stopMask = result.faultMask;
    if (result.faultMask & running) { stop(3, true); result.stopMask = 3; }
    return result;
  }
  constexpr const char* start(uint8_t mask, uint32_t duty1, uint32_t duty2, bool pressureOk) {
    if (!armed_) return fail("NOT_ARMED");
    if (!pressureOk) return fail("PRESSURE_UNAVAILABLE");
    if (runningMask()) return fail("ALREADY_RUNNING_USE_SET");
    if (mask < 1 || mask > 3) return fail("BAD_MOTOR_SELECTION");
    ManualRunState next[2] = {motors_[0], motors_[1]};
    for (unsigned i = 0; i < 2; ++i) if (mask & (1U << i)) {
      const char* error = next[i].start(i == 0 ? duty1 : duty2);
      if (error) return fail(error);
    }
    motors_[0] = next[0]; motors_[1] = next[1];
    return nullptr;
  }
  constexpr const char* set(unsigned i, uint32_t duty) {
    if (i > 1) return fail("BAD_MOTOR_SELECTION");
    const char* error = motors_[i].set(duty);
    return error ? fail(error) : nullptr;
  }
  constexpr const char* clear(uint8_t mask, uint32_t now) {
    if (runningMask()) return fail("CLEAR_ERR_REQUIRES_STOP");
    if (mask < 1 || mask > 3) return fail("BAD_MOTOR_SELECTION");
    ManualRunState next[2] = {motors_[0], motors_[1]};
    for (unsigned i = 0; i < 2; ++i) if (mask & (1U << i)) {
      const char* error = next[i].clearErr(now);
      if (error) return fail(error);
    }
    motors_[0] = next[0]; motors_[1] = next[1];
    return nullptr;
  }
 private:
  ManualRunState motors_[2];
  bool armed_ = false;
};

} // namespace saver_vacuum_bench
#endif
