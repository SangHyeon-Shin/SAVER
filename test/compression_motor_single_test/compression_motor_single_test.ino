/*
 * SAVER compression-motor-only bench test: Teensy 4.1 + Makerbase VESC 75100 V2.
 * Motor: BL60120-2430. UNCOUPLED, secured motor only; NEVER a CPR/linear-axis test.
 * No pressure/PPG/encoder/limit/E-stop GPIO acquisition and no Jetson protocol.
 * Physical E-stop / power isolation is mandatory and independent of this sketch.
 *
 * User's connector photo: #5 COMM, NOT #6 USART2.
 *   VESC TX/SCL -> Teensy 28 (Serial7 RX)
 *   VESC RX/SDA <- Teensy 29 (Serial7 TX)
 *   VESC GND   -- Teensy GND (signal reference, not motor-current return)
 * VESC 5V/3.3V pins are NOT connected to Teensy. Verify UART is 0..3.3V first.
 * Motor phase/Hall wires go to VESC, never to Teensy. Match labels, not colors.
 *
 * Boot/reconnect never starts the motor. Existing user-edited numeric settings
 * are preserved: default +10%, magnitude ceiling 50%, maximum 5000 ms.
 * Actual VESC limits remain unverified. Manual ARM and a healthy link are required.
 *
 * USB Serial Monitor, newline or CRLF:
 * HELP | STATUS | ARM | RUN [signed_pct] [ms] | SET signed_pct | STOP | DISARM | CLEAR_ERR
 * Negative duty requests the opposite configured direction, NOT guaranteed CW/up.
 * Each run needs fresh ARM and zero ERPM/duty reports spanning 250 ms while idle.
 * No immediate direction change during RUN/SET; stop, verify standstill, then re-arm.
 * This is signed DUTY control, not RPM/position/PID or automatic reciprocation.
 * STOP means a VESC SET_CURRENT(0) release request, NOT a mechanical brake,
 * position hold, EN LOW or proof of standstill. UART commands have no drive ACK.
 * A broken UART cannot deliver STOP: configure/test the VESC's own timeout.
 *
 * This sketch does not set VESC motor/app limits, detect the motor, update its
 * firmware or set medical parameters. It only reads FW/values, sets duty, and
 * requests zero current. Duty percent is NOT current in A or percent of 16 A.
 */

#include <Arduino.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "vesc_bench.h"

#if !defined(ARDUINO_TEENSY41)
#error "Select Teensy 4.1 and USB Type Serial"
#endif

namespace {
using namespace saver_compression_bench;

// ===== 기존 사용자 설정값 보존; 역방향은 RUN -10 5000 처럼 음수 사용 =====
// 사용자는 VESC 설정 완료를 보고하고 잠금 해제를 요청함. 실제 설정/배선 검증을 뜻하지 않음.
constexpr bool BENCH_SETUP_CONFIRMED = true; // <<< 사용자 요청으로 해제. ARM/통신/오류 검사는 유지.
constexpr uint8_t MAX_DUTY_PERCENT = 50; // <<< 기존 듀티 크기 상한: 양/음 모두 절댓값 50%.
constexpr uint32_t MAX_RUN_MS = 5000; // <<< 사용자 지정 최대시간: 5000ms = 5초 (5 아님).
constexpr int16_t DEFAULT_DUTY_PERCENT = 10; // <<< RUN 생략 시 +10%. 음수이면 반대 방향.
constexpr uint32_t DEFAULT_RUN_MS = 5000; // <<< RUN 시간 생략 시 5초. 부팅 자동 실행 아님.
constexpr uint32_t VESC_UART_BAUD = 115200; // <<< VESC Tool의 UART baud와 일치시킬 것
// 115200은 이 시험의 통신 설정일 뿐 실제 VESC 출하 설정을 확인한 값은 아닙니다.
// ========================================================================

constexpr uint8_t PIN_CP_UART_RX = 28; // pinmap.h의 Serial7 RX: VESC TX를 받음.
constexpr uint8_t PIN_CP_UART_TX = 29; // pinmap.h의 Serial7 TX: VESC RX로 송신.
constexpr Limits LIMITS = {BENCH_SETUP_CONFIRMED, MAX_DUTY_PERCENT, MAX_RUN_MS};
constexpr uint32_t CONTROL_TX_MS = 50;
constexpr uint32_t QUERY_MS = 100;
constexpr uint32_t PROBE_MS = 1000;
constexpr uint32_t STATUS_MS = 500;
constexpr uint8_t UART_READ_BUDGET = 64;
constexpr uint8_t USB_READ_BUDGET = 32;

static_assert(MAX_DUTY_PERCENT <= 100 && DEFAULT_DUTY_PERCENT >= -100 &&
                  DEFAULT_DUTY_PERCENT <= 100,
              "Duty magnitude range 0..100");
static_assert(MAX_RUN_MS <= ABSOLUTE_MAX_RUN_MS, "Bench ceiling is 30000 ms");
static_assert(DEFAULT_RUN_MS >= 100 && DEFAULT_RUN_MS <= ABSOLUTE_MAX_RUN_MS,
              "Default run duration outside bench range");
static_assert(VESC_UART_BAUD >= 9600 && VESC_UART_BAUD <= 1000000, "Check VESC UART baud");
static_assert(!BENCH_SETUP_CONFIRMED || configured(LIMITS), "Set verified bench limits first");
static_assert(!BENCH_SETUP_CONFIRMED ||
                  (dutyAllowed(DEFAULT_DUTY_PERCENT, LIMITS) && DEFAULT_RUN_MS <= MAX_RUN_MS),
              "Defaults exceed limits");

class CompressionBench {
public:
  void begin() {
    // Only Serial7 is touched. Other motor pins/feedback inputs stay untouched.
    Serial7.setRX(PIN_CP_UART_RX);
    Serial7.setTX(PIN_CP_UART_TX);
    Serial7.begin(VESC_UART_BAUD, SERIAL_8N1);
    Serial.begin(115200); // Native USB monitor setting, independent of VESC baud.
    requestStop();
    lastControlTx_ = millis();
    lastQuery_ = millis() - QUERY_MS;
    lastProbe_ = millis() - PROBE_MS;
  }

