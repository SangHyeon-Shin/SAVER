/*
 * SAVER simultaneous vacuum + compression motor bench test.
 * Research prototype bench use only. No Jetson protocol and no automatic start.
 *
 * Scope:
 *   - Vacuum motor 1: VSP24 / RUN25 / EN26 / ERR27
 *   - Vacuum motor 2: VSP33 / RUN32 / EN37 / ERR36
 *   - Compression VESC COMM UART: Serial7 RX28 / TX29
 *   - Pressure, linear position, current, encoder and limit inputs are NOT read.
 *
 * Because position/limit feedback is deliberately disabled, the compression motor
 * must be mechanically uncoupled from the linear axis and secured. This is not a
 * CPR or suction-retention test. Physical E-stop and independent power removal are
 * mandatory. A UART STOP is not a brake and cannot work after a broken UART link.
 *
 * Serial Monitor: 115200, Newline or Both NL & CR.
 *   HELP
 *   STATUS
 *   ARM
 *   RUN_ALL vac1_pct vac2_pct cp_signed_pct duration_ms
 *   ROUNDTRIP vac1_pct vac2_pct cp_first_signed_pct leg_ms
 *   STOP
 *   DISARM
 *   CLEAR_ERR
 *
 * First automatic round trip: ROUNDTRIP 6 6 10 500
 * Negative compression duty selects the opposite configured VESC direction.
 */

#include <Arduino.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "combined_bench_logic.h"
#include "vesc_bench.h"

#if !defined(ARDUINO_TEENSY41)
#error "Select Teensy 4.1 and USB Type Serial"
#endif

