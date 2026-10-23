/* host_test.cpp — saver_teensy.ino(v1.1)를 고치지 않고 PC 에서 컴파일해 명령 처리를 시험한다.
 * 2026-10-23 신상현(Claude Code) 작성, 한건민 검토 필요.
 * 실물 동작 검증이 아니다. Teensy 툴체인 컴파일도 아니다(Arduino.h 등은 이 폴더의 대용 헤더).
 * 저장소 루트에서:
 *
 * 1) 기본 빌드: 펌웨어 그대로(구동 훅 없음, §7.1 설정값 미설정)
 *    mkdir -p build
 *    g++ -std=c++14 -Wall -Wextra -I teensy/tests/host -o build/saver_host teensy/tests/host/host_test.cpp
 *    ./build/saver_host           모든 장면을 장면마다 새 프로세스로 실행
 *    ./build/saver_host aid       장면 하나만
 *
 * 2) 훅 빌드: PC 시험 전용 설정값(jetson/simulators/teensy_settings.sim.json 과 같은 시뮬레이션 값)과
 *    가짜 구동 훅으로 busy, 완료 이벤트, DISARM 음압 판정, 해제 확인을 시험
 *    g++ -std=c++14 -Wall -Wextra -DSAVER_HOST_HOOKS -DSAVER_HW_HEAD_MOTION=1 -DSAVER_HW_DRIVE_MOTION=1 \
 *        -I teensy/tests/host -o build/saver_host_hooks teensy/tests/host/host_test.cpp
 *    ./build/saver_host_hooks
 *
 * 3) 가상 포트(리눅스·맥): ./build/saver_host --pty  →  "PTY /dev/pts/N" 을 출력하고 실시간으로 loop() 를 돈다.
 *    젯슨: python3 -c "from saver.teensy_serial import TeensyLink; ..." 또는 브리지 --serial /dev/pts/N.
 *    훅 빌드의 --pty 는 압력 0 kPa(해제 확인), 상단 리미트 검출, 가짜 구동 0.5 s 뒤 성공(시험 전용).
 *
 * C++14: src/saver_linear 가 C++14 constexpr 을 쓴다(Teensyduino 는 gnu++14 이상). */
#include <Arduino.h>
#include "../../saver_teensy.ino"

#include <cstdio>
#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>
#include <iostream>
#if defined(__unix__) || defined(__APPLE__)
#include <fcntl.h>
#include <signal.h>
#include <termios.h>
#include <time.h>
#endif

/* ───────────────────────── 가짜 구동 훅 (훅 빌드) ───────────────────────── */
#ifdef SAVER_HOST_HOOKS
namespace fake {
bool vac_ok = true;            /* 압력센서 유효 */
float vac_kPa = 0.0f;          /* 대기압 */
bool limit_top = true;         /* 헤드가 상단에 있음 */
bool linear_ok = true;
float linear_mm = 0.0f;
bool contact_clear = true;     /* 접촉 탐색 시작 조건 */
bool vac_out = false;
bool vac_reached = false;
bool encoder_valid = true;
bool settled = true;
float drv_pos = 0.0f;
int home_starts = 0, search_starts = 0, drive_starts = 0, cancels = 0, zeros = 0;
bool search_hold = false;
float search_press = 0.0f;
/* 시험이 정하는 다음 완료 */
bool finish = false;
const char* result = "OK";
const char* code = NULL;
MotionEvt rep = {};
const char* raise3 = NULL;     /* 다음 tick 에 심각도 3 오류 */
bool autoMode = false;         /* --pty: 0.5 s 뒤 자동 성공 */
uint32_t started = 0;
}  /* namespace fake */

void hwVacuumOutput(bool on) { fake::vac_out = on; fake::started = millis(); }
bool hwVacuumReached() {
  return fake::vac_reached || (fake::autoMode && fake::vac_out && millis() - fake::started >= 500);
}
static bool fakeTick(MotionEvt& rep, bool home) {
  if (fake::raise3) {
    const char* c = fake::raise3;
    fake::raise3 = NULL;
    raiseFault(c, 3, "TEST_FAULT");      /* 훅 안에서 바로(출력 차단이 먼저) */
    return false;
  }
  const bool due = fake::finish || (fake::autoMode && millis() - fake::started >= 500);
  if (!due) return false;
  rep = fake::rep;
  rep.result = fake::result;
  rep.code = fake::code;
  if (home && !strcmp(fake::result, "OK")) fake::limit_top = true;
  fake::finish = false;
  fake::result = "OK";
  fake::code = NULL;
  fake::rep = MotionEvt();
  return true;
}
void hwHomeStart() { fake::home_starts++; fake::started = millis(); fake::limit_top = false; }
bool hwHomeTick(MotionEvt& rep) { return fakeTick(rep, true); }
void hwHeadSearchStart(bool hold, float press_mm) {
  fake::search_starts++; fake::search_hold = hold; fake::search_press = press_mm;
  fake::started = millis(); fake::limit_top = false;
}
bool hwHeadSearchTick(MotionEvt& rep) { return fakeTick(rep, false); }
bool hwHeadContactClear() { return fake::contact_clear; }
void hwHeadCancel() { fake::cancels++; }
void hwDriveStart(float, float) { fake::drive_starts++; fake::started = millis(); }
bool hwDriveTick(MotionEvt& rep) { return fakeTick(rep, false); }
void hwDriveZero() { fake::zeros++; fake::drv_pos = 0.0f; }
bool hwDriveEncoderValid() { return fake::encoder_valid; }
bool hwDriveSettled() { return fake::settled; }
void hwDrivePose(MotStatus& m) {
  if (!fake::encoder_valid) return;
  m.drv_pos_ok = true; m.drv_pos_mm = fake::drv_pos;
  m.drv_yaw_ok = true; m.drv_yaw_deg = 0.0f;
}
void hwMotionStopReport(MotionEvt&) {}
void hostSensorOverride(Sensors& s) {
  s.vac_ok = fake::vac_ok; s.vac_kPa = fake::vac_kPa;
  s.limit_top = fake::limit_top;
  s.linear_position_ok = fake::linear_ok; s.linear_position_mm = fake::linear_mm;
}

/* jetson/simulators/teensy_settings.sim.json 과 같은 시뮬레이션 전용 값, 그 파일에 없는 주행 항목은 1.
 * 의학적·공학적 근거 없음. 실기 펌웨어에 쓰지 않는다. */
static void simSettings() {
  g_cfg.limit_top_ok = 1;
  g_cfg.drv_speed_mm_s = 20; g_cfg.drv_speed_max_mm_s = 40; g_cfg.drv_step_max_mm = 20;
  g_cfg.drv_timeout_ms = 3000; g_cfg.drv_settle_ms = 100; g_cfg.drv_settle_max_ms = 1000;
  g_cfg.drv_avg_ms = 200; g_cfg.drv_avg_min_n = 10; g_cfg.drv_pos_tol_mm = 0.5f;
  g_cfg.force_stale_ms = 100; g_cfg.scan_force_max_N = 150; g_cfg.contact_force_N = 5;
  g_cfg.contact_confirm_ms = 50;
  g_cfg.head_search_speed_mm_s = 10; g_cfg.head_search_max_mm = 80; g_cfg.head_timeout_ms = 10000;
  g_cfg.head_press_max_mm = 15;
  g_cfg.vac_release_kPa = 1.0f; g_cfg.vac_release_ms = 300; g_cfg.vac_release_timeout_ms = 5000;
  g_cfg.enc_counts_per_mm_l = 1; g_cfg.enc_counts_per_mm_r = 1; g_cfg.drv_accel_mm_s2 = 1;
  g_cfg.drv_enc_timeout_ms = 1; g_cfg.drv_enc_diff_max_mm = 1; g_cfg.drv_yaw_max_deg = 1;
  g_cfg.drv_tilt_max_deg = 1; g_cfg.drv_imu_timeout_ms = 1;
}
#endif