  void tick() {
    synchronizeUsb();
    checkSafety();
    pollVesc();
    checkSafety();
    if (usbReady_)
      pollCommands();
    checkSafety();
    serviceTx();
    if (usbReady_ && static_cast<uint32_t>(millis() - lastStatus_) >= STATUS_MS) {
      lastStatus_ = millis();
      status();
    }
  }

private:
  // All mutable state is owned by this bench object, not global g_* variables.
  Control control_;
  PacketParser packets_;
  CommandLine line_;
  Values values_;
  bool usbReady_ = false, firmwareKnown_ = false, valuesKnown_ = false;
  bool fwPending_ = false, valuesPending_ = false, zeroPending_ = true;
  uint8_t fwMajor_ = 0, fwMinor_ = 0;
  uint32_t fwRequestedAt_ = 0, valuesRequestedAt_ = 0;
  uint32_t lastControlTx_ = 0, lastQuery_ = 0, lastProbe_ = 0, lastStatus_ = 0;
  uint32_t droppedLogs_ = 0, badPackets_ = 0, txFailures_ = 0, usbDisconnects_ = 0;
  uint32_t zeroQueued_ = 0, driveQueued_ = 0;
  const char *lastReason_ = "BOOT";

  void emit(const char *format, ...) {
    char text[320];
    va_list args;
    va_start(args, format);
    const int count = vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    if (count < 0 || static_cast<size_t>(count) >= sizeof(text) || !Serial ||
        Serial.availableForWrite() < count + 1) {
      ++droppedLogs_;
      return;
    }
    Serial.write(reinterpret_cast<const uint8_t *>(text), static_cast<size_t>(count));
    Serial.write(static_cast<uint8_t>('\n'));
  }

  bool send(const Frame &frame) {
    // Single producer; never enter HardwareSerial's wait-for-space loop.
    if (Serial7.availableForWrite() < static_cast<int>(frame.length))
      return false;
    return Serial7.write(frame.bytes, frame.length) == frame.length;
  }

  bool queueZero() {
    if (!send(releaseCommand()))
      return false;
    ++zeroQueued_;
    zeroPending_ = false;
    lastControlTx_ = millis();
    return true; // Queued to Teensy UART, NOT an acknowledgement from VESC.
  }

  void requestStop() {
    control_.stop();
    zeroPending_ = true;
    queueZero(); // First attempt BEFORE any log. Pending stop outranks drive/query.
  }

  void fail(const char *why) {
    requestStop();
    control_.trip();
    lastReason_ = why;
    emit("ERROR,%s,drive_command_active=0,stop_pending=%u", why, zeroPending_ ? 1U : 0U);
  }

  void invalidateLink() {
    firmwareKnown_ = false;
    valuesKnown_ = false;
    fwPending_ = false;
    valuesPending_ = false;
    packets_.reset();
    control_.invalidate(); // Does not clear an existing local fault latch.
    lastProbe_ = millis() - PROBE_MS;
  }