namespace {
using namespace saver_combined_bench;
using namespace saver_compression_bench;

// Current repository pin map. The source guard compares every value to teensy/pinmap.h.
constexpr uint8_t PIN_CP_UART_RX = 28;
constexpr uint8_t PIN_CP_UART_TX = 29;
constexpr uint8_t PIN_VAC1_VSP = 24;
constexpr uint8_t PIN_VAC1_RUN = 25;
constexpr uint8_t PIN_VAC1_EN = 26;
constexpr uint8_t PIN_VAC1_ERR = 27;
constexpr uint8_t PIN_VAC2_VSP = 33;
constexpr uint8_t PIN_VAC2_RUN = 32;
constexpr uint8_t PIN_VAC2_EN = 37;
constexpr uint8_t PIN_VAC2_ERR = 36;

constexpr uint8_t VSP_PINS[2] = {PIN_VAC1_VSP, PIN_VAC2_VSP};
constexpr uint8_t RUN_PINS[2] = {PIN_VAC1_RUN, PIN_VAC2_RUN};
constexpr uint8_t EN_PINS[2] = {PIN_VAC1_EN, PIN_VAC2_EN};
constexpr uint8_t ERR_PINS[2] = {PIN_VAC1_ERR, PIN_VAC2_ERR};

// Conservative first simultaneous-run envelope. These are local bench limits,
// not medical settings or verified component ratings.
constexpr RunAllLimits RUN_LIMITS = {22, 50, 2000};
constexpr Limits VESC_LIMITS = {true, RUN_LIMITS.maxCompressionPercent, RUN_LIMITS.maxRunMs};
constexpr uint8_t EXTERNAL_SUPPLY_CURRENT_LIMIT_A = 10;
constexpr uint32_t VESC_UART_BAUD = 115200;
constexpr float VACUUM_PWM_HZ = 10000.0f;
constexpr uint32_t CONTROL_TX_MS = 50;
constexpr uint32_t QUERY_MS = 100;
constexpr uint32_t PROBE_MS = 1000;
constexpr uint32_t STATUS_MS = 500;
constexpr uint32_t REVERSAL_DWELL_MS = 500;
constexpr uint32_t REVERSAL_TIMEOUT_MS = 2000;
constexpr uint8_t UART_READ_BUDGET = 64;
constexpr uint8_t USB_READ_BUDGET = 32;

static_assert(configured(VESC_LIMITS), "Combined VESC bench limits must be configured");
static_assert(RUN_LIMITS.maxVacuumPercent <= 100, "Vacuum duty range");
static_assert(RUN_LIMITS.maxRunMs <= ABSOLUTE_MAX_RUN_MS, "Combined duration ceiling");
static_assert(VESC_UART_BAUD >= 9600 && VESC_UART_BAUD <= 1000000, "Check VESC UART baud");

class CombinedBench {
public:
  void begin() {
    for (uint8_t i = 0; i < 2; ++i) {
      digitalWriteFast(EN_PINS[i], LOW);
      pinMode(EN_PINS[i], OUTPUT);
      digitalWriteFast(VSP_PINS[i], LOW);
      pinMode(VSP_PINS[i], OUTPUT);
    }
    analogWriteResolution(8);
    for (uint8_t i = 0; i < 2; ++i) {
      analogWriteFrequency(VSP_PINS[i], VACUUM_PWM_HZ);
      pinMode(ERR_PINS[i], INPUT);
      pinMode(RUN_PINS[i], INPUT);
    }

    Serial7.setRX(PIN_CP_UART_RX);
    Serial7.setTX(PIN_CP_UART_TX);
    Serial7.begin(VESC_UART_BAUD, SERIAL_8N1);
    Serial.begin(115200);

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
  Control control_;
  VacuumInterlock vacuum_;
  PacketParser packets_;
  CommandLine line_;
  Values values_;
  bool usbReady_ = false, firmwareKnown_ = false, valuesKnown_ = false;
  bool fwPending_ = false, valuesPending_ = false, zeroPending_ = true;
  uint8_t fwMajor_ = 0, fwMinor_ = 0;
  uint8_t vac1Duty_ = 0, vac2Duty_ = 0;
  RoundTripPhase roundTripPhase_ = RoundTripPhase::Idle;
  int16_t firstLegDuty_ = 0;
  uint32_t legDurationMs_ = 0, phaseStartedAt_ = 0;
  uint32_t fwRequestedAt_ = 0, valuesRequestedAt_ = 0;
  uint32_t lastControlTx_ = 0, lastQuery_ = 0, lastProbe_ = 0, lastStatus_ = 0;
  uint32_t droppedLogs_ = 0, badPackets_ = 0, txFailures_ = 0, usbDisconnects_ = 0;
  uint32_t zeroQueued_ = 0, driveQueued_ = 0;
  const char *lastReason_ = "BOOT";

  void emit(const char *format, ...) {
    char text[384];
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

  void writeVacuumOutputsOff() {
    for (uint8_t i = 0; i < 2; ++i) {
      analogWrite(VSP_PINS[i], 0);
      digitalWriteFast(EN_PINS[i], LOW);
      digitalWriteFast(VSP_PINS[i], LOW);
      pinMode(VSP_PINS[i], OUTPUT);
    }
    vac1Duty_ = 0;
    vac2Duty_ = 0;
  }

  bool send(const Frame &frame) {
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
    return true;
  }

  void requestStop() {
    // GPIO motor-off commands and the VESC zero request precede every report.
    writeVacuumOutputsOff();
    control_.stop();
    zeroPending_ = true;
    queueZero();
    roundTripPhase_ = RoundTripPhase::Idle;
    firstLegDuty_ = 0;
    legDurationMs_ = 0;
  }

  void requestCompressionStop() {
    control_.stop();
    zeroPending_ = true;
    queueZero();
  }

  void fail(const char *why) {
    requestStop();
    control_.trip();
    lastReason_ = why;
    emit("ERROR,%s,all_motor_commands_off=1,vesc_stop_pending=%u", why,
         zeroPending_ ? 1U : 0U);
  }

  bool linkReady() const { return firmwareKnown_ && supportedFirmware(fwMajor_); }

  void invalidateLink() {
    firmwareKnown_ = false;
    valuesKnown_ = false;
    fwPending_ = false;
    valuesPending_ = false;
    packets_.reset();
    control_.invalidate();
    lastProbe_ = millis() - PROBE_MS;
  }

  void observeVacuumInputs(uint32_t now) {
    const uint8_t newlyLatched =
        vacuum_.observe(digitalRead(PIN_VAC1_ERR) == LOW, digitalRead(PIN_VAC2_ERR) == LOW, now);
    if (newlyLatched) {
      requestStop();
      control_.trip();
      lastReason_ = "VACUUM_ERR_LOW";
      emit("FAULT,VACUUM_ERR_LOW,motor_mask=%u,all_motor_commands_off=1",
           static_cast<unsigned>(newlyLatched));
    }
  }

  const char *phaseName() const {
    switch (roundTripPhase_) {
    case RoundTripPhase::FirstLeg: return "FIRST_LEG";
    case RoundTripPhase::WaitForZero: return "WAIT_FOR_ZERO";
    case RoundTripPhase::ReturnLeg: return "RETURN_LEG";
    default: return "IDLE";
    }
  }

  void serviceRoundTrip(uint32_t now) {
    if (roundTripPhase_ == RoundTripPhase::Idle)
      return;
    const uint32_t elapsed = static_cast<uint32_t>(now - phaseStartedAt_);
    const bool zeroReady = !zeroPending_ && linkReady() && usbReady_ &&
                           control_.reportedZeroStable(now);
    const RoundTripAction action = nextRoundTripAction(
        roundTripPhase_, elapsed, legDurationMs_, zeroReady, REVERSAL_DWELL_MS,
        REVERSAL_TIMEOUT_MS);
    if (action == RoundTripAction::StopForDwell) {
      requestCompressionStop();
      roundTripPhase_ = RoundTripPhase::WaitForZero;
      phaseStartedAt_ = now;
      lastReason_ = "ROUNDTRIP_WAIT_FOR_ZERO";
      emit("EVENT,ROUNDTRIP_DWELL_STARTED,dwell_min_ms=%lu,zero_report_required=1,vacuum_continues=1",
           static_cast<unsigned long>(REVERSAL_DWELL_MS));
      return;
    }
    if (action == RoundTripAction::ZeroTimeout) {
      fail("ROUNDTRIP_ZERO_TIMEOUT");
      return;
    }
    if (action == RoundTripAction::StartReturn) {
      const bool internalReady = zeroReady;
      const char *error = control_.arm(VESC_LIMITS, internalReady, now);
      if (!error)
        error = control_.start(oppositeDuty(firstLegDuty_), legDurationMs_, VESC_LIMITS,
                               internalReady, now);
      if (error) {
        fail(error);
        return;
      }
      roundTripPhase_ = RoundTripPhase::ReturnLeg;
      phaseStartedAt_ = now;
      lastControlTx_ = now - CONTROL_TX_MS;
      serviceTx();
      if (!control_.running())
        return;
      lastReason_ = "ROUNDTRIP_RETURN_STARTED";
      emit("EVENT,ROUNDTRIP_RETURN_STARTED,cp_duty_pct=%d,leg_ms=%lu,vacuum_continues=1",
           static_cast<int>(oppositeDuty(firstLegDuty_)),
           static_cast<unsigned long>(legDurationMs_));
      return;
    }
    if (action == RoundTripAction::Complete) {
      requestStop();
      lastReason_ = "ROUNDTRIP_COMPLETE";
      emit("EVENT,ROUNDTRIP_COMPLETE,all_motor_commands_off=1,position_return_verified=0");
    }
  }

  void checkSafety() {
    const uint32_t now = millis();
    observeVacuumInputs(now);
    serviceRoundTrip(now);
    const char *reason = control_.check(usbReady_ && static_cast<bool>(Serial), linkReady(), now);
    if (reason) {
      requestStop();
      lastReason_ = reason;
      emit("EVENT,%s,all_motor_commands_off=1,vesc_stop_pending=%u", reason,
           zeroPending_ ? 1U : 0U);
    }
    if (packets_.expire(now))
      badPacket("VESC_PARTIAL_FRAME_TIMEOUT");
    if (fwPending_ && static_cast<uint32_t>(now - fwRequestedAt_) >= VALUES_STALE_MS)
      fwPending_ = false;
    if (valuesPending_ && static_cast<uint32_t>(now - valuesRequestedAt_) >= VALUES_STALE_MS)
      valuesPending_ = false;
    if (valuesKnown_ && !control_.fresh(now)) {
      requestStop();
      invalidateLink();
      lastReason_ = "VESC_VALUES_STALE";
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
    for (uint8_t i = 0; i < USB_READ_BUDGET && Serial.available(); ++i)
      Serial.read();
    if (Serial.available())
      return;
    requestStop();
    invalidateLink();
    line_.reset();
    usbReady_ = true;
    emit("SAVER_VACUUM_COMPRESSION_COMBINED_TEST_READY,build=vacuum2_vesc_combined_v1");
    emit("PINS,VAC1=VSP24/RUN25/EN26/ERR27,VAC2=VSP33/RUN32/EN37/ERR36,VESC=RX28/TX29");
    emit("FEATURES,pressure_enabled=0,linear_enabled=0,current_enabled=0,auto_start=0");
    emit("POWER,external_supply_limit_A=%u,firmware_total_current_monitor=0",
         static_cast<unsigned>(EXTERNAL_SUPPLY_CURRENT_LIMIT_A));
    help();
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
          emit("FAULT,VESC_FAULT,code=%u,all_motor_commands_off=1",
               static_cast<unsigned>(decoded.fault));
      }
    }
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

  void startVacuum(uint8_t vacuum1, uint8_t vacuum2) {
    vac1Duty_ = vacuum1;
    vac2Duty_ = vacuum2;
    digitalWriteFast(PIN_VAC1_EN, HIGH);
    digitalWriteFast(PIN_VAC2_EN, HIGH);
    analogWrite(PIN_VAC1_VSP, saver_combined_bench::dutyToPwm(vacuum1));
    analogWrite(PIN_VAC2_VSP, saver_combined_bench::dutyToPwm(vacuum2));
  }

  void help() {
    emit("COMMANDS,HELP|STATUS|ARM|RUN_ALL vac1_pct vac2_pct cp_signed_pct duration_ms|ROUNDTRIP vac1_pct vac2_pct cp_first_signed_pct leg_ms|STOP|DISARM|CLEAR_ERR");
    emit("LIMITS,vacuum_pct=1..%u,compression_pct=-%u..-1_or_1..%u,duration_ms=100..%lu",
         static_cast<unsigned>(RUN_LIMITS.maxVacuumPercent),
         static_cast<unsigned>(RUN_LIMITS.maxCompressionPercent),
         static_cast<unsigned>(RUN_LIMITS.maxCompressionPercent),
         static_cast<unsigned long>(RUN_LIMITS.maxRunMs));
    emit("EXAMPLE_FIRST,ROUNDTRIP 6 6 10 500");
    emit("ROUNDTRIP,first_leg_then_zero_for_500ms_and_reported_zero_250ms_then_opposite_leg,position_return_verified=0");
    emit("NOTE,raise_vacuum_in_steps_6_then_10_then_22_only_while_external_total_current_stays_below_10A");
    emit("WARNING,uncoupled_compression_motor_only,no_position_or_limit_feedback,no_pressure_interlock");
    emit("WARNING,STOP_is_not_mechanical_brake,physical_estop_and_power_isolation_required");
  }

  void status() {
    const uint32_t now = millis();
    const bool fresh = valuesKnown_ && linkReady() && control_.fresh(now);
    emit("STATUS,ms=%lu,armed=%u,commanded_running=%u,remaining_ms=%lu,roundtrip_phase=%s,link_ok=%u,vac_ready=%u,last_reason=%s",
         static_cast<unsigned long>(now), control_.armed() ? 1U : 0U,
         control_.running() ? 1U : 0U, static_cast<unsigned long>(control_.remaining(now)),
         phaseName(), fresh ? 1U : 0U, vacuum_.ready(now) ? 1U : 0U, lastReason_);
    emit("VAC,id=1,pwm_pct=%u,en=%u,err_raw=%u,err_fault=%u,run_raw=%u",
         static_cast<unsigned>(vac1Duty_), static_cast<unsigned>(digitalRead(PIN_VAC1_EN)),
         static_cast<unsigned>(digitalRead(PIN_VAC1_ERR)), vacuum_.fault(0) ? 1U : 0U,
         static_cast<unsigned>(digitalRead(PIN_VAC1_RUN)));
    emit("VAC,id=2,pwm_pct=%u,en=%u,err_raw=%u,err_fault=%u,run_raw=%u",
         static_cast<unsigned>(vac2Duty_), static_cast<unsigned>(digitalRead(PIN_VAC2_EN)),
         static_cast<unsigned>(digitalRead(PIN_VAC2_ERR)), vacuum_.fault(1) ? 1U : 0U,
         static_cast<unsigned>(digitalRead(PIN_VAC2_RUN)));
    emit("CP,duty_pct=%d,local_fault=%u,reported_zero_stable=%u,standstill_verified=0",
         static_cast<int>(control_.duty()), control_.fault() ? 1U : 0U,
         fresh && control_.reportedZeroStable(now) ? 1U : 0U);
    if (fresh)
      emit("VESC,values_ok=1,input_V=%.1f,motor_current_A=%.2f,input_current_A=%.2f,duty_pct=%.1f,erpm=%ld,fet_temp_C=%.1f,fault_code=%u",
           values_.inputDeciV / 10.0, values_.motorCentiA / 100.0,
           values_.inputCentiA / 100.0, values_.dutyMilli / 10.0,
           static_cast<long>(values_.erpm), values_.fetDeciC / 10.0,
           static_cast<unsigned>(values_.fault));
    else
      emit("VESC,values_ok=0");
    emit("DIAG,vesc_stop_pending=%u,bad_packets=%lu,tx_failures=%lu,usb_disconnects=%lu,dropped_logs=%lu",
         zeroPending_ ? 1U : 0U, static_cast<unsigned long>(badPackets_),
         static_cast<unsigned long>(txFailures_), static_cast<unsigned long>(usbDisconnects_),
         static_cast<unsigned long>(droppedLogs_));
  }

  bool parseRequest(char *arg1, char *arg2, char *arg3, char *arg4, char *extra,
                    RunAllRequest &request) {
    uint32_t vacuum1 = 0, vacuum2 = 0, duration = 0;
    int32_t compression = 0;
    if (!arg1 || !arg2 || !arg3 || !arg4 || extra || !parseUnsigned(arg1, vacuum1) ||
        !parseUnsigned(arg2, vacuum2) || !parseDutyPercent(arg3, compression) ||
        !parseUnsigned(arg4, duration)) {
      fail("BAD_RUN_ARGUMENTS");
      return false;
    }
    if (vacuum1 > UINT8_MAX || vacuum2 > UINT8_MAX) {
      fail("VACUUM_DUTY_OUT_OF_RANGE");
      return false;
    }
    request = {static_cast<uint8_t>(vacuum1), static_cast<uint8_t>(vacuum2),
               static_cast<int16_t>(compression), duration};
    const char *error = validate(request, RUN_LIMITS);
    if (error) {
      fail(error);
      return false;
    }
    return true;
  }

  bool startRequest(const RunAllRequest &request, uint32_t now) {
    observeVacuumInputs(now);
    if (!vacuum_.ready(now)) {
      fail("VACUUM_ERR_NOT_HIGH_FOR_250MS");
      return false;
    }
    const char *error = control_.start(request.compressionPercent, request.durationMs, VESC_LIMITS,
                                       linkReady() && usbReady_ && !zeroPending_, now);
    if (error) {
      fail(error);
      return false;
    }
    lastControlTx_ = now - CONTROL_TX_MS;
    serviceTx();
    if (!control_.running())
      return false;
    startVacuum(request.vacuum1Percent, request.vacuum2Percent);
    return true;
  }

  void runAll(char *arg1, char *arg2, char *arg3, char *arg4, char *extra) {
    RunAllRequest request = {};
    if (!parseRequest(arg1, arg2, arg3, arg4, extra, request))
      return;
    const uint32_t now = millis();
    if (!startRequest(request, now))
      return;
    lastReason_ = "RUN_ALL_REQUESTED";
    emit("OK,RUN_ALL_REQUESTED,vac1_pct=%lu,vac2_pct=%lu,cp_duty_pct=%ld,duration_ms=%lu,rotation_verified=0",
         static_cast<unsigned long>(request.vacuum1Percent),
         static_cast<unsigned long>(request.vacuum2Percent),
         static_cast<long>(request.compressionPercent),
         static_cast<unsigned long>(request.durationMs));
  }

  void roundTrip(char *arg1, char *arg2, char *arg3, char *arg4, char *extra) {
    RunAllRequest request = {};
    if (!parseRequest(arg1, arg2, arg3, arg4, extra, request))
      return;
    const uint32_t now = millis();
    if (!startRequest(request, now))
      return;
    roundTripPhase_ = RoundTripPhase::FirstLeg;
    firstLegDuty_ = request.compressionPercent;
    legDurationMs_ = request.durationMs;
    phaseStartedAt_ = now;
    lastReason_ = "ROUNDTRIP_FIRST_STARTED";
    emit("OK,ROUNDTRIP_STARTED,vac1_pct=%u,vac2_pct=%u,first_cp_duty_pct=%d,return_cp_duty_pct=%d,leg_ms=%lu,position_return_verified=0",
         static_cast<unsigned>(request.vacuum1Percent),
         static_cast<unsigned>(request.vacuum2Percent),
         static_cast<int>(request.compressionPercent),
         static_cast<int>(oppositeDuty(request.compressionPercent)),
         static_cast<unsigned long>(request.durationMs));
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
    char *arg3 = strtok_r(nullptr, " \t", &context);
    char *arg4 = strtok_r(nullptr, " \t", &context);
    char *extra = strtok_r(nullptr, " \t", &context);
    const uint32_t now = millis();
    const char *error = nullptr;

    if ((!strcmp(cmd, "STOP") || !strcmp(cmd, "DISARM")) && !arg1) {
      requestStop();
      lastReason_ = "USER_STOP";
      emit("OK,ALL_STOP_REQUESTED,vesc_stop_pending=%u,standstill_verified=0",
           zeroPending_ ? 1U : 0U);
    } else if (!strcmp(cmd, "STATUS") && !arg1)
      status();
    else if (!strcmp(cmd, "HELP") && !arg1)
      help();
    else if (!strcmp(cmd, "ARM") && !arg1) {
      const bool wasRunning = control_.running();
      requestStop();
      observeVacuumInputs(now);
      if (wasRunning) {
        fail("ARM_WHILE_RUNNING");
        return;
      }
      if (!vacuum_.ready(now))
        error = "VACUUM_ERR_NOT_HIGH_FOR_250MS";
      else
        error = control_.arm(VESC_LIMITS, linkReady() && usbReady_ && !zeroPending_, now);
      if (!error) {
        lastReason_ = "ARMED";
        emit("OK,ARMED,run_within_ms=10000,all_motor_commands_off=1");
      }
    } else if (!strcmp(cmd, "CLEAR_ERR") && !arg1) {
      requestStop();
      observeVacuumInputs(now);
      error = control_.clear(linkReady(), now);
      if (!error)
        error = vacuum_.clear(now);
      if (!error) {
        lastReason_ = "LOCAL_FAULTS_CLEARED";
        emit("OK,LOCAL_FAULTS_CLEARED,armed=0");
      }
    } else if (!strcmp(cmd, "RUN_ALL")) {
      runAll(arg1, arg2, arg3, arg4, extra);
      return;
    } else if (!strcmp(cmd, "ROUNDTRIP")) {
      roundTrip(arg1, arg2, arg3, arg4, extra);
      return;
    } else
      error = "UNKNOWN_OR_MALFORMED_COMMAND";

    if (error)
      fail(error);
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
};

CombinedBench &bench() {
  static CombinedBench instance;
  return instance;
}
} // namespace

void setup() { bench().begin(); }
void loop() { bench().tick(); }
