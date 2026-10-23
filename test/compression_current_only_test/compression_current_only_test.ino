/*
 * UNCOUPLED secured motor ONLY. Support the axis and remove belt/screw coupling first.
 * Manual command PULSE signed_A: current requests for 1000 ms, then a zero-current request.
 * RUN signed_A: current requests until STOP, with NO automatic run deadline.
 * STOP / USB loss / bad input / TX congestion request zero. No ARM, LIMIT, PID or sensors.
 * Remain at the bench; RUN has no temperature, position, force or current feedback protection.
 * This sketch does NOT receive VESC feedback or prove that UART/STOP worked.
 * Physical power cut and VESC's own verified current/voltage/regen/timeout limits are required.
 * RX7=28 (unused feedback), TX7=29 -> VESC RX, common signal GND; VESC 5V/3.3V disconnected.
 * Other motor power must be disconnected. Zero current is NOT a brake or holding torque.
 */
#include <Arduino.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>

#if !defined(ARDUINO_TEENSY41)
#error "Select Teensy 4.1 / USB Type Serial"
#endif

namespace {
constexpr uint8_t PIN_CP_UART_RX = 28, PIN_CP_UART_TX = 29;
constexpr uint32_t PULSE_MS = 1000, TX_MS = 100, LINE_TIMEOUT_MS = 1000;
// No sketch-specific amperage ceiling. Keep the signed-int32 wire representation bound.
// This is NOT a safe motor/supply current limit; VESC protection settings remain external.

constexpr uint16_t crc16(const uint8_t *data, size_t n) {
  uint16_t crc = 0;
  for (size_t i = 0; i < n; ++i) {
    crc ^= uint16_t(data[i]) << 8;
    for (unsigned bit = 0; bit < 8; ++bit)
      crc = uint16_t((crc << 1) ^ ((crc & 0x8000U) ? 0x1021U : 0U));
  }
  return crc;
}

// Exact decimal amperes, up to 3 fractional digits. No units/exponents/NaN/Inf.
constexpr bool parseCurrent(const char *p, int32_t &out) {
  if (!p || !*p)
    return false;
  const bool negative = *p == '-';
  if (*p == '-' || *p == '+')
    ++p;
  if (!*p)
    return false;
  uint32_t whole = 0, fraction = 0;
  const uint32_t maxMagnitude = uint32_t(INT32_MAX) + (negative ? 1U : 0U);
  const uint32_t maxWhole = maxMagnitude / 1000U;
  unsigned decimals = 0;
  bool dot = false, digit = false;
  for (; *p; ++p) {
    if (*p == '.' && !dot) {
      dot = true;
      continue;
    }
    if (*p < '0' || *p > '9')
      return false;
    digit = true;
    if (dot) {
      if (++decimals > 3)
        return false;
      fraction = fraction * 10U + uint32_t(*p - '0');
    } else {
      const uint32_t nextDigit = uint32_t(*p - '0');
      if (whole > maxWhole / 10U || (whole == maxWhole / 10U && nextDigit > maxWhole % 10U))
        return false;
      whole = whole * 10U + nextDigit;
    }
  }
  if (!digit || (dot && !decimals))
    return false;
  while (decimals++ < 3)
    fraction *= 10U;
  const uint32_t magnitude = whole * 1000U + fraction;
  if (!magnitude || magnitude > maxMagnitude)
    return false;
  // Avoid both out-of-range signed conversion and negation overflow at INT32_MIN.
  out = negative ? -int32_t(magnitude - 1U) - 1 : int32_t(magnitude);
  return true;
}

class CurrentPulse {
public:
  void begin() {
    Serial7.setRX(PIN_CP_UART_RX);
    Serial7.setTX(PIN_CP_UART_TX);
    Serial7.begin(115200, SERIAL_8N1);
    Serial.begin(115200);
    stop("BOOT");
  }

  void tick() {
    const uint32_t now = millis();
    if (!Serial) {
      if (usb_)
        stop("USB_DISCONNECTED");
      usb_ = false;
      used_ = 0;
      discard_ = false;
    } else if (!usb_) {
      // Discard stale commands across USB session boundaries; never auto-resume.
      for (unsigned i = 0; i < 64 && Serial.available(); ++i)
        Serial.read();
      if (Serial.available())
        return;
      stop("USB_CONNECTED");
      usb_ = true;
      emit("READY,build=current_manual_stop_v3,auto_start=0,feedback_verified=0");
      emit("LIMITS,sketch_current_cap=NONE,wire=signed_int32_mA,vesc_limits_not_modified=1");
      help();
    }
    if (pulseExpired())
      stop("PULSE_DONE");
    if (used_ && uint32_t(now - lineAt_) >= LINE_TIMEOUT_MS) {
      used_ = 0;
      discard_ = true;
      stop("COMMAND_TIMEOUT");
    }
    if (usb_) {
      for (unsigned i = 0; i < 64 && Serial.available(); ++i) {
        if (pulseExpired())
          stop("PULSE_DONE");
        const char c = char(Serial.read());
        if (c == '\n') {
          if (!discard_ && used_) {
            line_[used_] = 0;
            command();
          }
          used_ = 0;
          discard_ = false;
        } else if (!discard_) {
          if (c == '\r' || c == '\t')
            append(' ');
          else if (c >= 32 && c <= 126)
            append(c);
          else
            badLine();
        }
      }
    }
    // Deadline check again immediately before any nonzero transmission.
    if (pulseExpired())
      stop("PULSE_DONE");
    if (zeroPending_) {
      zeroPending_ = !sendCurrent(0);
      return; // Pending zero always outranks nonzero requests.
    }
    if (active_ && uint32_t(millis() - lastTx_) >= TX_MS) {
      if (!sendCurrent(milliAmps_))
        stop("UART_TX_FULL");
    }
  }

private:
  bool active_ = false, timed_ = false, usb_ = false, zeroPending_ = true, discard_ = false;
  int32_t milliAmps_ = 0;
  uint32_t started_ = 0, lastTx_ = 0, lineAt_ = 0, dropped_ = 0;
  char line_[48] = {};
  size_t used_ = 0;