  void synchronizeUsb() {
    if (!Serial) {
      if (usbReady_) {
        requestStop();
        control_.trip();
        lastReason_ = "USB_DISCONNECTED";
        ++usbDisconnects_;
        invalidateLink();
      }
      usbReady_ = false;
      line_.reset();
      return;
    }
    if (usbReady_)
      return;
    // New USB session: discard queued commands; never replay a stale ARM/RUN.
    for (uint8_t i = 0; i < USB_READ_BUDGET && Serial.available(); ++i)
      Serial.read();
    if (Serial.available())
      return;
    requestStop();
    invalidateLink();
    line_.reset();
    usbReady_ = true;
    emit("SAVER_COMPRESSION_MOTOR_SINGLE_TEST_READY,build=vesc_uart_bidirectional_v2");
    emit("PINS,RX7=28,TX7=29,vesc_port=COMM,uart_baud=%lu",
         static_cast<unsigned long>(VESC_UART_BAUD));
    emit("SAFETY,config_locked=%u,auto_start=0,stop_is_zero_current_not_brake=1",
         configured(LIMITS) ? 0U : 1U);
    help();
  }

  bool linkReady() const { return firmwareKnown_ && supportedFirmware(fwMajor_); }

  void checkSafety() {
    const uint32_t now = millis();
    const char *reason = control_.check(usbReady_ && static_cast<bool>(Serial), linkReady(), now);
    if (reason) {
      requestStop();
      lastReason_ = reason;
      emit("EVENT,%s,drive_command_active=0,stop_pending=%u", reason, zeroPending_ ? 1U : 0U);
    }
    if (packets_.expire(now))
      badPacket("VESC_PARTIAL_FRAME_TIMEOUT");
    if (fwPending_ && static_cast<uint32_t>(now - fwRequestedAt_) >= VALUES_STALE_MS)
      fwPending_ = false;
    if (valuesPending_ && static_cast<uint32_t>(now - valuesRequestedAt_) >= VALUES_STALE_MS)
      valuesPending_ = false;
    if (valuesKnown_ && !control_.fresh(now)) {
      // Reprobe after lost telemetry. Reception never restores ARM/RUN.
      requestStop();
      invalidateLink();
    }
    if (line_.expire(now))
      fail("USB_COMMAND_TIMEOUT");
  }

  void badPacket(const char *why) {
    ++badPackets_;
    const bool active = control_.armed() || control_.running();
    requestStop();
    if (active)
      control_.trip();
    lastReason_ = why;
    valuesKnown_ = false;
    control_.invalidate();
    // Count noise instead of flooding USB; STATUS exposes reason/counter.
  }

  void pollVesc() {
    for (uint8_t i = 0; i < UART_READ_BUDGET && Serial7.available(); ++i) {
      const PacketResult result = packets_.push(static_cast<uint8_t>(Serial7.read()), millis());
      if (result == PacketResult::Invalid)
        badPacket("VESC_BAD_FRAME");
      else if (result == PacketResult::Ready)
        handlePacket();
    }
  }

  void handlePacket() {
    const uint8_t *p = packets_.data();
    const size_t n = packets_.size();
    const uint32_t now = millis();
    if (p[0] == FW_VERSION) {
      if (!fwPending_ || static_cast<uint32_t>(now - fwRequestedAt_) >= VALUES_STALE_MS)
        return;
      fwPending_ = false;
      // Modern reply includes major/minor + NUL-terminated hardware name.
      bool nameTerminated = false;
      for (size_t i = 3; i < n; ++i)
        if (p[i] == 0) {
          nameTerminated = true;
          break;
        }
      if (n < 5 || !nameTerminated || !supportedFirmware(p[1])) {
        firmwareKnown_ = false;
        fail("UNSUPPORTED_FW_OR_REPLY_CHECK_VESC_TOOL");
        return;
      }
      fwMajor_ = p[1];
      fwMinor_ = p[2];
      firmwareKnown_ = true;
      emit("VESC_FW,major=%u,minor=%u", static_cast<unsigned>(fwMajor_),
           static_cast<unsigned>(fwMinor_));
    } else if (p[0] == GET_VALUES) {
      if (!linkReady() || !valuesPending_ ||
          static_cast<uint32_t>(now - valuesRequestedAt_) >= VALUES_STALE_MS)
        return;
      valuesPending_ = false;
      Values decoded;
      if (!decodeValues(p, n, decoded)) {
        badPacket("VESC_BAD_VALUES_LAYOUT");
        return;
      }
      values_ = decoded;
      valuesKnown_ = true;
      const bool newFault = decoded.fault != 0 && !control_.fault();
      control_.observe(decoded.fault, decoded.erpm, decoded.dutyMilli, now);
      if (decoded.fault) {
        requestStop();
        lastReason_ = "VESC_REPORTED_FAULT";
        if (newFault)
          emit("ERROR,VESC_FAULT,code=%u,drive_command_active=0",
               static_cast<unsigned>(decoded.fault));
      }
    }
    // Other CRC-valid packet types are ignored and never refresh telemetry age.
  }