/* ───────────────────────── 시험 도구 ───────────────────────── */
namespace {
int g_pass = 0, g_fail = 0;
void ck(const std::string& name, bool ok, const std::string& detail = "") {
  std::cout << (ok ? "  OK   " : "  FAIL ") << name;
  if (!detail.empty()) std::cout << "  <" << detail << ">";
  std::cout << "\n";
  ok ? g_pass++ : g_fail++;
}

std::vector<std::string> g_lines;    /* 펌웨어가 보낸 줄(LF 제외) */
std::string g_partial;
bool g_cr = false;                   /* CR 을 보낸 적이 있나 */
unsigned g_boot = 0;
bool g_keepAlive = true;             /* 1 초마다 PING(통신 워치독 2 s 를 넘지 않게) */
uint32_t g_lastPing = 0;
const unsigned PING_SEQ = 9;

void collect() {
  g_partial += Serial.tx;
  Serial.tx.clear();
  size_t p;
  while ((p = g_partial.find('\n')) != std::string::npos) {
    std::string l = g_partial.substr(0, p);
    if (l.find('\r') != std::string::npos) g_cr = true;
    g_lines.push_back(l);
    g_partial.erase(0, p + 1);
  }
}

void stepMs(uint32_t ms) {
  for (uint32_t k = 0; k < ms; k++) {
    host::pins().now_us += 1000;
    if (g_keepAlive && millis() - g_lastPing >= 1000) {
      g_lastPing = millis();
      Serial.rx += "{\"v\":1,\"t\":\"cmd\",\"seq\":9,\"cmd\":\"PING\"}\n";
    }
    loop();
  }
  collect();
}

size_t mark() { collect(); return g_lines.size(); }

std::string sval(const std::string& l, const char* k) {
  char b[SAVER_MAX_LINE];
  return jsonStr(l.c_str(), k, b, sizeof b) ? std::string(b) : std::string("<없음>");
}
long long ival(const std::string& l, const char* k) {
  uint32_t u;
  return jsonU32(l.c_str(), k, &u) ? (long long)u : -1;
}
int bval(const std::string& l, const char* k) {
  bool b;
  return jsonBool(l.c_str(), k, &b) ? (b ? 1 : 0) : -1;
}
bool has(const std::string& l, const char* k) { return jsonHas(l.c_str(), k); }
bool isT(const std::string& l, const char* t) { return sval(l, "t") == t; }

std::vector<std::string> pick(const std::vector<std::string>& v, const char* t, const char* evt = NULL) {
  std::vector<std::string> o;
  for (size_t i = 0; i < v.size(); i++)
    if (isT(v[i], t) && (!evt || sval(v[i], "evt") == evt)) o.push_back(v[i]);
  return o;
}
std::string last(const char* t) {
  collect();
  for (size_t i = g_lines.size(); i-- > 0;) if (isT(g_lines[i], t)) return g_lines[i];
  return "";
}

unsigned nextSeq() {
  static unsigned s = 100;
  if (++s > 60000) s = 101;
  return s;
}

std::string cmdLine(const std::string& cmd, const std::string& extra, long long aid, long long bid, unsigned seq) {
  std::string s = "{\"v\":1,\"t\":\"cmd\",\"seq\":" + std::to_string(seq) + ",\"cmd\":\"" + cmd + "\"";
  if (bid >= 0) s += ",\"bid\":" + std::to_string(bid);
  if (aid >= 0) s += ",\"aid\":" + std::to_string(aid);
  if (!extra.empty()) s += "," + extra;
  return s + "}";
}

struct R {
  std::vector<std::string> out;
  std::string ack;
  unsigned seq;
  bool ok() const { return bval(ack, "ok") == 1; }
  std::string why() const { return sval(ack, "why"); }
  std::string err() const { return sval(ack, "err"); }
  std::string state() const { return sval(ack, "state"); }
  std::vector<std::string> evts(const char* name) const { return pick(out, "evt", name); }
  std::vector<std::string> errs() const { return pick(out, "err"); }
};

R rawLine(const std::string& line, unsigned seq, uint32_t ms = 4) {
  size_t m = mark();
  Serial.rx += line;
  Serial.rx += "\n";
  stepMs(ms);
  R r;
  r.seq = seq;
  r.out.assign(g_lines.begin() + (long)m, g_lines.end());
  for (size_t i = 0; i < r.out.size(); i++)
    if (isT(r.out[i], "ack") && ival(r.out[i], "seq") == (long long)seq) { r.ack = r.out[i]; break; }
  return r;
}

enum Bid { BID_OK, BID_NONE, BID_WRONG };
R exec(const std::string& cmd, const std::string& extra = "", long long aid = -1, Bid bm = BID_OK,
       long long seqForce = -1, uint32_t ms = 4) {
  const unsigned seq = seqForce >= 0 ? (unsigned)seqForce : nextSeq();
  const long long bid = bm == BID_OK ? (long long)g_boot : bm == BID_WRONG ? (long long)g_boot + 1 : -1;
  return rawLine(cmdLine(cmd, extra, aid, bid, seq), seq, ms);
}
R query(const std::string& cmd) { return exec(cmd, "", -1, BID_NONE); }

std::string info() {
  R r = query("GET_INFO");
  std::vector<std::string> v = r.evts("INFO");
  return v.empty() ? std::string() : v[0];
}

std::string summary(const R& r) { return r.ack.empty() ? "(ACK 없음)" : r.ack; }

unsigned long driveEnWrites() { return host::pins().writes[PIN_DRIVE1_EN]; }

/* 모든 장면 끝에: 모든 줄이 평면 JSON, 511 byte 이하, v:1, CR 없음, 송신 실패 없음 */
void finalChecks() {
  collect();
  bool valid = true, len = true, ver = true;
  std::string bad;
  for (size_t i = 0; i < g_lines.size(); i++) {
    const std::string& l = g_lines[i];
    if (!saverJsonValid(l.c_str())) { valid = false; bad = l; }
    if (l.size() > SAVER_MAX_BODY) len = false;
    if (ival(l, "v") != 1) ver = false;
  }
  ck("모든 줄 평면 JSON", valid, bad);
  ck("모든 줄 본문 ≤ 511 byte", len);
  ck("모든 줄 v:1", ver);
  ck("CR 없음(LF 하나)", !g_cr && g_partial.empty());
  ck("생성 실패·초과 줄 없음", g_txFail == 0, std::to_string(g_txFail));
}

void start() {
  setup();
  collect();
#ifdef SAVER_HOST_HOOKS
  simSettings();
#endif
  g_lastPing = millis();
  std::string i = info();
  g_boot = (unsigned)ival(i, "boot_id");
}

/* ───────────────────────── 기본 빌드 장면 ───────────────────────── */
#ifndef SAVER_HOST_HOOKS
void scInfo() {
  ck("부팅 이벤트 BOOT", !g_lines.empty() && sval(g_lines[0], "evt") == "BOOT", g_lines.empty() ? "" : g_lines[0]);
  R r = query("GET_INFO");
  std::vector<std::string> inf = r.evts("INFO");
  ck("GET_INFO → ACK ok", r.ok() && r.state() == "IDLE", summary(r));
  ck("INFO 1개", inf.size() == 1);
  const std::string i = inf.empty() ? "" : inf[0];
  ck("INFO seq 는 GET_INFO 의 seq", ival(i, "seq") == (long long)r.seq, i);
  ck("proto 1.1, fw 1.1.0", sval(i, "proto") == "1.1" && sval(i, "fw") == "1.1.0", i);
  ck("caps = MOT, AID, FAULTS (구동 훅 없음)", i.find("\"caps\":[\"MOT\",\"AID\",\"FAULTS\"]") != std::string::npos, i);
  ck("boot_id 1~65535", ival(i, "boot_id") >= 1 && ival(i, "boot_id") <= 65535, i);
  ck("기록 전 last_seq·last_aid 생략", !has(i, "last_seq") && !has(i, "last_aid"), i);
  ck("drv_enc_mode 2(미설정), drv_enc_side 생략", ival(i, "drv_enc_mode") == 2 && !has(i, "drv_enc_side"), i);
  R again = rawLine(cmdLine("GET_INFO", "", -1, -1, r.seq), r.seq);
  ck("같은 seq 의 조회도 매번 새로 실행", again.ok() && again.evts("INFO").size() == 1);
  ck("부팅 횟수는 EEPROM 4160~4163", EEPROM.read(4160) == 1 && EEPROM.read(4162) == 0xFE, std::to_string(EEPROM.read(4160)));
}

void scTelMot() {
  const size_t m = mark();
  stepMs(260);
  std::vector<std::string> v(g_lines.begin() + (long)m, g_lines.end());
  std::vector<std::string> pairs;
  bool paired = true, sameBoot = true, inc = true;
  long long lastTn = -1;
  int nt = 0;
  for (size_t i = 0; i < v.size(); i++) {
    if (!isT(v[i], "tel")) continue;
    nt++;
    if (i + 1 >= v.size() || !isT(v[i + 1], "mot") || ival(v[i], "tn") != ival(v[i + 1], "tn") ||
        ival(v[i], "ms") != ival(v[i + 1], "ms")) paired = false;
    if (ival(v[i], "boot_id") != (long long)g_boot) sameBoot = false;
    if (ival(v[i], "tn") <= lastTn) inc = false;
    lastTn = ival(v[i], "tn");
  }
  ck("주기 tel 5개 이상(20 Hz)", nt >= 5, std::to_string(nt));
  ck("tel 바로 뒤 같은 tn·ms 의 mot", paired);
  ck("tel boot_id = INFO boot_id", sameBoot);
  ck("tn 증가", inc);
  const std::string mo = last("mot"), te = last("tel");
  ck("mot: busy NONE, busy_seq 생략", sval(mo, "busy") == "NONE" && !has(mo, "busy_seq"), mo);
  ck("mot: rearm_lock false, vac_released false(해제 판정값·압력 변환 없음)",
     bval(mo, "rearm_lock") == 0 && bval(mo, "vac_released") == 0, mo);
  ck("mot: 엔코더 무효 → drv_settled 생략, 원점 없음 → 위치 생략",
     !has(mo, "drv_settled") && !has(mo, "drv_pos_mm") && !has(mo, "head_pos_mm"), mo);
  ck("tel: IDLE, faults [], 측정 불가 필드 생략",
     sval(te, "state") == "IDLE" && te.find("\"faults\":[]") != std::string::npos && !has(te, "vac_kPa"), te);
  R s = query("GET_STATUS");
  std::vector<std::string> t = pick(s.out, "tel"), mm = pick(s.out, "mot");
  ck("GET_STATUS → ACK, tel, mot 같은 tn", s.ok() && t.size() >= 1 && mm.size() >= 1 &&
     ival(t.back(), "tn") == ival(mm.back(), "tn"), summary(s));
  Serial.room = 100;                                   /* 송신 버퍼 부족 */
  const size_t m2 = mark();
  stepMs(160);
  std::vector<std::string> v2(g_lines.begin() + (long)m2, g_lines.end());
  ck("송신 버퍼가 모자라면 tel·mot 을 버림", pick(v2, "tel").empty() && pick(v2, "mot").empty() && g_telDrops > 0,
     std::to_string(g_telDrops));
  R p = query("PING");
  ck("그래도 ACK 는 보냄", p.ok(), summary(p));
  Serial.room = 6144;
}

void scAid() {
  R a = exec("ARM");
  ck("bid·aid 없음 → AID_REQUIRED", !a.ok() && a.why() == "AID_REQUIRED" && a.err() == "2-CU-011", summary(a));
  a = exec("ARM", "", 1, BID_NONE);
  ck("bid 없음 → AID_REQUIRED", a.why() == "AID_REQUIRED", summary(a));
  a = exec("ARM", "", 1, BID_WRONG);
  ck("bid 다름 → BOOT_MISMATCH", a.why() == "BOOT_MISMATCH", summary(a));
  a = exec("ARM", "", 0);
  ck("aid 0 → AID_REQUIRED", a.why() == "AID_REQUIRED", summary(a));
  ck("기록하지 않는 거부는 last_* 를 만들지 않음", !has(info(), "last_aid"));

  R arm = exec("ARM", "", 1, BID_OK, 10);
  ck("ARM aid 1 → VACUUM_NOT_RELEASED(해제 판정값·압력 변환 미설정)", !arm.ok() && arm.why() == "VACUUM_NOT_RELEASED", summary(arm));
  std::string i = info();
  ck("GET_INFO last_seq 10, last_aid 1", ival(i, "last_seq") == 10 && ival(i, "last_aid") == 1, i);
  R rep = exec("ARM", "", 1, BID_OK, 10);
  ck("같은 seq·aid 재전송 → 원래 ACK 그대로", rep.ack == arm.ack, summary(rep));
  R conf = exec("ARM", "", 1, BID_OK, 11);
  ck("같은 aid, 다른 seq → SEQ_CONFLICT", conf.why() == "SEQ_CONFLICT", summary(conf));
  i = info();
  ck("재전송·충돌 뒤 last_seq·last_aid 그대로", ival(i, "last_seq") == 10 && ival(i, "last_aid") == 1, i);

  R home = exec("HOME", "", 2, BID_OK, 12);
  ck("HOME(IDLE) → NOT_ARMED", home.why() == "NOT_ARMED", summary(home));
  R homeRep = exec("HOME", "", 2, BID_OK, 12);
  ck("거부된 동작 재전송 → 원래 거부 ACK", homeRep.ack == home.ack);
  R hh = exec("HEAD_HOLD", "\"press_mm\":10", 3, BID_OK, 13);
  ck("HEAD_HOLD(IDLE) → NOT_HOMED", hh.why() == "NOT_HOMED", summary(hh));
  R ds = exec("DRIVE_STEP", "\"dist_mm\":-10.5", 4, BID_OK, 14);
  ck("DRIVE_STEP(IDLE) → NOT_HOMED", ds.why() == "NOT_HOMED", summary(ds));
  R dsRep = exec("DRIVE_STEP", "\"dist_mm\":-10.499,\"speed_mm_s\":0", 4, BID_OK, 14);
  ck("정규화가 같은 재전송(-10.499 → -10.50) → 원래 ACK", dsRep.ack == ds.ack, summary(dsRep));
  R dsConf = exec("DRIVE_STEP", "\"dist_mm\":-10.6", 4, BID_OK, 14);
  ck("같은 aid, 다른 파라미터 → SEQ_CONFLICT", dsConf.why() == "SEQ_CONFLICT", summary(dsConf));
  R stale = exec("HOME", "", 2, BID_OK, 12);
  ck("기록 밖 옛 aid 2 → STALE_SEQ", stale.why() == "STALE_SEQ", summary(stale));

  R tr = exec("SET_TEL_RATE", "\"hz\":20", 5, BID_OK, 15);
  ck("SET_TEL_RATE aid 5 → ok", tr.ok(), summary(tr));
  R armOld = exec("ARM", "", 1, BID_OK, 10);
  ck("cmd_last 가 바뀐 뒤 옛 aid 1 → STALE_SEQ", armOld.why() == "STALE_SEQ", summary(armOld));
  R unk = exec("FOO", "", 6, BID_OK, 16);
  ck("모르는 명령 → UNKNOWN_COMMAND", unk.why() == "UNKNOWN_COMMAND" && unk.err() == "1-CM-005", summary(unk));
  i = info();
  ck("모르는 명령·STALE 은 기록하지 않음(last_aid 5, last_seq 15)",
     ival(i, "last_aid") == 5 && ival(i, "last_seq") == 15, i);

  for (int k = 0; k < 2000; k++) Serial.rx += "{\"v\":1,\"t\":\"cmd\",\"seq\":7,\"cmd\":\"GET_STATUS\"}\n";
  stepMs(300);
  R stale2 = exec("HOME", "", 3, BID_OK, 13);
  ck("조회 2000개 뒤 옛 동작 aid 3 → STALE_SEQ", stale2.why() == "STALE_SEQ", summary(stale2));

  R big = exec("SET_TEL_RATE", "\"hz\":20", 4294967295LL, BID_OK, 17);
  ck("aid 4294967295 를 정확히 읽음", big.ok(), summary(big));
  ck("last_aid 4294967295", ival(info(), "last_aid") == 4294967295LL);
  R ex = exec("ARM", "", 7, BID_OK, 18);
  ck("aid 소진 뒤 새 명령 → AID_EXHAUSTED", ex.why() == "AID_EXHAUSTED", summary(ex));
  R exStop = exec("DRIVE_STOP", "", 8, BID_OK, 19);
  ck("aid 소진 뒤에도 정지 명령은 실행", exStop.ok(), summary(exStop));
  i = info();
  ck("그 정지는 last_seq·last_aid 를 바꾸지 않음", ival(i, "last_aid") == 4294967295LL && ival(i, "last_seq") == 17, i);
  R over = rawLine("{\"v\":1,\"t\":\"cmd\",\"seq\":21,\"cmd\":\"ARM\",\"bid\":" + std::to_string(g_boot) +
                 ",\"aid\":4294967296}", 21);
  ck("aid 4294967296(넘침) → AID_REQUIRED", over.why() == "AID_REQUIRED", summary(over));
}

void scStops() {
  unsigned long w0 = driveEnWrites();
  R d = exec("DISARM");
  ck("bid·aid 없는 DISARM 도 정지 출력 먼저", driveEnWrites() > w0);
  ck("…그리고 기록 사유가 아니라 상태 오류(IDLE: NOT_ARMED)", d.why() == "NOT_ARMED", summary(d));
  R p = exec("PAUSE_CPR", "", 3, BID_WRONG);
  ck("bid 틀린 PAUSE_CPR → 상태 오류 NOT_RUNNING", p.why() == "NOT_RUNNING", summary(p));
  ck("기록 사유로 실행한 정지는 last_aid 를 만들지 않음", !has(info(), "last_aid"));
  R s1 = exec("DRIVE_STOP", "", 3, BID_OK, 30);
  ck("DRIVE_STOP 새 aid → ok", s1.ok(), summary(s1));
  ck("last_aid 3", ival(info(), "last_aid") == 3);
  w0 = driveEnWrites();
  R s1r = exec("DRIVE_STOP", "", 3, BID_OK, 30);
  ck("같은 aid 정지 재전송 → 원래 ACK, 정지 출력은 다시", s1r.ack == s1.ack && driveEnWrites() > w0);

  w0 = driveEnWrites();
  R broken = rawLine("{\"v\":1,\"t\":\"cmd\",\"seq\":31,\"cmd\":\"STOP_CPR\"", 31);
  ck("STOP_CPR 이 든 깨진 줄 → 1-CM-003, 정지 아님",
     broken.ack.empty() && broken.errs().size() == 1 && sval(broken.errs()[0], "code") == "1-CM-003" &&
     driveEnWrites() == w0, broken.errs().empty() ? "" : broken.errs()[0]);
  R x = exec("STOP_CPRX", "", 4, BID_OK, 32);
  ck("cmd STOP_CPRX → UNKNOWN_COMMAND, 정지 아님", x.why() == "UNKNOWN_COMMAND" && driveEnWrites() == w0, summary(x));
  R lower = exec("stop_cpr", "", 5, BID_OK, 33);
  ck("cmd stop_cpr(소문자) → UNKNOWN_COMMAND", lower.why() == "UNKNOWN_COMMAND" && driveEnWrites() == w0);
  R notCmd = rawLine("{\"v\":1,\"t\":\"evt\",\"seq\":34,\"cmd\":\"STOP_CPR\"}", 34);
  ck("t 가 cmd 가 아닌 줄 → 무시, 정지 아님", notCmd.out.empty() || pick(notCmd.out, "ack").empty());
  ck("…정지 출력 없음", driveEnWrites() == w0);

  w0 = driveEnWrites();
  R v2 = rawLine("{\"v\":2,\"t\":\"cmd\",\"seq\":35,\"cmd\":\"STOP_CPR\"}", 35);
  ck("v:2 STOP_CPR → 정지 출력 먼저", driveEnWrites() > w0);
  std::vector<std::string> e = v2.errs();
  ck("…그 뒤 3-CM-001 sev 3 latched", e.size() == 1 && sval(e[0], "code") == "3-CM-001" &&
     ival(e[0], "sev") == 3 && bval(e[0], "latched") == 1, e.empty() ? "" : e[0]);
  stepMs(60);
  const std::string te = last("tel");
  ck("상태 FAULT, faults [3-CM-001]", sval(te, "state") == "FAULT" &&
     te.find("\"faults\":[\"3-CM-001\"]") != std::string::npos, te);
  R gf = query("GET_FAULTS");
  std::vector<std::string> fe = gf.errs(), end = gf.evts("FAULTS_END");
  ck("GET_FAULTS → ACK, err 1줄, FAULTS_END n=1", gf.ok() && fe.size() == 1 && end.size() == 1 &&
     ival(end[0], "n") == 1 && ival(end[0], "seq") == (long long)gf.seq, summary(gf));
  R dis = exec("DISARM", "", 6, BID_OK, 36);
  ck("FAULT 에서 DISARM → FAULT_ACTIVE(3-CM-001), 상태 그대로",
     dis.why() == "FAULT_ACTIVE" && dis.err() == "3-CM-001", summary(dis));
  R rf = exec("RESET_FAULT", "", 7, BID_OK, 37);
  ck("RESET_FAULT → FAULT_PERSISTS([구현] 원인 확인 전)", rf.why() == "FAULT_PERSISTS" && rf.err() == "3-CM-001", summary(rf));
  R arm = exec("ARM", "", 8, BID_OK, 38);
  ck("FAULT 에서 ARM → NOT_IDLE", arm.why() == "NOT_IDLE", summary(arm));
}

void scEstop() {
  host::pins().level[PIN_ESTOP] = HIGH;
  const size_t m = mark();
  stepMs(3);
  std::vector<std::string> v(g_lines.begin() + (long)m, g_lines.end());
  std::vector<std::string> e = pick(v, "err");
  ck("비상정지 → 3-MU-002 latched", e.size() == 1 && sval(e[0], "code") == "3-MU-002" && bval(e[0], "latched") == 1);
  R a = exec("ARM", "", 1);
  ck("ESTOP 중 ARM → ESTOP_ACTIVE(3-MU-002)", a.why() == "ESTOP_ACTIVE" && a.err() == "3-MU-002", summary(a));
  ck("ESTOP 중 GET_INFO 허용", !info().empty());
  R gf = query("GET_FAULTS");
  ck("ESTOP 중 GET_FAULTS 허용, 3-MU-002", gf.ok() && gf.errs().size() == 1 && sval(gf.errs()[0], "code") == "3-MU-002");
  R ds = exec("DRIVE_STOP", "", 2);
  ck("ESTOP 중 DRIVE_STOP → ACK ok, 상태 ESTOP", ds.ok() && ds.state() == "ESTOP", summary(ds));
  unsigned long w0 = driveEnWrites();
  R dis = exec("DISARM", "", 3);
  ck("ESTOP 중 DISARM → 정지 출력 뒤 ESTOP_ACTIVE", dis.why() == "ESTOP_ACTIVE" && driveEnWrites() > w0, summary(dis));
  R p = query("PING");
  ck("ESTOP 중 PING", p.ok() && p.state() == "ESTOP");
  stepMs(60);
  const std::string te = last("tel");
  ck("tel estop true, faults [3-MU-002]", bval(te, "estop") == 1 && te.find("\"faults\":[\"3-MU-002\"]") != std::string::npos, te);
}

void scRejects() {
  struct Case { const char* cmd; const char* extra; const char* why; const char* err; };
  const Case cs[] = {
    { "STIFFNESS_SCAN",    "\"points\":8,\"probe_N\":50", "NOT_FIXED",     "2-CU-011" },
    { "TRIAL_COMPRESSION", "\"cycles\":3",                "NOT_READY",     "2-CU-011" },
    { "START_CPR",         "",                            "NOT_READY",     "2-CU-011" },
    { "RESUME_CPR",        "",                            "NOT_PAUSED",    "2-CU-011" },
    { "VACUUM_ON",         "\"target_kPa\":-22.5",        "NOT_HOMED",     "2-CU-011" },
    { "SET_TARGET",        "\"bpm\":110,\"depth_mm\":50", "NOT_ARMED",     "2-CU-011" },
    { "SET_CP_POINT",      "\"offset_mm\":0",             "NOT_FIXED",     "2-CU-011" },
    { "STOP_CPR",          "\"reason\":\"SAFETY\"",       "NOT_RUNNING",   "2-CU-011" },
    { "SET_TEL_RATE",      "\"hz\":999",                  "BAD_RATE",      "1-CM-004" },
  };
  long long aid = 1;
  for (size_t k = 0; k < sizeof cs / sizeof cs[0]; k++) {
    R r = exec(cs[k].cmd, cs[k].extra, aid++);
    ck(std::string(cs[k].cmd) + " → " + cs[k].why, !r.ok() && r.why() == cs[k].why && r.err() == cs[k].err, summary(r));
  }
  R off = exec("VACUUM_OFF", "", aid++);
  ck("VACUUM_OFF(IDLE) → 수락", off.ok() && off.state() == "IDLE", summary(off));
  const size_t m = mark();
  stepMs(800);
  std::vector<std::string> v(g_lines.begin() + (long)m, g_lines.end());
  ck("압력 변환이 없으면 해제 확인이 안 되어 VACUUM_RELEASED 없음", pick(v, "evt", "VACUUM_RELEASED").empty());
  ck("mot vac_released false 유지", bval(last("mot"), "vac_released") == 0);
}

void scComm() {
  R ov = rawLine("{\"v\":1,\"t\":\"cmd\",\"seq\":40,\"cmd\":\"PING\",\"x\":\"" + std::string(600, 'a') + "\"}", 40);
  ck("512 byte 넘는 줄 → 1-CM-003 LINE_TOO_LONG", ov.ack.empty() && ov.errs().size() == 1 &&
     sval(ov.errs()[0], "why") == "LINE_TOO_LONG");
  R crlf = rawLine("{\"v\":1,\"t\":\"cmd\",\"seq\":41,\"cmd\":\"PING\"}\r", 41);
  ck("CRLF 로 온 명령도 처리", crlf.ok(), summary(crlf));
  g_keepAlive = false;
  const size_t m = mark();
  stepMs(2100);
  std::vector<std::string> v(g_lines.begin() + (long)m, g_lines.end());
  std::vector<std::string> e = pick(v, "err");
  ck("2 s 동안 수신 없음 → 3-CM-010 COMM_LOST", e.size() == 1 && sval(e[0], "code") == "3-CM-010", e.empty() ? "" : e[0]);
  ck("상태 FAULT", sval(last("tel"), "state") == "FAULT");
}
#endif  /* !SAVER_HOST_HOOKS */

/* ───────────────────────── 훅 빌드 장면 ───────────────────────── */
#ifdef SAVER_HOST_HOOKS
void waitReleased() { stepMs(350); }   /* vac_release_ms 300 */

R armOk(long long aid) {
  waitReleased();
  return exec("ARM", "", aid);
}

void finishWith(const char* result, const char* code = NULL) {
  fake::result = result;
  fake::code = code;
  fake::finish = true;
}

void scHooksInfo() {
  std::string i = info();
  ck("caps 에 DRIVE·HEAD_HOLD·CP_ZERO(훅 빌드)", i.find("\"caps\":[\"DRIVE\",\"HEAD_HOLD\",\"CP_ZERO\",\"MOT\",\"AID\",\"FAULTS\"]") != std::string::npos, i);
  ck("부팅 직후 vac_released false", bval(last("mot"), "vac_released") == 0 || last("mot").empty());
  R early = exec("ARM", "", 1);
  ck("해제 확인 전 ARM → VACUUM_NOT_RELEASED", early.why() == "VACUUM_NOT_RELEASED", summary(early));
  waitReleased();
  ck("압력 0 kPa 가 300 ms 이어지면 vac_released true", bval(last("mot"), "vac_released") == 1, last("mot"));
  R arm = exec("ARM", "", 2);
  ck("ARM → ARMED", arm.ok() && arm.state() == "ARMED", summary(arm));
}

void scHomeBusy() {
  armOk(1);
  R home = exec("HOME", "", 2, BID_OK, 200);
  ck("HOME → ACK ok(상태는 아직 ARMED)", home.ok() && home.state() == "ARMED", summary(home));
  stepMs(60);
  std::string mo = last("mot");
  ck("mot busy HOMING, busy_seq·busy_aid", sval(mo, "busy") == "HOMING" && ival(mo, "busy_seq") == 200 &&
     ival(mo, "busy_aid") == 2, mo);
  R hh = exec("HEAD_HOLD", "\"press_mm\":10", 3);
  ck("실행 중 다른 동작 → BUSY", hh.why() == "BUSY", summary(hh));
  R st = exec("SET_TARGET", "\"bpm\":110", 4);
  ck("실행 중 즉시 변경 → BUSY", st.why() == "BUSY", summary(st));
  R tr = exec("SET_TEL_RATE", "\"hz\":20", 5);
  ck("실행 중 SET_TEL_RATE 는 받음", tr.ok(), summary(tr));
  R inprog = exec("HOME", "", 2, BID_OK, 200);
  ck("진행 중 재전송 → 원래 ACK 만", inprog.ack == home.ack && inprog.evts("HOMING_DONE").empty());
  finishWith("OK");
  const size_t m = mark();
  stepMs(5);
  std::vector<std::string> v(g_lines.begin() + (long)m, g_lines.end());
  std::vector<std::string> d = pick(v, "evt", "HOMING_DONE");
  ck("HOMING_DONE: 원래 seq·aid, result OK", d.size() == 1 && ival(d[0], "seq") == 200 && ival(d[0], "aid") == 2 &&
     sval(d[0], "result") == "OK", d.empty() ? "" : d[0]);
  stepMs(60);
  mo = last("mot");
  ck("busy NONE, mot_last HOME/OK", sval(mo, "busy") == "NONE" && sval(mo, "mot_last_cmd") == "HOME" &&
     sval(mo, "mot_last_result") == "OK" && ival(mo, "mot_last_aid") == 2, mo);
  ck("상태 HOMED", sval(last("tel"), "state") == "HOMED");
  R done = exec("HOME", "", 2, BID_OK, 200);
  ck("끝난 뒤 재전송 → 원래 ACK 와 저장된 HOMING_DONE", done.ack == home.ack &&
     done.evts("HOMING_DONE").size() == 1 && done.evts("HOMING_DONE")[0] == d[0]);
  ck("재전송은 재실행하지 않음", fake::home_starts == 1);
}

void homeOk(long long aid) {
  exec("HOME", "", aid);
  finishWith("OK");
  stepMs(5);
}

void scHeadHold() {
  armOk(1);
  homeOk(2);
  R pr0 = exec("HEAD_HOLD", "\"press_mm\":0", 3);
  ck("press_mm 0 → PRESS_RANGE", pr0.why() == "PRESS_RANGE", summary(pr0));
  R pr1 = exec("HEAD_HOLD", "\"press_mm\":20", 4);
  ck("press_mm > head_press_max_mm → PRESS_RANGE", pr1.why() == "PRESS_RANGE", summary(pr1));
  fake::contact_clear = false;
  R inc = exec("HEAD_HOLD", "\"press_mm\":10", 5);
  ck("시작 때 이미 접촉 → IN_CONTACT", inc.why() == "IN_CONTACT", summary(inc));
  fake::contact_clear = true;
  R hh = exec("HEAD_HOLD", "\"press_mm\":10,\"drv_zero\":true", 6, BID_OK, 300);
  ck("HEAD_HOLD → ok, busy HEAD_SEARCH", hh.ok() && (stepMs(60), sval(last("mot"), "busy") == "HEAD_SEARCH"), summary(hh));
  R stop = exec("DRIVE_STOP", "", 7, BID_OK, 301);
  std::vector<std::string> held = stop.evts("HEAD_HELD");
  ck("DRIVE_STOP → HEAD_HELD STOPPED(원래 seq·aid)", held.size() == 1 && ival(held[0], "seq") == 300 &&
     ival(held[0], "aid") == 6 && sval(held[0], "result") == "STOPPED", held.empty() ? "" : held[0]);
  ck("DRIVE_STOP 자체는 ACK ok", stop.ok(), summary(stop));
  ck("완료 이벤트가 ACK 보다 먼저", !held.empty() && !stop.ack.empty() &&
     std::find(stop.out.begin(), stop.out.end(), held[0]) < std::find(stop.out.begin(), stop.out.end(), stop.ack));
  ck("헤드 하강 목표 취소 훅 호출", fake::cancels > 0);
  stepMs(60);
  ck("중단 뒤 head_hold_active false, 주행 원점 없음", bval(last("mot"), "head_hold_active") == 0 &&
     !has(last("mot"), "drv_pos_mm"), last("mot"));
  R again = exec("HEAD_HOLD", "\"press_mm\":10", 8);
  ck("헤드가 HOME 위치가 아니면 HEAD_NOT_READY", again.why() == "HEAD_NOT_READY", summary(again));
  R ds = exec("DRIVE_STEP", "\"dist_mm\":10", 9);
  ck("DRIVE_STEP 도 HEAD_NOT_READY", ds.why() == "HEAD_NOT_READY", summary(ds));
  homeOk(10);
  R ok = exec("HEAD_HOLD", "\"press_mm\":10,\"drv_zero\":true", 11, BID_OK, 302);
  fake::rep.head_contact_ok = true; fake::rep.head_contact_mm = 31.2f;
  fake::rep.head_pos_ok = true; fake::rep.head_pos_mm = 41.2f;
  fake::rep.force_ok = true; fake::rep.cp_force_N = 18.4f;
  finishWith("OK");
  const size_t m = mark();
  stepMs(5);
  std::vector<std::string> v(g_lines.begin() + (long)m, g_lines.end());
  std::vector<std::string> h = pick(v, "evt", "HEAD_HELD");
  ck("HEAD_HELD OK 와 잰 값", ok.ok() && h.size() == 1 && h[0].find("\"seq\":302,\"aid\":11,\"result\":\"OK\","
     "\"head_contact_mm\":31.20,\"head_pos_mm\":41.20,\"cp_force_N\":18.40") != std::string::npos, h.empty() ? "" : h[0]);
  stepMs(60);
  const std::string mo = last("mot");
  ck("head_hold_active true, head_contact_mm 31.20, 주행 원점 생김", bval(mo, "head_hold_active") == 1 &&
     mo.find("\"head_contact_mm\":31.20") != std::string::npos && has(mo, "drv_pos_mm") && fake::zeros == 1, mo);
  R already = exec("HEAD_HOLD", "\"press_mm\":10", 12);
  ck("유지 중 새 HEAD_HOLD → ALREADY_HOLDING", already.why() == "ALREADY_HOLDING", summary(already));
  R stop2 = exec("DRIVE_STOP", "", 13);
  stepMs(60);
  ck("DRIVE_STOP 은 이미 성립한 헤드 유지를 풀지 않음", stop2.ok() && bval(last("mot"), "head_hold_active") == 1);
}

void scDrive() {
  armOk(1);
  homeOk(2);
  exec("HEAD_HOLD", "\"press_mm\":10,\"drv_zero\":true", 3);
  finishWith("OK");
  stepMs(5);
  R r0 = exec("DRIVE_STEP", "\"dist_mm\":0", 4);
  ck("dist_mm 0 → DIST_RANGE", r0.why() == "DIST_RANGE", summary(r0));
  R r1 = exec("DRIVE_STEP", "\"dist_mm\":-25", 5);
  ck("|dist_mm| > drv_step_max_mm → DIST_RANGE", r1.why() == "DIST_RANGE", summary(r1));
  R r2 = exec("DRIVE_STEP", "\"dist_mm\":10,\"speed_mm_s\":50", 6);
  ck("speed_mm_s > 최대 → SPEED_RANGE", r2.why() == "SPEED_RANGE", summary(r2));
  R go = exec("DRIVE_STEP", "\"dist_mm\":10", 7, BID_OK, 400);
  stepMs(60);
  ck("DRIVE_STEP → ok, busy DRIVE, drv_settled false", go.ok() && sval(last("mot"), "busy") == "DRIVE" &&
     bval(last("mot"), "drv_settled") == 0, last("mot"));
  fake::rep.drive_ok = true; fake::rep.drive_mm = 9.96f; fake::rep.drv_pos_ok = true; fake::rep.drv_pos_mm = 40.1f;
  fake::rep.drv_yaw_ok = true; fake::rep.drv_yaw_deg = 0.6f; fake::rep.force_ok = true; fake::rep.cp_force_N = 28.4f;
  fake::rep.force_n_ok = true; fake::rep.force_n = 40;
  finishWith("OK");
  size_t m = mark();
  stepMs(5);
  std::vector<std::string> v(g_lines.begin() + (long)m, g_lines.end());
  std::vector<std::string> dd = pick(v, "evt", "DRIVE_DONE");
  ck("DRIVE_DONE 초안 §4.2 형식", dd.size() == 1 && dd[0].find("\"seq\":400,\"aid\":7,\"result\":\"OK\",\"drive_mm\":9.96,"
     "\"drv_pos_mm\":40.10,\"drv_yaw_deg\":0.60,\"cp_force_N\":28.40,\"force_n\":40") != std::string::npos,
     dd.empty() ? "" : dd[0]);
  exec("DRIVE_STEP", "\"dist_mm\":10", 8);
  fake::settled = false;
  finishWith("SETTLE_FAIL");
  stepMs(5);
  R ns = exec("DRIVE_STEP", "\"dist_mm\":10", 9);
  ck("SETTLE_FAIL 뒤 정지 확인 전 → NOT_SETTLED", ns.why() == "NOT_SETTLED", summary(ns));
  fake::settled = true;
  stepMs(5);
  R go2 = exec("DRIVE_STEP", "\"dist_mm\":10", 10, BID_OK, 401);
  ck("정지 확인 뒤 DRIVE_STEP 수락", go2.ok(), summary(go2));
  finishWith("SHORT", "2-TU-004");
  m = mark();
  stepMs(5);
  std::vector<std::string> v2(g_lines.begin() + (long)m, g_lines.end());
  std::vector<std::string> e = pick(v2, "err"), d2 = pick(v2, "evt", "DRIVE_DONE");
  ck("SHORT: err 2-TU-004 를 따로, DRIVE_DONE SHORT code 2-TU-004",
     e.size() == 1 && sval(e[0], "code") == "2-TU-004" && bval(e[0], "latched") == 0 && d2.size() == 1 &&
     sval(d2[0], "result") == "SHORT" && sval(d2[0], "code") == "2-TU-004", d2.empty() ? "" : d2[0]);
  ck("…err 가 이벤트보다 먼저", !e.empty() && !d2.empty() &&
     std::find(v2.begin(), v2.end(), e[0]) < std::find(v2.begin(), v2.end(), d2[0]));
  stepMs(60);
  ck("2-TU-004 는 활성 오류 목록에 남지 않음", last("tel").find("\"faults\":[]") != std::string::npos, last("tel"));
  R go3 = exec("DRIVE_STEP", "\"dist_mm\":10", 11);
  finishWith("ABORTED", "2-TU-001");
  stepMs(5);
  R yaw = exec("DRIVE_STEP", "\"dist_mm\":10", 12);
  ck("방향 이탈(원인 남음) 동안 DRIVE_STEP → FAULT_ACTIVE(2-TU-001)", go3.ok() && yaw.why() == "FAULT_ACTIVE" &&
     yaw.err() == "2-TU-001", summary(yaw));
  clearFaultCause("2-TU-001");
  R ok4 = exec("DRIVE_STEP", "\"dist_mm\":10", 13);
  ck("원인 해소 뒤 DRIVE_STEP 수락", ok4.ok(), summary(ok4));
}

void scFaultJob() {
  armOk(1);
  homeOk(2);
  exec("HEAD_HOLD", "\"press_mm\":10", 3);
  finishWith("OK");
  stepMs(5);
  R go = exec("DRIVE_STEP", "\"dist_mm\":10", 4, BID_OK, 500);
  unsigned long w0 = driveEnWrites();
  fake::raise3 = "3-TU-003";
  const size_t m = mark();
  stepMs(5);
  std::vector<std::string> v(g_lines.begin() + (long)m, g_lines.end());
  std::vector<std::string> e = pick(v, "err"), d = pick(v, "evt", "DRIVE_DONE");
  ck("반력 상한 → 출력 차단", go.ok() && driveEnWrites() > w0);
  ck("err 3-TU-003 이 먼저, 그 뒤 DRIVE_DONE FAULT code 3-TU-003",
     e.size() == 1 && d.size() == 1 && sval(d[0], "result") == "FAULT" && sval(d[0], "code") == "3-TU-003" &&
     ival(d[0], "aid") == 4 && std::find(v.begin(), v.end(), e[0]) < std::find(v.begin(), v.end(), d[0]),
     d.empty() ? "" : d[0]);
  stepMs(60);
  ck("상태 FAULT, 헤드 유지 끝, faults [3-TU-003]", sval(last("tel"), "state") == "FAULT" &&
     bval(last("mot"), "head_hold_active") == 0 && last("tel").find("\"faults\":[\"3-TU-003\"]") != std::string::npos);
  R h = exec("HOME", "", 5);
  ck("FAULT 중 동작 명령 → FAULT_ACTIVE(3-TU-003)", h.why() == "FAULT_ACTIVE" && h.err() == "3-TU-003", summary(h));
}

void scVacuum() {
  armOk(1);
  homeOk(2);
  R on = exec("VACUUM_ON", "\"target_kPa\":-22.5", 3);
  stepMs(60);
  ck("VACUUM_ON → ok, 출력 켜짐이라 vac_released false", on.ok() && fake::vac_out &&
     bval(last("mot"), "vac_released") == 0, last("mot"));
  ck("흡착 완료 판정 전에는 HOMED 그대로(VACUUM_OK 를 꾸며 보내지 않음)", sval(last("tel"), "state") == "HOMED");
  R dis = exec("DISARM", "", 4);
  stepMs(60);
  ck("음압 출력 켜짐에서 DISARM → VACUUM_HOLD, rearm_lock true", dis.ok() && dis.state() == "VACUUM_HOLD" &&
     bval(last("mot"), "rearm_lock") == 1, summary(dis));
  const char* blocked[] = { "ARM", "SET_TARGET", "HEAD_HOLD", "DRIVE_STEP", "SET_CP_POINT", "START_CPR", "RESUME_CPR", "VACUUM_ON" };
  long long aid = 5;
  bool allLocked = true;
  for (size_t k = 0; k < sizeof blocked / sizeof blocked[0]; k++) {
    R r = exec(blocked[k], !strcmp(blocked[k], "HEAD_HOLD") ? "\"press_mm\":10" :
                           !strcmp(blocked[k], "DRIVE_STEP") ? "\"dist_mm\":10" : "", aid++);
    if (r.why() != "REARM_LOCKED") { allLocked = false; std::cout << "    " << blocked[k] << ": " << r.ack << "\n"; }
  }
  ck("재가동 명령 → REARM_LOCKED", allLocked);
  R tr = exec("SET_TEL_RATE", "\"hz\":20", aid++);
  ck("SET_TEL_RATE 는 받음", tr.ok(), summary(tr));
  R home = exec("HOME", "", aid++);
  finishWith("OK");
  stepMs(65);
  ck("HOME(헤드 복귀)은 허용, rearm_lock 유지, VACUUM_HOLD", home.ok() && bval(last("mot"), "rearm_lock") == 1 &&
     sval(last("tel"), "state") == "VACUUM_HOLD", summary(home));
  fake::vac_kPa = -20.0f;
  R off = exec("VACUUM_OFF", "", aid++, BID_OK, 600);
  stepMs(60);
  ck("VACUUM_OFF ACK 뒤에도 VACUUM_HOLD·rearm_lock 그대로", off.ok() && sval(last("tel"), "state") == "VACUUM_HOLD" &&
     bval(last("mot"), "rearm_lock") == 1);
  size_t m = mark();
  stepMs(5200);
  std::vector<std::string> v(g_lines.begin() + (long)m, g_lines.end());
  std::vector<std::string> to = pick(v, "err");
  ck("해제 대기 시간 초과 → 2-VU-008 한 번, 비래치", to.size() == 1 && sval(to[0], "code") == "2-VU-008" &&
     bval(to[0], "latched") == 0, to.empty() ? "" : to[0]);
  ck("…상태·rearm_lock 그대로", sval(last("tel"), "state") == "VACUUM_HOLD" && bval(last("mot"), "rearm_lock") == 1);
  fake::vac_kPa = -0.3f;
  m = mark();
  stepMs(400);
  std::vector<std::string> v2(g_lines.begin() + (long)m, g_lines.end());
  std::vector<std::string> rel = pick(v2, "evt", "VACUUM_RELEASED");
  ck("해제 확인 → VACUUM_RELEASED(VACUUM_OFF 의 seq·aid)", rel.size() == 1 && ival(rel[0], "seq") == 600 &&
     ival(rel[0], "aid") == aid - 1 && rel[0].find("\"vac_kPa\":-0.30") != std::string::npos, rel.empty() ? "" : rel[0]);
  stepMs(60);
  ck("IDLE, rearm_lock false, 2-VU-008 해소", sval(last("tel"), "state") == "IDLE" &&
     bval(last("mot"), "rearm_lock") == 0 && last("tel").find("\"faults\":[]") != std::string::npos, last("tel"));
  R arm = exec("ARM", "", aid++);
  ck("다시 ARM 가능", arm.ok(), summary(arm));
}

void scCpZero() {
  armOk(1);
  homeOk(2);
  exec("VACUUM_ON", "", 3);
  fake::vac_reached = true;
  size_t m = mark();
  stepMs(5);
  std::vector<std::string> v(g_lines.begin() + (long)m, g_lines.end());
  ck("흡착 완료 판정 → VACUUM_OK, VACUUM_HOLD", pick(v, "evt", "VACUUM_OK").size() == 1);
  R idx = exec("SET_CP_POINT", "\"idx\":4", 4);
  ck("v1.1 SET_CP_POINT 에 idx → DIST_RANGE", idx.why() == "DIST_RANGE", summary(idx));
  R cp = exec("SET_CP_POINT", "\"offset_mm\":0", 5, BID_OK, 700);
  stepMs(60);
  ck("SET_CP_POINT → ok, busy CP_ZERO", cp.ok() && sval(last("mot"), "busy") == "CP_ZERO");
  R st = exec("DRIVE_STOP", "", 6);
  std::vector<std::string> z = st.evts("CP_ZERO_SET");
  ck("중단 → CP_ZERO_SET STOPPED", z.size() == 1 && sval(z[0], "result") == "STOPPED" && ival(z[0], "seq") == 700);
  R tc = exec("TRIAL_COMPRESSION", "\"cycles\":3", 7);
  ck("영점 없이 VACUUM_HOLD 에서 TRIAL_COMPRESSION → NOT_READY", tc.why() == "NOT_READY", summary(tc));
  R cp2 = exec("SET_CP_POINT", "\"offset_mm\":0", 8);
  ck("헤드가 HOME 위치가 아니면 SET_CP_POINT → HEAD_NOT_READY", cp2.why() == "HEAD_NOT_READY", summary(cp2));
  homeOk(9);
  ck("VACUUM_HOLD 에서 HOME 은 헤드만 복귀", sval(last("tel"), "state") == "VACUUM_HOLD" || (stepMs(60), sval(last("tel"), "state") == "VACUUM_HOLD"));
  exec("SET_CP_POINT", "\"offset_mm\":0", 10);
  fake::rep.head_contact_ok = true; fake::rep.head_contact_mm = 30.0f;
  finishWith("OK");
  m = mark();
  stepMs(5);
  std::vector<std::string> v2(g_lines.begin() + (long)m, g_lines.end());
  std::vector<std::string> z2 = pick(v2, "evt", "CP_ZERO_SET");
  ck("CP_ZERO_SET OK, head_contact_mm", z2.size() == 1 && z2[0].find("\"result\":\"OK\",\"head_contact_mm\":30.00") != std::string::npos,
     z2.empty() ? "" : z2[0]);
  stepMs(60);
  ck("상태 CPR_READY", sval(last("tel"), "state") == "CPR_READY");
  R h = exec("HOME", "", 11);
  ck("CPR_READY 에서 HOME → NOT_ARMED", h.why() == "NOT_ARMED", summary(h));
  R s0 = exec("START_CPR", "", 12);
  ck("목표 깊이 미설정 → TARGET_NOT_SET(2-CU-012)", s0.why() == "TARGET_NOT_SET" && s0.err() == "2-CU-012", summary(s0));
  R tg = exec("SET_TARGET", "\"bpm\":110,\"depth_mm\":50,\"force_max_N\":600", 13);
  R s1 = exec("START_CPR", "", 14);
  ck("SET_TARGET 뒤 START_CPR → CPR_RUNNING", tg.ok() && s1.ok() && s1.state() == "CPR_RUNNING", summary(s1));
  R p = exec("PAUSE_CPR", "", 15);
  R rs = exec("RESUME_CPR", "", 16);
  R sp = exec("STOP_CPR", "\"reason\":\"END\"", 17);
  ck("PAUSE → CPR_PAUSED, RESUME → CPR_RUNNING, STOP → HOMED", p.state() == "CPR_PAUSED" &&
     rs.state() == "CPR_RUNNING" && sp.state() == "HOMED", summary(sp));
}

void scDisarmIdle() {
  armOk(1);
  R d = exec("DISARM", "", 2);
  stepMs(60);
  ck("음압 출력 꺼짐·해제 확인 상태의 DISARM → IDLE, rearm_lock false", d.ok() && d.state() == "IDLE" &&
     bval(last("mot"), "rearm_lock") == 0, summary(d));
  fake::vac_ok = false;
  stepMs(10);
  R a = exec("ARM", "", 3);
  ck("압력센서 무효 → vac_released false → ARM 거부", a.why() == "VACUUM_NOT_RELEASED", summary(a));
  fake::vac_ok = true;
  R a2 = armOk(4);
  R d2 = exec("DISARM", "", 5);
  ck("다시 ARM 뒤 DISARM → IDLE", a2.ok() && d2.state() == "IDLE");
  fake::vac_ok = false;
  stepMs(10);
  R a3 = exec("ARM", "", 6);
  fake::vac_ok = true;
  ck("(확인) 센서 무효면 ARM 거부", a3.why() == "VACUUM_NOT_RELEASED");
}

void scDisarmUnreleased() {
  armOk(1);
  homeOk(2);
  fake::vac_ok = false;                         /* 출력은 꺼져 있지만 해제를 확인할 수 없음 */
  stepMs(10);
  R d = exec("DISARM", "", 3);
  stepMs(60);
  ck("해제 미확인(센서 무효)에서 DISARM → VACUUM_HOLD, rearm_lock", d.state() == "VACUUM_HOLD" &&
     bval(last("mot"), "rearm_lock") == 1, summary(d));
}
#endif  /* SAVER_HOST_HOOKS */

struct Scenario { const char* name; void (*fn)(); };
const Scenario SCENARIOS[] = {
#ifndef SAVER_HOST_HOOKS
  { "info", scInfo }, { "telmot", scTelMot }, { "aid", scAid }, { "stops", scStops },
  { "estop", scEstop }, { "rejects", scRejects }, { "comm", scComm },
#else
  { "hooks_info", scHooksInfo }, { "home_busy", scHomeBusy }, { "head_hold", scHeadHold },
  { "drive", scDrive }, { "fault_job", scFaultJob }, { "vacuum", scVacuum }, { "cp_zero", scCpZero },
  { "disarm_idle", scDisarmIdle }, { "disarm_unreleased", scDisarmUnreleased },
#endif
};
const size_t N_SCEN = sizeof SCENARIOS / sizeof SCENARIOS[0];

int runOne(const char* name) {
  for (size_t k = 0; k < N_SCEN; k++) {
    if (strcmp(SCENARIOS[k].name, name)) continue;
    std::cout << "\n[" << name << "]\n";
    start();
    SCENARIOS[k].fn();
    finalChecks();
    std::cout << "  (" << name << ": 통과 " << g_pass << " / 실패 " << g_fail << ")\n";
    return g_fail == 0 ? 0 : 1;
  }
  std::cerr << "없는 장면: " << name << "\n";
  return 2;
}

#if defined(__unix__) || defined(__APPLE__)
volatile sig_atomic_t g_stop = 0;
void onSignal(int) { g_stop = 1; }

int runPty() {
  int m = posix_openpt(O_RDWR | O_NOCTTY);
  if (m < 0 || grantpt(m) != 0 || unlockpt(m) != 0) { perror("pty"); return 2; }
  const char* name = ptsname(m);
  int s = open(name, O_RDWR | O_NOCTTY);           /* 슬레이브를 열어 두어야 마스터 읽기가 EIO 가 되지 않는다 */
  struct termios t;
  if (s >= 0 && tcgetattr(s, &t) == 0) { cfmakeraw(&t); tcsetattr(s, TCSANOW, &t); }
  if (tcgetattr(m, &t) == 0) { cfmakeraw(&t); tcsetattr(m, TCSANOW, &t); }
  fcntl(m, F_SETFL, fcntl(m, F_GETFL) | O_NONBLOCK);
  Serial.pty_fd = m;
#ifdef SAVER_HOST_HOOKS
  simSettings();
  fake::autoMode = true;
#endif
  signal(SIGINT, onSignal);
  signal(SIGTERM, onSignal);
  struct timespec t0, now;
  clock_gettime(CLOCK_MONOTONIC, &t0);
  host::pins().now_us = 1000;
  setup();
  std::printf("PTY %s\n", name);
  std::fflush(stdout);
  while (!g_stop) {
    clock_gettime(CLOCK_MONOTONIC, &now);
    host::pins().now_us = 1000 + (uint64_t)(now.tv_sec - t0.tv_sec) * 1000000u +
                          (uint64_t)((now.tv_nsec - t0.tv_nsec) / 1000);
    loop();
    usleep(200);
  }
  std::printf("tel 버림 %lu, 송신 실패 %lu\n", (unsigned long)g_telDrops, (unsigned long)g_txFail);
  return 0;
}
#endif
}  /* namespace */

int main(int argc, char** argv) {
  if (argc >= 2 && !strcmp(argv[1], "--pty")) {
#if defined(__unix__) || defined(__APPLE__)
    return runPty();
#else
    std::cerr << "--pty 는 리눅스·맥에서만\n";
    return 2;
#endif
  }
  if (argc >= 2) return runOne(argv[1]);
  /* 장면마다 새 프로세스: 펌웨어 전역 상태가 장면 사이에 남지 않게 */
  int failed = 0;
  for (size_t k = 0; k < N_SCEN; k++) {
    std::string c = std::string("\"") + argv[0] + "\" " + SCENARIOS[k].name;
    std::fflush(stdout);
    if (std::system(c.c_str()) != 0) failed++;
  }
  std::cout << "\n" << (failed == 0 ? "전체 통과" : "실패 있음") << "  (장면 " << N_SCEN << "개, 실패 " << failed << ")\n";
  return failed == 0 ? 0 : 1;
}
