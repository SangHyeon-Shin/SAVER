#ifndef SAVER_DEMO_APP_H
#define SAVER_DEMO_APP_H
#include <EEPROM.h>
#include "../../pinmap.h"
#include "bindings.h"
#include "wire_protocol.h"
#include "compression_module.h"
#include "vacuum_module.h"
#include "ppg_module.h"
#include "combined_trial.h"
namespace saver_demo {
class App {
public:
  App() : cp_(log_), vac_(log_), ppg_(log_) {}
  void begin() {
    Serial.begin(115200);
    analogReadResolution(12);
    analogReadAveraging(16);
    const uint8_t disabled[] = {PIN_DRIVE1_RPWM, PIN_DRIVE1_LPWM, PIN_DRIVE1_EN, PIN_DRIVE2_RPWM,
                                PIN_DRIVE2_LPWM, PIN_DRIVE2_EN,   PIN_FAN1_PWM,  PIN_FAN2_PWM};
    for (uint8_t pin : disabled) {
      digitalWriteFast(pin, LOW);
      pinMode(pin, OUTPUT);
    }
    pinMode(PIN_GYRO_UART_RX, INPUT);
    pinMode(PIN_GYRO_UART_TX, INPUT);
    vac_.begin();
    cp_.begin();
    cp_.enter();
    cp_.loadCalibration(EEPROM);
    session_.boot = nextBoot(EEPROM);
    connected_ = bool(Serial);
    log_.emit("READY,manual_placement_bench_only=1,proto=1.1,auto_HOME=UNAVAILABLE");
    if (!session_.boot)
      trip(nullptr, "BOOT_JOURNAL_UNAVAILABLE");
  }
  void tick() {
    if (bool(Serial) != connected_) {
      cp_.stop();
      cancelCombined();
      combinedPower_.invalidate();
      prepared_ = pausePending_ = false;
      owner_ = Owner::None;
      connected_ = bool(Serial);
      len_ = 0;
      discard_ = carriage_ = false;
      resync_ = true;
      if (!faulted())
        state_ = vac_.requested() ? "VACUUM_HOLD" : "IDLE";
      log_.clear();
      log_.emit("USB_%s,compression_zero_requested=1,standstill_verified=0",
                connected_ ? "CONNECTED" : "DISCONNECTED");
    }
    if (connected_ && resync_) {
      for (unsigned i = 0; i < 512 && Serial.available(); ++i)
        Serial.read();
      if (!Serial.available()) {
        resync_ = false;
        log_.emit("LINK_READY,SEND_FRESH_COMMAND=1");
      }
    } else if (connected_)
      poll();
    cp_.tick();
    const bool hadVacuum = vac_.requested() || vac_.active();
    vac_.tick();
    if (ppgOn_)
      ppg_.tick();
    if (cp_.deadlineMissed())
      cp_.stopForDeadline();
    if (cp_.fault() && !faulted())
      trip(compressionCode(cp_.faultReason()), cp_.faultReason());
    if (hadVacuum && vac_.fault() && !faulted()) {
      const char *why = vac_.faultReason();
      const char *code = !strcmp(why, "BLOWER_ERR_LOW")         ? "2-VU-007"
                         : !strcmp(why, "VACUUM_LOST")          ? "3-VU-006"
                         : !strcmp(why, "BLOWER_ON_TIME_LIMIT") ? "2-VU-004"
                                                                : nullptr;
      trip(code, why);
    }
    if (selectable::cfg::ESTOP_AUX_INSTALLED &&
        digitalRead(PIN_ESTOP) == selectable::cfg::ESTOP_ACTIVE_LEVEL) {
      cp_.stop();
      cancelCombined();
      prepared_ = pausePending_ = false;
      state_ = "ESTOP";
    }
    if (owner_ == Owner::Remote && uint32_t(millis() - lastJson_) >= 2000 &&
        (cp_.active() || !strcmp(state_, "ARMED") || !strcmp(state_, "CPR_READY") ||
         !strcmp(state_, "CPR_PAUSED")))
      trip("3-CM-010", "COMM_LOST_IMMEDIATE_BENCH_STOP");
    serviceCombined();
    if (pausePending_) {
      if (!cp_.standstill())
        restSeen_ = false;
      else {
        if (!restSeen_) {
          restSeen_ = true;
          restAt_ = millis();
        }
        if (uint32_t(millis() - restAt_) >= 250) {
          pausePending_ = false;
          state_ = "CPR_PAUSED";
          log_.emit("PAUSE_CONFIRMED,standstill_verified=1");
        }
      }
    }
    if (vac_.holding() && !faulted() && !cp_.active() && !prepared_ &&
        (!strcmp(state_, "IDLE") || !strcmp(state_, "ARMED")))
      state_ = "VACUUM_HOLD";
    if (localRelease_ && vac_.released()) {
      localRelease_ = false;
      rearmLock_ = false;
      if (!faulted())
        state_ = "IDLE";
    }
    if (!cp_.active() && !pausePending_ && !strcmp(state_, "CPR_RUNNING") && !cp_.fault()) {
      prepared_ = false;
      state_ = vac_.requested() ? "VACUUM_HOLD" : "IDLE";
    }
    if (vacSeq_ && vac_.holding()) {
      event("VACUUM_OK", vacSeq_, vacAid_);
      vacSeq_ = 0;
      if (!faulted() && !cp_.active())
        state_ = "VACUUM_HOLD";
    }
    if (releaseSeq_ && vac_.released()) {
      event("VACUUM_RELEASED", releaseSeq_, releaseAid_);
      releaseSeq_ = 0;
      rearmLock_ = false;
      if (!faulted())
        state_ = "IDLE";
    }
    if ((!quiet_ || owner_ == Owner::Remote) && uint32_t(millis() - telAt_) >= period_) {
      telAt_ = millis();
      telemetry();
    }
    cp_.serviceCheck();
    cp_.serviceStop();
    log_.flush();
  }

private:
  enum class Owner { None, Local, Remote };
  bool faulted() const { return !strcmp(state_, "FAULT") || !strcmp(state_, "ESTOP"); }
  static const char *compressionCode(const char *why) {
    if (!strcmp(why, "MOTOR_CURRENT_LIMIT") || !strcmp(why, "INPUT_CURRENT_LIMIT"))
      return "3-CU-021";
    if (strstr(why, "LINEAR") || strstr(why, "SOFT_TRAVEL") || strstr(why, "SOFT_LIMIT"))
      return "3-TU-010";
    if (strstr(why, "LOADCELL") || strstr(why, "NAU7802"))
      return "3-TU-009";
    if (!strcmp(why, "LIMIT_ACTIVE"))
      return "3-CU-022";
    if (!strcmp(why, "FOLLOWING_ERROR") || !strcmp(why, "END_POSITION_ERROR"))
      return "2-CU-012";
    return nullptr; // Unagreed codes remain local diagnostics; never mislabel a fault.
  }
  void trip(const char *code, const char *why) {
    cp_.stop();
    cancelCombined();
    prepared_ = pausePending_ = false;
    if (!strcmp(state_, "ESTOP"))
      return;
    const bool hadFault = faulted() && faultWhy_[0];
    state_ = "FAULT";
    if (hadFault)
      return;
    snprintf(fault_, sizeof(fault_), "%s", code ? code : "");
    snprintf(faultWhy_, sizeof(faultWhy_), "%s", why);
    if (!fault_[0]) {
      log_.emit("FAULT,%s,protocol_code=UNASSIGNED,compression_stopped=1", why);
      return;
    }
    char b[384];
    snprintf(b, sizeof(b),
             "{\"v\":1,\"t\":\"err\",\"ms\":%lu,\"code\":\"%s\",\"sev\":%d,\"latched\":true,"
             "\"why\":\"%s\"}",
             (unsigned long)millis(), code, code[0] == '3' ? 3 : 2, why);
    log_.raw(b);
  }
  void stopOutput(const char *cmd) {
    if (strcmp(cmd, "DRIVE_STOP")) {
      cancelCombined();
      cp_.stop();
    }
  }
  void stopState(const char *cmd) {
    if (!strcmp(state_, "FAULT") || !strcmp(state_, "ESTOP"))
      return;
    if (!strcmp(cmd, "PAUSE_CPR")) {
      if (!strcmp(state_, "CPR_RUNNING")) {
        pausePending_ = true;
        restSeen_ = false;
      }
    } else if (!strcmp(cmd, "STOP_CPR") || !strcmp(cmd, "DISARM")) {
      prepared_ = pausePending_ = false;
      if (!strcmp(cmd, "DISARM"))
        rearmLock_ = !vac_.released();
      state_ = vac_.requested() || !vac_.released() ? "VACUUM_HOLD" : "IDLE";
    }
  }
  bool cpCommand(const char *text) {
    char b[128];
    snprintf(b, sizeof(b), "%s", text);
    selectable::Command c(b);
    log_.beginCommand();
    return cp_.command(c) && !log_.failed();
  }
  const char *start(uint32_t cycles) {
    if (!session_.boot)
      return "BOOT_JOURNAL_UNAVAILABLE";
    if (cp_.active())
      return "ALREADY_RUNNING";
    if (!prepared_)
      return "MANUAL_BENCH_PREPARE_REQUIRED";
    if (faulted())
      return "FAULT_OR_ESTOP_LATCHED";
    if (pausePending_)
      return "PAUSE_NOT_CONFIRMED";
    if (const char *why = cp_.durationIssue(cycles))
      return why;
    if (vac_.requested()) {
      float p = 0;
      if (!vac_.holding() || !vac_.pressure(p) || p > -15)
        return "VACUUM_NOT_READY";
    }
    if (const char *why = cp_.readyIssue())
      return why;
    if (!cpCommand("ARM"))
      return log_.reason();
    char b[48];
    snprintf(b, sizeof(b), "RUN %lu", (unsigned long)cycles);
    if (!cpCommand(b))
      return log_.reason();
    state_ = "CPR_RUNNING";
    return nullptr;
  }
  void cancelCombined() { combinedPending_ = combinedRunning_ = false; }
  const char *combinedIssue(uint32_t cycles) const {
    if (!session_.boot)
      return "BOOT_JOURNAL_UNAVAILABLE";
    if (faulted())
      return "FAULT_OR_ESTOP_LATCHED";
    if (combinedPending_ || combinedRunning_ || cp_.active() || pausePending_)
      return "BOTH_ALREADY_ACTIVE_OR_STOP_PENDING";
    if (const char *why = combinedPower_.issue(cp_.inputCurrentLimit()))
      return why;
    if (!vac_.configured())
      return "VAC_CONFIG_REQUIRED";
    if (vac_.fault())
      return "VAC_FAULT_USE_CLEAR_ERR";
    // A bad offset such as -24.6 kPa at atmosphere must not count as adhesion.
    // Uses the configured release band/time, never an invented zero offset.
    if (vac_.active() || vac_.requested() || !vac_.released() || rearmLock_)
      return "BOTH_START_FROM_VERIFIED_RELEASED_PRESSURE";
    if (!prepared_)
      return "MANUAL_BENCH_PREPARE_REQUIRED";
    if (const char *why = cp_.readyIssue())
      return why;
    return cp_.durationIssue(cycles);
  }
  const char *startCombined(uint32_t cycles) {
    if (const char *why = combinedIssue(cycles))
      return why;
    if (const char *why = vac_.startAuto(-22.5f))
      return why;
    combinedCycles_ = cycles;
    combinedAt_ = millis();
    combinedPending_ = true;
    localRelease_ = false;
    state_ = "ARMED";
    log_.emit("OK,BOTH_WAIT_VACUUM,cycles=%lu,compression_started=0", (unsigned long)cycles);
    return nullptr;
  }
  void serviceCombined() {
    if (combinedRunning_ && uint32_t(millis() - combinedAt_) >= selectable::cfg::MAX_RUN_MS) {
      trip(nullptr, "BOTH_TOTAL_TIME_LIMIT");
      return;
    }
    if (combinedRunning_ && !cp_.active()) {
      combinedRunning_ = false;
      log_.emit("EVENT,BOTH_COMPRESSION_ENDED,vacuum_retained=1,standstill_verified=0");
    }
    if (!combinedPending_)
      return;
    const char *why = faulted() || owner_ != Owner::Local ? "BOTH_CANCELLED" : nullptr;
    if (!why)
      why = combinedPower_.issue(cp_.inputCurrentLimit());
    if (!why && (!vac_.requested() || vac_.fault()))
      why = "BOTH_VACUUM_UNAVAILABLE";
    if (!why)
      why = cp_.readyIssue();
    if (why) {
      trip(nullptr, why); // Compression off first; do not release existing suction.
      return;
    }
    if (uint32_t(millis() - combinedAt_) >= vac_.acquisitionLimitMs()) {
      vac_.stop();
      trip("2-VU-004", "BOTH_VACUUM_ACQUIRE_TIMEOUT");
      return;
    }
    if (vac_.holding()) {
      combinedPending_ = false; // A single request starts at most one finite profile.
      if (const char *failure = start(combinedCycles_))
        trip(nullptr, failure);
      else {
        combinedRunning_ = true;
        log_.emit("EVENT,BOTH_COMPRESSION_STARTED,vacuum_feedback=1");
      }
    }
  }
  void reportCombined() {
    log_.emit("BOTH,waiting=%u,running=%u,power_bounds_recorded=%u,supply_limit_A=%.1f,"
              "branch_input_plan_A=%.1f,cp_motor_command_cap_A=%.2f,total_current_measured=0",
              combinedPending_, combinedRunning_, combinedPower_.known(),
              double(CombinedPower::supplyLimitA()), double(CombinedPower::branchLimitA()),
              double(cp_.commandCurrentLimit()));
    if (combinedPower_.known())
      log_.emit("BOTH_BUDGET,blowers_and_other_max_A=%.2f,cp_input_trip_A=%.2f,headroom_A=%.2f,"
                "blower_current_control=0",
                double(combinedPower_.otherLoadA()), double(cp_.inputCurrentLimit()),
                double(CombinedPower::supplyLimitA() - combinedPower_.otherLoadA() -
                       cp_.inputCurrentLimit()));
  }
  const char *execute(const Object &j, const char *cmd, uint16_t seq, uint32_t aid) {
    if (query(cmd))
      return nullptr;
    if (stopCommand(cmd)) {
      if (!strcmp(cmd, "PAUSE_CPR") && strcmp(state_, "CPR_RUNNING"))
        return "NOT_CPR_RUNNING";
      if (!strcmp(cmd, "STOP_CPR") && strcmp(state_, "CPR_RUNNING") && strcmp(state_, "CPR_PAUSED"))
        return "NOT_CPR_ACTIVE";
      stopState(cmd);
      return nullptr;
    }
    if (owner_ == Owner::Local)
      return "LOCAL_CONTROL_ACTIVE";
    if (!session_.boot)
      return "BOOT_JOURNAL_UNAVAILABLE";
    if (!strcmp(cmd, "SET_TEL_RATE")) {
      uint32_t hz = 0;
      if (!j.u32("hz", hz) || hz < 1 || hz > 50)
        return "BAD_TEL_RATE";
      period_ = 1000 / hz;
      return nullptr;
    }
    if (!strcmp(cmd, "HOME"))
      return "HOME_UNAVAILABLE_NO_VERIFIED_LIMIT";
    if (!strcmp(cmd, "HEAD_HOLD") || !strcmp(cmd, "DRIVE_STEP") || !strcmp(cmd, "STIFFNESS_SCAN"))
      return "UNSUPPORTED_MANUAL_PLACEMENT_BUILD";
    if (!strcmp(cmd, "SET_CP_POINT"))
      return "AUTO_CP_ZERO_UNAVAILABLE_USE_LOCAL_CP_PREPARE";
    if (!strcmp(cmd, "TRIAL_COMPRESSION"))
      return "TRIAL_RESULT_NOT_IMPLEMENTED";
    if (!strcmp(cmd, "RESET_FAULT")) {
      if (strcmp(state_, "FAULT"))
        return "NOT_FAULT";
      return resetFault();
    }
    if (faulted())
      return "FAULT_OR_ESTOP_LATCHED";
    if (!strcmp(cmd, "ARM")) {
      if (strcmp(state_, "IDLE"))
        return "NOT_IDLE";
      if (rearmLock_ || !vac_.released())
        return "VACUUM_RELEASE_NOT_VERIFIED";
      if (!selectable::control::configuredInputsReady())
        return "SAFETY_INPUTS_UNKNOWN";
      if (vac_.fault())
        return "BLOWER_FAULT_LATCHED";
      owner_ = Owner::Remote;
      state_ = "ARMED";
      // The existing protocol has no PPG_ON command: remote ARM enables the
      // observation-only optical acquisition, never compression motion.
      if (!ppgOn_) {
        ppg_.enter();
        ppgOn_ = true;
      }
      return nullptr;
    }
    if (!strcmp(cmd, "VACUUM_OFF")) {
      if (cp_.active() || pausePending_)
        return "STOP_COMPRESSION_BEFORE_VACUUM_RELEASE";
      vac_.stop();
      prepared_ = false;
      releaseSeq_ = seq;
      releaseAid_ = aid;
      vacSeq_ = 0;
      return nullptr;
    }
    if (!strcmp(cmd, "VACUUM_ON")) {
      if (strcmp(state_, "ARMED") && strcmp(state_, "HOMED"))
        return "NOT_ARMED";
      float target = 0;
      if (!j.num("target_kPa", target))
        return "TARGET_KPA_REQUIRED";
      const char *why = vac_.startAuto(target);
      if (why)
        return why;
      owner_ = Owner::Remote;
      releaseSeq_ = 0;
      vacSeq_ = seq;
      vacAid_ = aid;
      return nullptr;
    }
    if (!strcmp(cmd, "SET_TARGET")) {
      if (cp_.active())
        return "TARGET_CHANGE_REQUIRES_STOP";
      float bpm = 0, depth = 0, force = 0;
      if (!j.num("bpm", bpm) || !j.num("depth_mm", depth) || !j.num("force_max_N", force) ||
          !selectable::control::validTarget(depth, bpm) || force <= 0 || force > 600)
        return "BAD_BENCH_TARGET";
      char b[96];
      snprintf(b, sizeof(b), "TARGET %.6f %.6f", double(depth), double(bpm));
      if (!cpCommand(b))
        return log_.reason();
      snprintf(b, sizeof(b), "FORCELIMIT %.6f", double(force));
      if (!cpCommand(b))
        return log_.reason();
      prepared_ = false;
      return nullptr;
    }
    if (!strcmp(cmd, "START_CPR") || !strcmp(cmd, "RESUME_CPR")) {
      const bool resume = !strcmp(cmd, "RESUME_CPR");
      if (strcmp(state_, resume ? "CPR_PAUSED" : "CPR_READY"))
        return "WRONG_CPR_STATE";
      const uint32_t cycles =
          uint32_t(double(selectable::cfg::MAX_RUN_MS) * cp_.targetRate() / 60000.0);
      const char *why = cycles ? start(cycles) : "INVALID_RUN_DURATION";
      if (!why)
        owner_ = Owner::Remote;
      return why;
    }
    return "UNKNOWN_COMMAND";
  }
  void makeAck(char *b, size_t n, uint32_t seq, bool ok, const char *why) {
    if (ok)
      snprintf(b, n, "{\"v\":1,\"t\":\"ack\",\"seq\":%lu,\"ok\":true,\"state\":\"%s\"}",
               (unsigned long)seq, state_);
    else
      snprintf(
          b, n,
          "{\"v\":1,\"t\":\"ack\",\"seq\":%lu,\"ok\":false,\"err\":\"2-CU-011\",\"why\":\"%s\"}",
          (unsigned long)seq, why);
  }
  void ack(uint32_t seq, bool ok, const char *why) {
    char b[320];
    makeAck(b, sizeof(b), seq, ok, why);
    log_.raw(b);
  }
  void json(const char *line) {
    Object j;
    if (!j.parse(line) || !j.str("t") || strcmp(j.str("t"), "cmd") || !j.str("cmd")) {
      invalid("MALFORMED_JSON_COMMAND");
      return;
    }
    const char *cmd = j.str("cmd");
    const bool stopping = stopCommand(cmd);
    if (stopping)
      stopOutput(cmd);
    uint32_t v = 0, seq = 0, bid = 0, aid = 0;
    if (!j.u32("v", v) || v != 1) {
      trip("3-CM-001", "VERSION_MISMATCH");
      return;
    }
    if (!j.u32("seq", seq) || !seq || seq > 65535) {
      invalid("INVALID_SEQ");
      return;
    }
    lastJson_ = millis();
    char key[768];
    if (!j.canonical(key, sizeof(key))) {
      invalid("COMMAND_TOO_COMPLEX");
      return;
    }
    bool record = false;
    if (legacy_ && !query(cmd)) {
      if ((j.find("bid") || j.find("aid")) && !stopping) {
        ack(seq, false, "LEGACY_PROFILE_EXPECTS_SEQ_ONLY");
        return;
      }
      const auto *old = legacyReceipts_.lookup(uint16_t(seq));
      if (old) {
        if (strcmp(old->key, key))
          ack(seq, false, "SEQ_CONFLICT");
        else
          log_.raw(old->ack);
        return;
      }
      record = true;
    } else if (!query(cmd)) {
      const bool fields = j.u32("bid", bid) && j.u32("aid", aid) && aid;
      const char *why = !fields ? "AID_REQUIRED" : bid != session_.boot ? "BOOT_MISMATCH" : nullptr;
      const Record *old = !why ? session_.lookup(aid) : nullptr;
      if (old) {
        if (strcmp(old->key, key))
          ack(seq, false, "SEQ_CONFLICT");
        else {
          log_.raw(old->ack);
          if (old->event[0])
            log_.raw(old->event);
        }
        return;
      }
      if (!why && aid <= session_.maxAid)
        why = session_.maxAid == UINT32_MAX ? "AID_EXHAUSTED" : "STALE_SEQ";
      if (why && !stopping) {
        ack(seq, false, why);
        return;
      }
      record = !why;
    }
    const char *why = execute(j, cmd, uint16_t(seq), aid);
    char response[320];
    makeAck(response, sizeof(response), seq, !why, why);
    if (record) {
      if (legacy_)
        legacyReceipts_.remember(uint16_t(seq), key, response);
      else
        session_.remember(action(cmd), !why, aid, uint16_t(seq), key, response);
    }
    log_.raw(response);
    if (why)
      return;
    if (!strcmp(cmd, "GET_STATUS"))
      telemetry();
    else if (!strcmp(cmd, "GET_INFO")) {
      char b[384];
      snprintf(b, sizeof(b),
               "{\"v\":1,\"t\":\"evt\",\"evt\":\"INFO\",\"seq\":%lu,\"fw\":\"manual-bench-"
               "20261023\",\"proto\":\"%s\",\"caps\":[%s],\"boot_id\":%u,"
               "\"last_seq\":%u,\"last_aid\":%lu}",
               (unsigned long)seq, legacy_ ? "1.0" : "1.1", legacy_ ? "" : "\"AID\",\"MOT\"",
               session_.boot, session_.lastSeq, (unsigned long)session_.maxAid);
      log_.raw(b);
    } else if (!strcmp(cmd, "GET_FAULTS")) {
      char b[384];
      if (fault_[0]) {
        snprintf(b, sizeof(b),
                 "{\"v\":1,\"t\":\"err\",\"ms\":%lu,\"code\":\"%s\",\"sev\":%c,\"latched\":true,"
                 "\"why\":\"%s\"}",
                 (unsigned long)millis(), fault_, fault_[0], faultWhy_);
        log_.raw(b);
      } else if (faulted())
        log_.emit("FAULT,%s,protocol_code=UNASSIGNED", faultWhy_);
      snprintf(b, sizeof(b),
               "{\"v\":1,\"t\":\"evt\",\"evt\":\"FAULTS_END\",\"ms\":%lu,\"seq\":%lu,\"n\":%u}",
               (unsigned long)millis(), (unsigned long)seq, fault_[0] ? 1 : 0);
      log_.raw(b);
    }
  }
  void event(const char *name, uint16_t seq, uint32_t aid) {
    char b[256];
    float p = 0;
    const bool valid = vac_.pressure(p);
    if (valid)
      snprintf(b, sizeof(b),
               "{\"v\":1,\"t\":\"evt\",\"ms\":%lu,\"evt\":\"%s\",\"seq\":%u,\"aid\":%lu,\"vac_"
               "kPa\":%.2f}",
               (unsigned long)millis(), name, seq, (unsigned long)aid, double(p));
    else
      snprintf(b, sizeof(b),
               "{\"v\":1,\"t\":\"evt\",\"ms\":%lu,\"evt\":\"%s\",\"seq\":%u,\"aid\":%lu}",
               (unsigned long)millis(), name, seq, (unsigned long)aid);
    log_.raw(b);
  }
  void telemetry() {
    const uint32_t sampleMs = millis();
    char b[512];
    size_t n = size_t(
        snprintf(b, sizeof(b),
                 "{\"v\":1,\"t\":\"tel\",\"ms\":%lu,\"tn\":%lu,\"boot_id\":%u,\"state\":\"%s\"",
                 (unsigned long)sampleMs, (unsigned long)++tn_, session_.boot, state_));
    auto field = [&](const char *k, float v, bool ok) {
      if (ok && n < sizeof(b))
        n += size_t(snprintf(b + n, sizeof(b) - n, ",\"%s\":%.2f", k, double(v)));
    };
    float a = 0, d = 0, r = 0;
    double f = 0;
    bool ok = vac_.pressure(a);
    field("vac_kPa", a, ok);
    ok = cp_.motorCurrent(a);
    field("cp_motor_i_A", a, ok);
    ok = cp_.force(f);
    field("cp_force_N", float(f), ok);
    ok = cp_.cycleMetrics(d, r);
    field("cp_depth_mm", d, ok);
    field("cp_rate_cpm", r, ok);
    ok = ppg_.heartRate(a);
    field("ppg_hr_bpm", a, ok && ppgOn_);
    ok = ppg_.saturation(a);
    field("spo2_pct", a, ok && ppgOn_);
    if (selectable::cfg::ESTOP_AUX_INSTALLED && n < sizeof(b))
      n += size_t(snprintf(b + n, sizeof(b) - n, ",\"estop\":%s",
                           !strcmp(state_, "ESTOP") ? "true" : "false"));
    if (selectable::cfg::LIMIT_SWITCHES_INSTALLED && n < sizeof(b))
      n += size_t(
          snprintf(b + n, sizeof(b) - n, ",\"limit_top\":%s,\"limit_bottom\":%s",
                   digitalRead(3) == selectable::cfg::TOP_ACTIVE_LEVEL ? "true" : "false",
                   digitalRead(4) == selectable::cfg::BOTTOM_ACTIVE_LEVEL ? "true" : "false"));
    if (n < sizeof(b)) {
      if (faulted() && !fault_[0])
        n += size_t(snprintf(b + n, sizeof(b) - n, "}\n"));
      else
        n += size_t(snprintf(b + n, sizeof(b) - n, ",\"faults\":[%s%s%s]}\n", fault_[0] ? "\"" : "",
                             fault_, fault_[0] ? "\"" : ""));
    }
    if (n < sizeof(b))
      log_.data(b);
    snprintf(b, sizeof(b),
             "{\"v\":1,\"t\":\"mot\",\"ms\":%lu,\"tn\":%lu,\"boot_id\":%u,\"busy\":\"NONE\","
             "\"hold\":false,\"settled\":%s,\"rearm_lock\":%s,\"vac_released\":%s}\n",
             (unsigned long)sampleMs, (unsigned long)tn_, session_.boot,
             cp_.standstill() ? "true" : "false", rearmLock_ ? "true" : "false",
             vac_.released() ? "true" : "false");
    log_.data(b);
  }
  void local(char *line) {
    selectable::Command c(line);
    if (!c.count)
      return;
    if (c.is("STOP", 1) || c.is("DISARM", 1)) {
      cp_.stop();
      cancelCombined();
      stopState(c.is("STOP", 1) ? "STOP_CPR" : "DISARM");
      log_.emit("OK,CP_STOP_REQUESTED,standstill_verified=0");
      return;
    }
    if (c.is("BOTH", 2) && !strcmp(c.tokens[1], "STOP")) {
      cp_.stop();
      cancelCombined();
      stopState("STOP_CPR");
      log_.emit("OK,BOTH_CANCELLED,compression_zero_requested=1,vacuum_retained=1,"
                "standstill_verified=0");
      return;
    }
    if (c.is("BOTH", 2) && (!strcmp(c.tokens[1], "STATUS") || !strcmp(c.tokens[1], "CHECK"))) {
      reportCombined();
      if (!strcmp(c.tokens[1], "CHECK")) {
        const char *why = combinedIssue(1);
        log_.emit("BOTH_CHECK,ready=%u,reason=%s", !why, why ? why : "READY");
      }
      return;
    }
    if (c.is("STATUS", 1)) {
      telemetry();
      cp_.report();
      vac_.report();
      ppg_.report();
      reportCombined();
      return;
    }
    if (c.is("HELP", 1)) {
      log_.emit("COMMANDS,CP ZERO|CAL mm|TARE|FORCE_CAL kg|TARGET mm cpm|FORCELIMIT N|ILIMIT "
                "A|INLIMIT A|CHECK|CLEAR|PREPARE|RUN cycles");
      log_.emit("COMMANDS,VAC CONFIG pct max_on_ms stable_ms release_band_kPa release_ms|ARM|RUN1 "
                "pct|RUN2 pct|RUNBOTH pct pct|ON|OFF|CLEAR_ERR");
      log_.emit("COMMANDS,PPG ON|OFF; STREAM ON|OFF; STOP=CP_only; DISARM=retain_vacuum; CONTROL "
                "LOCAL|REMOTE|REMOTE_V1");
      log_.emit("COMMANDS,BOTH POWER blower1_max_A blower2_max_A other_max_A|CHECK|STATUS|RUN "
                "cycles|STOP; STOP_retains_vacuum=1");
      return;
    }
    if (c.is("CLEAR_FAULT", 1)) {
      if (const char *why = resetFault())
        log_.emit("ERROR,%s", why);
      else
        log_.emit("OK,FAULT_CLEARED,armed=0");
      return;
    }
    if (c.is("CONTROL", 2)) {
      if (cp_.active() || pausePending_ || combinedPending_) {
        log_.emit("ERROR,STOP_BEFORE_CONTROL_TRANSFER");
        return;
      }
      if (!strcmp(c.tokens[1], "REMOTE") || !strcmp(c.tokens[1], "REMOTE_V1")) {
        legacy_ = !strcmp(c.tokens[1], "REMOTE_V1");
        owner_ = Owner::Remote;
        quiet_ = false;
        lastJson_ = millis();
        log_.emit("OK,REMOTE_CONTROL,proto=%s", legacy_ ? "1.0" : "1.1");
      } else if (!strcmp(c.tokens[1], "LOCAL") &&
                 (owner_ != Owner::Remote || uint32_t(millis() - lastJson_) >= 2000)) {
        owner_ = Owner::Local;
        log_.emit("OK,LOCAL_CONTROL");
      } else
        log_.emit("ERROR,CONTROL_TRANSFER_REJECTED");
      return;
    }
    if (owner_ == Owner::Remote) {
      log_.emit("ERROR,REMOTE_CONTROL_ACTIVE");
      return;
    }
    owner_ = Owner::Local;
    if (c.is("STREAM", 2)) {
      if (!strcmp(c.tokens[1], "OFF"))
        quiet_ = true;
      else if (!strcmp(c.tokens[1], "ON"))
        quiet_ = false;
      else {
        log_.emit("ERROR,BAD_STREAM_COMMAND");
        return;
      }
      log_.emit("OK,STREAM_%s,sensors_and_interlocks_unchanged=1", c.tokens[1]);
      return;
    }
    // A pending combined request must not survive configuration/calibration or
    // control transfer and unexpectedly start later. Read-only queries and the
    // stop commands above remain available; STREAM only changes output.
    if (combinedPending_ || combinedRunning_) {
      invalid("BOTH_ACTIVE_USE_STOP_BEFORE_CONFIG");
      return;
    }
    if (c.count && !strcmp(c.tokens[0], "BOTH")) {
      if (c.is("BOTH", 5) && !strcmp(c.tokens[1], "POWER")) {
        float a = 0, b = 0, other = 0;
        if (cp_.active() || pausePending_ || vac_.active() || vac_.requested()) {
          log_.emit("ERROR,BOTH_POWER_REQUIRES_OUTPUTS_OFF");
          return;
        }
        combinedPower_.invalidate();
        prepared_ = false;
        if (!selectable::control::parseNumber(c.tokens[2], a) ||
            !selectable::control::parseNumber(c.tokens[3], b) ||
            !selectable::control::parseNumber(c.tokens[4], other) ||
            !combinedPower_.configure(a, b, other))
          log_.emit("ERROR,BOTH_BAD_POWER_BOUNDS,blower_each_LE_3A=1,total_with_CP_LT_10A=1");
        else {
          log_.emit("OK,BOTH_POWER_BOUNDS_RECORDED,not_a_total_current_sensor=1");
          reportCombined();
        }
      } else if (c.is("BOTH", 3) && !strcmp(c.tokens[1], "RUN")) {
        uint32_t cycles = 0;
        if (!selectable::control::parseCycles(c.tokens[2], cycles))
          invalid("BAD_CYCLES");
        else if (const char *why = startCombined(cycles))
          log_.emit("ERROR,%s", why);
      } else
        invalid("BAD_BOTH_COMMAND");
      return;
    }
    if (c.count == 2 && !strcmp(c.tokens[0], "PPG")) {
      if (cp_.active()) {
        log_.emit("ERROR,PPG_CONFIG_REQUIRES_CP_STOP");
        return;
      }
      if (!strcmp(c.tokens[1], "ON")) {
        ppg_.enter();
        ppgOn_ = true;
      } else if (!strcmp(c.tokens[1], "OFF")) {
        ppg_.stop();
        ppgOn_ = false;
      } else
        log_.emit("ERROR,BAD_PPG_COMMAND");
      return;
    }
    if (c.count >= 2 && c.count <= 8 && !strcmp(c.tokens[0], "CP")) {
      if (c.count == 2 && !strcmp(c.tokens[1], "PREPARE")) {
        if (cp_.active() || pausePending_) {
          log_.emit("ERROR,PREPARE_REQUIRES_STOP");
          return;
        }
        const char *why = cp_.readyIssue();
        if (why || faulted() || !session_.boot)
          log_.emit("ERROR,%s", why              ? why
                                : !session_.boot ? "BOOT_JOURNAL_UNAVAILABLE"
                                                 : "FAULT_LATCHED");
        else {
          prepared_ = true;
          state_ = "CPR_READY";
          log_.emit("OK,MANUAL_BENCH_READY,not_HOME_or_CP_ZERO=1");
        }
        return;
      }
      if (!strcmp(c.tokens[1], "RUN")) {
        uint32_t cycles = 1;
        if (c.count > 3 ||
            (c.count == 3 && !selectable::control::parseCycles(c.tokens[2], cycles))) {
          invalid("BAD_CYCLES");
          return;
        }
        if (const char *why = start(cycles))
          log_.emit("ERROR,%s", why);
        return;
      }
      char b[128] = {};
      if (!join(c, b, sizeof(b))) {
        invalid("LOCAL_COMMAND_TOO_LONG");
        return;
      }
      if (strcmp(c.tokens[1], "CHECK"))
        prepared_ = false;
      if (!cpCommand(b))
        log_.emit("ERROR,%s", log_.failed() ? log_.reason() : "UNKNOWN_CP_COMMAND");
      if (!cp_.fault() && !strcmp(c.tokens[1], "CLEAR") && !vac_.fault() && session_.boot &&
          strcmp(state_, "ESTOP")) {
        fault_[0] = 0;
        faultWhy_[0] = 0;
        state_ = "IDLE";
      }
      return;
    }
    if (c.count >= 2 && c.count <= 7 && !strcmp(c.tokens[0], "VAC")) {
      if (cp_.active() || pausePending_) {
        log_.emit("ERROR,STOP_CP_BEFORE_VAC_CONFIG_OR_RELEASE");
        return;
      }
      if ((!strcmp(c.tokens[1], "ON") || !strcmp(c.tokens[1], "ARM") ||
           !strncmp(c.tokens[1], "RUN", 3)) &&
          (faulted() || !session_.boot)) {
        log_.emit("ERROR,FAULT_OR_BOOT_JOURNAL_UNAVAILABLE");
        return;
      }
      prepared_ = false;
      // Duty/configuration/arming changes invalidate the recorded blower
      // current measurements. OFF and CLEAR_ERR do not change the duty.
      if (strcmp(c.tokens[1], "OFF") && strcmp(c.tokens[1], "CLEAR_ERR"))
        combinedPower_.invalidate();
      if (c.count == 2 && !strcmp(c.tokens[1], "ON")) {
        const char *why = vac_.startAuto(-22.5f);
        if (why)
          log_.emit("ERROR,%s", why);
        else {
          state_ = "ARMED";
          localRelease_ = false;
          log_.emit("OK,VACUUM_START_REQUESTED");
        }
        return;
      }
      char b[128] = {};
      if (!join(c, b, sizeof(b))) {
        invalid("LOCAL_COMMAND_TOO_LONG");
        return;
      }
      selectable::Command v(b);
      log_.beginCommand();
      if (!vac_.command(v))
        log_.emit("ERROR,UNKNOWN_VAC_COMMAND");
      else if (!log_.failed() && !strcmp(c.tokens[1], "ARM"))
        state_ = "ARMED";
      else if (!log_.failed() && !strcmp(c.tokens[1], "OFF")) {
        localRelease_ = true;
        if (!faulted())
          state_ = "VACUUM_HOLD";
      }
      return;
    }
    invalid("UNKNOWN_LOCAL_COMMAND");
  }
  const char *resetFault() {
    cp_.stop();
    cancelCombined();
    prepared_ = pausePending_ = false;
    if (!session_.boot)
      return "BOOT_JOURNAL_UNAVAILABLE";
    if (!strcmp(state_, "ESTOP"))
      return "ESTOP_REQUIRES_PHYSICAL_RESET";
    if (cp_.fault() && !cpCommand("CLEAR"))
      return log_.reason();
    if (vac_.fault()) {
      char b[] = "CLEAR_ERR";
      selectable::Command c(b);
      log_.beginCommand();
      vac_.command(c);
      if (log_.failed())
        return log_.reason();
    }
    if (!selectable::control::configuredInputsReady())
      return "SAFETY_INPUTS_UNKNOWN";
    fault_[0] = faultWhy_[0] = 0;
    state_ = vac_.requested() || !vac_.released() ? "VACUUM_HOLD" : "IDLE";
    return nullptr;
  }
  static bool join(const selectable::Command &c, char *b, size_t n) {
    for (unsigned i = 1; i < c.count; ++i) {
      if (strlen(b) + strlen(c.tokens[i]) + (i > 1 ? 1 : 0) >= n)
        return false;
      if (i > 1)
        strncat(b, " ", n - strlen(b) - 1);
      strncat(b, c.tokens[i], n - strlen(b) - 1);
    }
    return true;
  }
  void invalid(const char *why) {
    const bool operating = cp_.active() || combinedPending_ || combinedRunning_ ||
                           !strcmp(state_, "CPR_READY") || !strcmp(state_, "CPR_PAUSED");
    cp_.stop();
    cancelCombined();
    prepared_ = pausePending_ = false;
    if (operating)
      trip("1-CM-003", why);
    else
      log_.emit("ERROR,%s", why);
  }
  void poll() {
    if ((len_ || carriage_) && uint32_t(millis() - rxAt_) >= 1000) {
      len_ = 0;
      carriage_ = false;
      discard_ = true;
      invalid("PARTIAL_LINE_TIMEOUT");
    }
    for (unsigned i = 0; i < 128 && Serial.available(); ++i) {
      const char c = char(Serial.read());
      rxAt_ = millis();
      if (c == '\n') {
        carriage_ = false;
        if (discard_) {
          discard_ = false;
          len_ = 0;
          continue;
        }
        line_[len_] = 0;
        if (len_) {
          const char *start = line_;
          while (*start == ' ' || *start == '\t')
            ++start;
          if (*start == '{')
            json(line_);
          else
            local(line_);
        }
        len_ = 0;
      } else if (carriage_) {
        len_ = 0;
        carriage_ = false;
        discard_ = true;
        invalid("CR_ONLY_ALLOWED_BEFORE_LF");
      } else if (c == '\r') {
        carriage_ = true;
      } else if (!discard_) {
        if ((uint8_t(c) < 32 && c != '\t') || uint8_t(c) > 126 || len_ >= sizeof(line_) - 1) {
          len_ = 0;
          discard_ = true;
          invalid("LINE_OVERFLOW_OR_NON_ASCII");
        } else
          line_[len_++] = c;
      }
    }
  }
  selectable::Log log_;
  selectable::Compression cp_;
  selectable::Vacuum vac_;
  selectable::Ppg ppg_;
  Session session_;
  LegacyReceipts legacyReceipts_;
  CombinedPower combinedPower_;
  const char *state_ = "IDLE";
  char fault_[16] = {}, faultWhy_[96] = {}, line_[512] = {};
  size_t len_ = 0;
  Owner owner_ = Owner::None;
  bool connected_ = false, discard_ = false, prepared_ = false, ppgOn_ = false;
  bool carriage_ = false, resync_ = false;
  bool legacy_ = false;
  bool localRelease_ = false;
  bool quiet_ = false;
  bool pausePending_ = false, restSeen_ = false, rearmLock_ = false;
  bool combinedPending_ = false, combinedRunning_ = false;
  uint32_t combinedAt_ = 0, combinedCycles_ = 0;
  uint32_t lastJson_ = 0, rxAt_ = 0, restAt_ = 0, tn_ = 0, telAt_ = 0, period_ = 50;
  uint16_t vacSeq_ = 0, releaseSeq_ = 0;
  uint32_t vacAid_ = 0, releaseAid_ = 0;
};
inline App &instance() {
  static App app;
  return app;
}
} // namespace saver_demo
#endif