  void serviceTx() {
    const uint32_t now = millis();
    if (zeroPending_) {
      queueZero();
      return;
    }
    if (static_cast<uint32_t>(now - lastControlTx_) >= CONTROL_TX_MS) {
      if (control_.running()) {
        if (!send(dutyCommand(control_.duty()))) {
          ++txFailures_;
          fail("UART_TX_FULL");
          return;
        }
        ++driveQueued_;
        lastControlTx_ = now;
      } else if (!queueZero()) {
        ++txFailures_;
        zeroPending_ = true;
        return;
      }
    }
    // No ALIVE packets: only actual control updates refresh the VESC watchdog.
    if (!firmwareKnown_) {
      if (!fwPending_ && static_cast<uint32_t>(now - lastProbe_) >= PROBE_MS &&
          send(request(FW_VERSION))) {
        fwRequestedAt_ = now;
        lastProbe_ = now;
        fwPending_ = true;
      }
    } else if (!valuesPending_ && static_cast<uint32_t>(now - lastQuery_) >= QUERY_MS &&
               send(request(GET_VALUES))) {
      lastQuery_ = now;
      valuesRequestedAt_ = now;
      valuesPending_ = true;
    }
  }

  void help() {
    emit("COMMANDS,HELP|STATUS|ARM|RUN [signed_pct] [ms]|SET signed_pct|STOP|DISARM|CLEAR_ERR");
    emit("CONFIG,setup_confirmed=%u,max_duty_pct=%u,max_run_ms=%lu,default_pct=%d,default_ms=%lu",
         BENCH_SETUP_CONFIRMED ? 1U : 0U, static_cast<unsigned>(MAX_DUTY_PERCENT),
         static_cast<unsigned long>(MAX_RUN_MS), static_cast<int>(DEFAULT_DUTY_PERCENT),
         static_cast<unsigned long>(DEFAULT_RUN_MS));
    emit("NOTE,duty_is_not_amperes,erpm_is_not_shaft_rpm,mechanical_stop_not_verified");
    emit("DIRECTION,positive=FWD,negative=REV,SET_cannot_change_direction,stop_then_wait_then_ARM");
    emit("NOTE,ARM_and_RUN_require_zero_ERPM_and_duty_reports_spanning_250ms,not_physical_stop_"
         "proof");
  }

  void status() {
    const uint32_t now = millis();
    const bool fresh = valuesKnown_ && linkReady() && control_.fresh(now);
    emit("STATUS,ms=%lu,config_locked=%u,armed=%u,commanded_running=%u,duty_pct=%d,remaining_ms=%"
         "lu,local_fault=%u,link_ok=%u,last_reason=%s",
         static_cast<unsigned long>(now), configured(LIMITS) ? 0U : 1U, control_.armed() ? 1U : 0U,
         control_.running() ? 1U : 0U, static_cast<int>(control_.duty()),
         static_cast<unsigned long>(control_.remaining(now)), control_.fault() ? 1U : 0U,
         fresh ? 1U : 0U, lastReason_);
    emit("IDLE_CHECK,reported_zero_stable=%u,standstill_verified=0",
         fresh && control_.reportedZeroStable(now) ? 1U : 0U);
    if (fresh) {
      emit("VESC,values_ok=1,age_ms=%lu,input_V=%.1f,motor_current_A=%.2f,input_current_A=%.2f,"
           "duty_pct=%.1f,erpm=%ld,fet_temp_C=%.1f,fault_code=%u",
           static_cast<unsigned long>(control_.age(now)), values_.inputDeciV / 10.0,
           values_.motorCentiA / 100.0, values_.inputCentiA / 100.0, values_.dutyMilli / 10.0,
           static_cast<long>(values_.erpm), values_.fetDeciC / 10.0,
           static_cast<unsigned>(values_.fault));
    } else
      emit("VESC,values_ok=0"); // No stale values or fake zero measurements.
    emit("DIAG,stop_pending=%u,zero_current_queued=%lu,drive_queued=%lu,bad_packets=%lu,tx_"
         "failures=%lu,usb_disconnects=%lu,dropped_logs=%lu",
         zeroPending_ ? 1U : 0U, static_cast<unsigned long>(zeroQueued_),
         static_cast<unsigned long>(driveQueued_), static_cast<unsigned long>(badPackets_),
         static_cast<unsigned long>(txFailures_), static_cast<unsigned long>(usbDisconnects_),
         static_cast<unsigned long>(droppedLogs_));
  }