  bool pulseExpired() const {
    return active_ && timed_ && uint32_t(millis() - started_) >= PULSE_MS;
  }
  void help() {
    emit("COMMANDS,RUN signed_A|PULSE signed_A|STOP|STATUS|HELP,pulse_ms=1000");
    emit("NOTE,RUN_mode=MANUAL_STOP,no_run_timer,use_STOP,PULSE_mode=ONE_SECOND");
    emit("WARNING,uncoupled_secured_motor_only,do_not_leave_running,no_feedback_or_holding_brake");
  }

  void emit(const char *fmt, ...) {
    if (!Serial)
      return;
    char line[240];
    va_list args;
    va_start(args, fmt);
    const int n = vsnprintf(line, sizeof(line) - 1, fmt, args);
    va_end(args);
    if (n < 0 || size_t(n) >= sizeof(line) - 1 || Serial.availableForWrite() < n + 1) {
      ++dropped_;
      return;
    }
    line[n] = '\n';
    Serial.write(reinterpret_cast<const uint8_t *>(line), size_t(n + 1));
  }
  bool sendCurrent(int32_t value) {
    uint8_t packet[10] = {2, 5, 6, 0, 0, 0, 0, 0, 0, 3}; // COMM_SET_CURRENT=6.
    const uint32_t u = uint32_t(value);
    for (unsigned i = 0; i < 4; ++i)
      packet[3 + i] = uint8_t(u >> (24 - i * 8));
    const uint16_t crc = crc16(packet + 2, 5);
    packet[7] = uint8_t(crc >> 8);
    packet[8] = uint8_t(crc);
    if (Serial7.availableForWrite() < int(sizeof(packet)))
      return false;
    if (Serial7.write(packet, sizeof(packet)) != sizeof(packet))
      return false;
    lastTx_ = millis();
    return true; // Queued only; this code does NOT receive an execution ACK.
  }
  void stop(const char *why) {
    active_ = false;
    timed_ = false;
    milliAmps_ = 0;
    zeroPending_ = !sendCurrent(0); // Attempt before reporting.
    emit("EVENT,%s,current_command_A=0,stop_pending=%u,standstill_verified=0", why,
         zeroPending_ ? 1U : 0U);
  }
  void badLine() {
    used_ = 0;
    discard_ = true;
    stop("BAD_OR_LONG_COMMAND");
  }
  void append(char c) {
    if (used_ >= sizeof(line_) - 1) {
      badLine();
      return;
    }
    if (!used_)
      lineAt_ = millis();
    line_[used_++] = c;
  }
  static char *token(char *&cursor) {
    while (*cursor == ' ')
      ++cursor;
    if (!*cursor)
      return nullptr;
    char *result = cursor;
    while (*cursor && *cursor != ' ')
      ++cursor;
    if (*cursor)
      *cursor++ = 0;
    return result;
  }
  void command() {
    char *cursor = line_;
    char *cmd = token(cursor);
    char *arg = token(cursor);
    char *extra = token(cursor);
    if (!cmd)
      return;
    if (!strcmp(cmd, "STOP") && !arg) {
      stop("USER_STOP");
      return;
    }
    if (!strcmp(cmd, "STATUS") && !arg) {
      const uint32_t elapsed = millis() - started_;
      char remaining[48] = "";
      if (active_ && timed_)
        snprintf(remaining, sizeof remaining, ",remaining_ms=%lu",
                 static_cast<unsigned long>(elapsed < PULSE_MS ? PULSE_MS - elapsed : 0));
      emit("STATUS,command_active=%u,command_current_A=%.3f,run_mode=%s,elapsed_ms=%lu%s,stop_"
           "pending=%u,"
           "feedback_verified=0,dropped_logs=%lu",
           active_ ? 1U : 0U, milliAmps_ / 1000.0,
           active_ ? (timed_ ? "PULSE_1S" : "MANUAL_STOP") : "STOPPED",
           static_cast<unsigned long>(active_ ? elapsed : 0), remaining, zeroPending_ ? 1U : 0U,
           static_cast<unsigned long>(dropped_));
      return;
    }
    if (!strcmp(cmd, "HELP") && !arg) {
      help();
      return;
    }
    int32_t value = 0;
    const bool manual = !strcmp(cmd, "RUN");
    if ((!manual && strcmp(cmd, "PULSE")) || !arg || extra || !parseCurrent(arg, value)) {
      stop("BAD_COMMAND_EXPECT_PULSE_OR_RUN_SIGNED_A");
      return;
    }
    if (active_ || zeroPending_) {
      stop("STOP_FIRST");
      return;
    }
    started_ = millis();
    if (!sendCurrent(value)) {
      stop("UART_TX_FULL");
      return;
    }
    milliAmps_ = value;
    timed_ = !manual;
    active_ = true;
    if (manual)
      emit("OK,RUN_STARTED,command_current_A=%.3f,run_mode=MANUAL_STOP,use_STOP=1,rotation_"
           "verified=0",
           milliAmps_ / 1000.0);
    else
      emit("OK,PULSE_STARTED,command_current_A=%.3f,duration_ms=1000,rotation_verified=0",
           milliAmps_ / 1000.0);
  }
};
CurrentPulse &pulse() {
  static CurrentPulse instance;
  return instance;
}
} // namespace
void setup() { pulse().begin(); }
void loop() { pulse().tick(); }