  void pollCommands() {
    for (uint8_t i = 0; i < USB_READ_BUDGET && Serial.available(); ++i) {
      checkSafety();
      const LineResult result = line_.push(static_cast<char>(Serial.read()), millis());
      if (result == LineResult::Invalid)
        fail("USB_BAD_OR_LONG_COMMAND");
      if (result == LineResult::Ready) {
        handleCommand(line_.data());
        line_.reset();
      }
    }
  }

  void handleCommand(char *text) {
    char *context = nullptr;
    char *cmd = strtok_r(text, " \t", &context);
    if (!cmd)
      return;
    for (char *p = cmd; *p; ++p)
      *p = static_cast<char>(toupper(static_cast<unsigned char>(*p)));
    char *arg1 = strtok_r(nullptr, " \t", &context);
    char *arg2 = strtok_r(nullptr, " \t", &context);
    char *extra = strtok_r(nullptr, " \t", &context);
    const uint32_t now = millis();
    const char *error = nullptr;

    if ((!strcmp(cmd, "STOP") || !strcmp(cmd, "DISARM")) && !arg1) {
      requestStop();
      lastReason_ = "USER_STOP";
      emit("OK,STOP_REQUESTED,zero_current_queued=%u,standstill_verified=0",
           zeroPending_ ? 0U : 1U);
    } else if (!strcmp(cmd, "STATUS") && !arg1)
      status();
    else if (!strcmp(cmd, "HELP") && !arg1)
      help();
    else if (!strcmp(cmd, "ARM") && !arg1) {
      const bool wasRunning = control_.running();
      requestStop();
      if (wasRunning) {
        fail("ARM_WHILE_RUNNING");
        return;
      }
      error = control_.arm(LIMITS, linkReady() && usbReady_ && !zeroPending_, now);
      if (!error) {
        lastReason_ = "ARMED";
        emit("OK,ARMED,run_within_ms=10000");
      }
    } else if (!strcmp(cmd, "CLEAR_ERR") && !arg1) {
      if (control_.running()) {
        fail("CLEAR_REQUIRES_STOP");
        return;
      }
      requestStop();
      error = control_.clear(linkReady(), now);
      if (!error) {
        lastReason_ = "LOCAL_FAULT_CLEARED";
        emit("OK,LOCAL_FAULT_CLEARED,armed=0");
      }
    } else if (!strcmp(cmd, "RUN")) {
      int32_t duty = DEFAULT_DUTY_PERCENT;
      uint32_t duration = DEFAULT_RUN_MS;
      if (extra || (arg1 && !parseDutyPercent(arg1, duty)) ||
          (arg2 && !parseUnsigned(arg2, duration))) {
        fail("BAD_RUN_ARGUMENTS");
        return;
      }
      error =
          control_.start(duty, duration, LIMITS, linkReady() && usbReady_ && !zeroPending_, now);
      if (!error) {
        // The UART command is not a VESC execution ACK and not proof of rotation.
        lastControlTx_ = now - CONTROL_TX_MS;
        serviceTx();
        if (!control_.running())
          return;
        lastReason_ = "RUN_REQUESTED";
        emit("OK,RUN_REQUESTED,duty_pct=%ld,duration_ms=%lu,direction=%s,rotation_verified=0",
             static_cast<long>(duty), static_cast<unsigned long>(duration),
             duty < 0 ? "REV" : "FWD");
      }
    } else if (!strcmp(cmd, "SET")) {
      int32_t duty = 0;
      if (!arg1 || arg2 || !parseDutyPercent(arg1, duty)) {
        fail("BAD_SET_ARGUMENTS");
        return;
      }
      error = control_.set(duty, LIMITS, now);
      if (!error) {
        if (!duty)
          requestStop();
        else {
          lastControlTx_ = now - CONTROL_TX_MS;
          serviceTx();
        }
        if (duty && !control_.running())
          return;
        lastReason_ = duty ? "DUTY_CHANGED" : "USER_STOP";
        emit("OK,SET,duty_pct=%ld,deadline_extended=0", static_cast<long>(duty));
      }
    } else
      error = "UNKNOWN_OR_MALFORMED_COMMAND";

    if (error)
      fail(error);
  }
};

CompressionBench &bench() {
  static CompressionBench instance;
  return instance;
}
} // namespace

void setup() { bench().begin(); }
void loop() { bench().tick(); }
