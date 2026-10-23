/* saver_proto.h · saver_v11.h 단위시험 — PC에서 실행 (teensy 폴더에서)
 *   g++ -std=c++11 -Wall -Wextra -o test test_saver_proto.cpp && ./test
 *
 * v1.1 (2026-10-23, 신상현(Claude Code) 작성, 한건민 검토 필요): 초안 §8 의 Teensy 항목 중 순수 논리
 *   - 숫자 형식(§3.4), 엄격한 JSON 검사(§2.5-0), 32비트 정수, 필드 생략, evt 의 seq·aid
 *   - 파라미터 정규화와 SEQ_CONFLICT, aid_max·act_cur·act_rej·cmd_last 판단 순서, BOOT_MISMATCH,
 *     GET_INFO last_seq·last_aid 불변 조건, 정지 방향 예외, AID_EXHAUSTED
 *   - faults 순서·자르기, 모든 메시지 종류의 최악값 길이(본문 ≤ 511)
 *   - 최악값 줄을 ../protocol/examples/v11_worst_case.jsonl 과 대조한다(젯슨 시험이 같은 파일을 protocol.decode 로 읽는다).
 *     생성기를 바꾸면 ./test --write-fixture 로 파일을 다시 만든다. */
#include "saver_proto.h"
#include "saver_v11.h"
#include <string>
#include <vector>
#include <cmath>
#include <fstream>
#include <sstream>
#include <iostream>

static int pass = 0, fail = 0;
static void ck(const char* name, bool ok, const std::string& detail = "")
{
    std::cout << (ok ? "  OK   " : "  FAIL ") << name;
    if (!detail.empty()) std::cout << "  <" << detail << ">";
    std::cout << "\n";
    ok ? pass++ : fail++;
}

static std::string f2(double v)
{
    char b[24];
    return saverF2(b, sizeof b, v) ? std::string(b) : std::string("(생략)");
}

/* ── 최악값 줄 (§3.4): 형식 규칙의 최댓값으로 모든 메시지 종류를 만든다 ───────────── */
static const unsigned long U32 = 4294967295ul;
static const unsigned U16 = 65535u;
static const float F = -9999.99f;

static std::vector<std::pair<std::string, std::string> > worstLines()
{
    std::vector<std::pair<std::string, std::string> > v;
    char b[SAVER_MAX_LINE];
    int n;

    n = buildAck(b, sizeof b, U16, true, "VACUUM_HOLD", NULL, NULL);
    v.push_back(std::make_pair("ack ok", n > 0 ? std::string(b) : ""));
    n = buildAck(b, sizeof b, U16, false, NULL, "3-TU-010", "VACUUM_NOT_RELEASED");
    v.push_back(std::make_pair("ack 거부", n > 0 ? std::string(b) : ""));
    n = buildErr(b, sizeof b, U32, "3-TU-010", 3, true, "XXXXXXXXXXXXXXXXXXXXXXXX");   /* why 24자 */
    v.push_back(std::make_pair("err", n > 0 ? std::string(b) : ""));
    n = buildEvt(b, sizeof b, U32, "VACUUM_RELEASED");
    v.push_back(std::make_pair("evt 기본", n > 0 ? std::string(b) : ""));

    static const char* const faults[8] = { "3-TU-010", "3-TU-010", "3-TU-010", "3-TU-010",
                                           "3-TU-010", "3-TU-010", "3-TU-010", "3-TU-010" };
    Telemetry t = {};
    t.state = "VACUUM_HOLD"; t.ms = U32;
    t.cp_rate_cpm = F; t.cp_rate_ok = true;   t.cp_depth_mm = F; t.cp_depth_ok = true;
    t.cp_force_N = F; t.cp_force_ok = true;   t.cp_motor_i_A = F; t.cp_motor_i_ok = true;
    t.vac_kPa = F; t.vac_ok = true;           t.ppg_hr_bpm = F; t.ppg_hr_ok = true;
    t.spo2_pct = F; t.spo2_ok = true;         t.ppg_sqi = F; t.ppg_sqi_ok = true;
    t.temp_C = F; t.temp_ok = true;           t.fan_pct = F; t.fan_ok = true;
    t.sync = true; t.tn = U32; t.boot_id = U16;
    t.faults_list = faults; t.faults_n = 8;   /* 6개로 잘리고 faults_more */
    n = buildTel(b, sizeof b, t);
    v.push_back(std::make_pair("tel", n > 0 ? std::string(b) : ""));

    MotStatus m = {};
    m.ms = U32; m.tn = U32; m.boot_id = U16; m.busy = "HEAD_SEARCH";
    m.busy_ids = true; m.busy_seq = U16; m.busy_aid = U32;
    m.head_hold_active = false; m.drv_settled_ok = true; m.drv_settled = false;
    m.drv_pos_ok = true; m.drv_pos_mm = F;   m.drv_yaw_ok = true; m.drv_yaw_deg = F;
    m.drv_pitch_ok = true; m.drv_pitch_deg = F; m.drv_roll_ok = true; m.drv_roll_deg = F;
    m.head_pos_ok = true; m.head_pos_mm = F; m.head_contact_ok = true; m.head_contact_mm = F;
    m.last_ok = true; m.last_seq = U16; m.last_aid = U32;
    m.last_cmd = "TRIAL_COMPRESSION"; m.last_result = "SETTLE_FAIL";
    m.rearm_lock = false; m.vac_released = false;
    n = buildMot(b, sizeof b, m);
    v.push_back(std::make_pair("mot", n > 0 ? std::string(b) : ""));

    static const char* const caps[6] = { "DRIVE", "HEAD_HOLD", "CP_ZERO", "MOT", "AID", "FAULTS" };
    InfoMsg im = {};
    im.ms = U32; im.seq = U16; im.fw = "XXXXXXXXXXXXXXXX"; im.caps = caps; im.caps_n = 6; im.boot_id = U16;
    im.has_last_seq = true; im.last_seq = U16; im.has_last_aid = true; im.last_aid = U32;
    im.drv_enc_mode = 1; im.drv_enc_side = 'R';
    n = buildInfo(b, sizeof b, im);
    v.push_back(std::make_pair("INFO", n > 0 ? std::string(b) : ""));

    MotionEvt e = {};
    e.ms = U32; e.evt = "DRIVE_DONE"; e.seq = U16; e.aid = U32; e.result = "SETTLE_FAIL"; e.code = "3-TU-010";
    e.drive_ok = true; e.drive_mm = F; e.drv_pos_ok = true; e.drv_pos_mm = F; e.drv_yaw_ok = true; e.drv_yaw_deg = F;
    e.force_ok = true; e.cp_force_N = F; e.force_n_ok = true; e.force_n = U16; e.drv_enc_mode = 1;
    n = buildMotionEvt(b, sizeof b, e);
    v.push_back(std::make_pair("DRIVE_DONE", n > 0 ? std::string(b) : ""));
    static const char* const headEvts[3] = { "HEAD_HELD", "HOMING_DONE", "CP_ZERO_SET" };
    for (int i = 0; i < 3; i++) {
        MotionEvt h = {};
        h.ms = U32; h.evt = headEvts[i]; h.seq = U16; h.aid = U32; h.result = "NO_CONTACT"; h.code = "3-TU-010";
        h.head_contact_ok = true; h.head_contact_mm = F; h.head_pos_ok = true; h.head_pos_mm = F;
        h.force_ok = true; h.cp_force_N = F;
        n = buildMotionEvt(b, sizeof b, h);
        v.push_back(std::make_pair(headEvts[i], n > 0 ? std::string(b) : ""));
    }
    MotionEvt tr = {};
    tr.ms = U32; tr.evt = "TRIAL_RESULT"; tr.seq = U16; tr.aid = U32; tr.result = "ABORTED"; tr.code = "3-TU-010";
    n = buildMotionEvt(b, sizeof b, tr);
    v.push_back(std::make_pair("TRIAL_RESULT", n > 0 ? std::string(b) : ""));
    n = buildVacReleased(b, sizeof b, U32, U16, U32, true, F);
    v.push_back(std::make_pair("VACUUM_RELEASED", n > 0 ? std::string(b) : ""));
    n = buildFaultsEnd(b, sizeof b, U32, U16, U16);
    v.push_back(std::make_pair("FAULTS_END", n > 0 ? std::string(b) : ""));
    return v;
}

static std::string fixtureText()
{
    std::string s;
    std::vector<std::pair<std::string, std::string> > v = worstLines();
    for (size_t i = 0; i < v.size(); i++) s += v[i].second + "\n";
    return s;
}

static const char* const FIXTURE_PATHS[] = {
    "../protocol/examples/v11_worst_case.jsonl",    /* teensy 폴더에서 실행 */
    "protocol/examples/v11_worst_case.jsonl",       /* 저장소 루트에서 실행 */
};

int main(int argc, char** argv)
{
    if (argc > 1 && std::string(argv[1]) == "--write-fixture") {
        std::ofstream f(FIXTURE_PATHS[0], std::ios::binary);
        if (!f) { std::cerr << "쓰기 실패: " << FIXTURE_PATHS[0] << " (teensy 폴더에서 실행)\n"; return 2; }
        f << fixtureText();
        std::cout << "썼음: " << FIXTURE_PATHS[0] << "\n";
        return 0;
    }

    char s[128]; float f; long i; bool b;
    std::cout << "\n== saver_proto.h 단위시험 ==\n\n[파싱]\n";

    const char* c1 = "{\"v\":1,\"t\":\"cmd\",\"seq\":104,\"cmd\":\"SET_TARGET\","
                     "\"bpm\":110,\"depth_mm\":50.0,\"force_max_N\":600}";
    ck("명령 이름",   jsonStr(c1, "cmd", s, sizeof s) && !strcmp(s, "SET_TARGET"), s);
    bool r; r = jsonInt(c1, "seq", &i);
    ck("정수 seq",    r && i == 104, std::to_string(i));
    r = jsonNum(c1, "depth_mm", &f);
    ck("실수 depth",  r && std::fabs(f - 50.0f) < 1e-6, std::to_string(f));
    ck("버전",        jsonInt(c1, "v", &i) && i == 1);

    const char* c2 = "{\"v\":1,\"t\":\"cmd\",\"seq\":9,\"cmd\":\"STOP_CPR\",\"reason\":\"ROSC_SUSPECTED\"}";
    r = jsonStr(c2, "reason", s, sizeof s);
    ck("reason 문자열", r && !strcmp(s, "ROSC_SUSPECTED"), s);

    const char* c3 = "{\"v\":1,\"t\":\"tel\",\"estop\":false,\"limit_top\":true}";
    ck("bool false",  jsonBool(c3, "estop", &b) && b == false);
    ck("bool true",   jsonBool(c3, "limit_top", &b) && b == true);

    ck("없는 키",      !jsonStr(c1, "nope", s, sizeof s));
    ck("필드 존재 확인", jsonHas(c1, "bpm") && !jsonHas(c1, "spo2_pct"));

    /* 값 문자열 안에 키처럼 보이는 내용이 있어도 오인하면 안 됨 */
    const char* c4 = "{\"v\":1,\"why\":\"bpm\\\":999\",\"bpm\":110}";
    r = jsonNum(c4, "bpm", &f);
    ck("값 속 가짜 키 무시", r && std::fabs(f - 110.0f) < 1e-6, std::to_string(f));

    /* 공백이 섞여도 동작 */
    const char* c5 = "{ \"v\" : 1 , \"seq\" : 77 , \"cmd\" : \"PING\" }";
    ck("공백 허용",    jsonInt(c5, "seq", &i) && i == 77 &&
                      jsonStr(c5, "cmd", s, sizeof s) && !strcmp(s, "PING"));

    /* 잘린 패킷은 반드시 실패해야 함 (통신 오류 시 흔함) */
    ck("잘린 문자열 거부", !jsonStr("{\"cmd\":\"START_C", "cmd", s, sizeof s));
    ck("깨진 JSON 거부",   !jsonInt("garbage!!", "seq", &i));
    ck("빈 줄 거부",       !jsonInt("", "seq", &i));

    /* 버퍼보다 긴 값은 잘리되 넘치지 않아야 함 */
    char tiny[6];
    r = jsonStr(c1, "cmd", tiny, sizeof tiny);
    ck("버퍼 오버런 없음", r && strlen(tiny) == 5, tiny);

    std::cout << "\n[중복 seq 차단 (v1.0 호환)]\n";
    SeqDedupe d;
    d.remember(8);
    ck("중복 감지",     d.isDuplicate(8));
    ck("신규 통과",     !d.isDuplicate(9));
    for (long k = 100; k < 108; k++) d.remember(k);
    ck("9개째에서 밀림", !d.isDuplicate(8) && d.isDuplicate(107));

    std::cout << "\n[생성]\n";
    char buf[SAVER_MAX_LINE];
    buildAck(buf, sizeof buf, 104, true, "CPR_RUNNING", NULL, NULL);
    ck("ACK 성공", std::string(buf) ==
        "{\"v\":1,\"t\":\"ack\",\"seq\":104,\"ok\":true,\"state\":\"CPR_RUNNING\"}", buf);

    buildAck(buf, sizeof buf, 105, false, NULL, "2-CU-011", "NOT_ARMED");
    ck("ACK 거부", strstr(buf, "\"ok\":false") && strstr(buf, "2-CU-011"), buf);

    buildErr(buf, sizeof buf, 201500, "3-CU-021", 3, true, "OVER_CURRENT");
    ck("ERR latched", strstr(buf, "\"latched\":true") && strstr(buf, "\"sev\":3"), buf);

    Telemetry t = {};
    t.state = "CPR_RUNNING"; t.ms = 184320;
    t.cp_rate_cpm = 109.8f; t.cp_rate_ok = true;
    t.cp_depth_mm = 49.2f;  t.cp_depth_ok = true;
    t.vac_kPa = -27.6f;     t.vac_ok = true;
    t.spo2_ok = false;                      /* 미측정 → 생략되어야 함 */
    int n = buildTel(buf, sizeof buf, t);
    ck("TEL 생성",       n > 0 && n < SAVER_MAX_LINE, std::to_string(n) + " byte");
    ck("TEL 음압 부호",  strstr(buf, "\"vac_kPa\":-27.60") != NULL);
    ck("미측정 필드 생략", strstr(buf, "spo2_pct") == NULL, buf);

    /* 생성한 것을 다시 파싱 — 왕복 일치 */
    ck("왕복 파싱", jsonNum(buf, "cp_depth_mm", &f) && std::fabs(f - 49.2f) < 0.01f);
    ck("왕복 상태",  jsonStr(buf, "state", s, sizeof s) && !strcmp(s, "CPR_RUNNING"));
    ck("v1.0 tel 에는 tn·boot_id 없음", strstr(buf, "\"tn\"") == NULL && strstr(buf, "boot_id") == NULL);

    /* ───────────────────────────── v1.1 ───────────────────────────── */
    std::cout << "\n[v1.1 숫자 형식 §3.4]\n";
    ck("소수 둘째 자리",        f2(49.2f) == "49.20" && f2(-27.6f) == "-27.60", f2(49.2f) + " " + f2(-27.6f));
    ck("-0.00 → 0.00",          f2(-0.004) == "0.00" && f2(-0.0) == "0.00", f2(-0.004));
    ck("반올림",                f2(0.005) == "0.01" && f2(-1.006) == "-1.01", f2(0.005) + " " + f2(-1.006));
    ck("범위 끝 -9999.99",      f2(-9999.99) == "-9999.99" && f2(9999.99) == "9999.99");
    ck("범위 밖 생략",          f2(9999.996) == "(생략)" && f2(-10000.0) == "(생략)" && f2(1e9) == "(생략)");
    ck("NaN·Inf 생략",          f2(std::nan("")) == "(생략)" && f2(HUGE_VAL) == "(생략)" && f2(-HUGE_VAL) == "(생략)");
    Telemetry tz = {};
    tz.state = "IDLE"; tz.ms = 1; tz.vac_kPa = -0.001f; tz.vac_ok = true;
    tz.temp_C = std::nanf(""); tz.temp_ok = true;               /* 유효라고 해도 NaN 이면 생략 */
    buildTel(buf, sizeof buf, tz);
    ck("tel -0.00 은 0.00",     strstr(buf, "\"vac_kPa\":0.00") != NULL, buf);
    ck("tel NaN 필드 생략",     strstr(buf, "temp_C") == NULL, buf);

    std::cout << "\n[v1.1 32비트 정수]\n";
    uint32_t u = 0;
    ck("aid 최댓값",       jsonU32("{\"aid\":4294967295}", "aid", &u) && u == 4294967295u, std::to_string(u));
    ck("2^24 위 정확",     jsonU32("{\"aid\":16777217}", "aid", &u) && u == 16777217u, std::to_string(u));
    ck("넘침 거부",        !jsonU32("{\"aid\":4294967296}", "aid", &u));
    ck("음수 거부",        !jsonU32("{\"aid\":-1}", "aid", &u));
    ck("소수 거부",        !jsonU32("{\"aid\":3.0}", "aid", &u) && !jsonU32("{\"aid\":3e2}", "aid", &u));
    ck("앞자리 0 거부",    !jsonU32("{\"aid\":007}", "aid", &u) && jsonU32("{\"aid\":0}", "aid", &u) && u == 0);
    ck("문자열 거부",      !jsonU32("{\"aid\":\"5\"}", "aid", &u));

    std::cout << "\n[v1.1 엄격한 JSON 검사 §2.5-0]\n";
    ck("정상 명령",        saverJsonValid("{\"v\":1,\"t\":\"cmd\",\"seq\":1,\"cmd\":\"STOP_CPR\",\"bid\":7,\"aid\":3}"));
    ck("공백·배열·null",   saverJsonValid(" { \"a\" : [ \"x\" , 1 , -2.5e3 , true ] , \"b\" : null } "));
    ck("닫는 괄호 없음",   !saverJsonValid("{\"v\":1,\"t\":\"cmd\",\"seq\":30,\"cmd\":\"STOP_CPR\""));
    ck("뒤에 쓰레기",      !saverJsonValid("{\"cmd\":\"STOP_CPR\"}x"));
    ck("중첩 객체",        !saverJsonValid("{\"a\":{\"cmd\":\"STOP_CPR\"}}"));
    ck("따옴표 없는 키",   !saverJsonValid("{cmd:\"STOP_CPR\"}"));
    ck("닫히지 않은 문자열", !saverJsonValid("{\"cmd\":\"STOP_CPR}"));
    ck("제어문자",         !saverJsonValid("{\"cmd\":\"STOP\tCPR\"}"));
    ck("비 ASCII",         !saverJsonValid("{\"cmd\":\"\xEC\xA0\x95\xEC\xA7\x80\"}"));
    ck("잘못된 수",        !saverJsonValid("{\"seq\":01}") && !saverJsonValid("{\"seq\":1.}") && !saverJsonValid("{\"seq\":+1}"));
    ck("빈 줄",            !saverJsonValid("") && !saverJsonValid("   "));
    std::string longLine = "{\"x\":\"" + std::string(520, 'a') + "\"}";
    ck("511 byte 초과",    !saverJsonValid(longLine.c_str()));
    ck("정지 명령 이름 정확히", saver_v11::isStopCmd("STOP_CPR") && !saver_v11::isStopCmd("STOP_CPRX") &&
                               !saver_v11::isStopCmd("stop_cpr") && !saver_v11::isStopCmd(" STOP_CPR"));

    std::cout << "\n[v1.1 명령 파라미터]\n";
    const char* ds = "{\"v\":1,\"t\":\"cmd\",\"seq\":1208,\"cmd\":\"DRIVE_STEP\",\"bid\":48213,\"aid\":37,\"dist_mm\":-10.50}";
    ck("음수·소수 dist_mm", jsonNum(ds, "dist_mm", &f) && std::fabs(f + 10.5f) < 1e-6, std::to_string(f));
    char p1[128], p2[128], p3[128], p4[128];
    saver_v11::normalizeParams("DRIVE_STEP", ds, 20.0f, p1, sizeof p1);
    ck("정규화: 생략한 speed 를 기본값으로", std::string(p1) == "dist_mm=-10.50;speed_mm_s=20.00;", p1);
    saver_v11::normalizeParams("DRIVE_STEP",
        "{\"cmd\":\"DRIVE_STEP\",\"dist_mm\":-10.499,\"speed_mm_s\":20,\"extra\":5}", 20.0f, p2, sizeof p2);
    ck("정규화: 0.01 반올림, 모르는 필드 무시", std::string(p1) == p2, p2);
    saver_v11::normalizeParams("HEAD_HOLD", "{\"cmd\":\"HEAD_HOLD\",\"press_mm\":10}", 0.0f, p3, sizeof p3);
    ck("정규화: drv_zero 기본 false", std::string(p3) == "press_mm=10.00;drv_zero=false;", p3);
    saver_v11::normalizeParams("DRIVE_STEP", "{\"cmd\":\"DRIVE_STEP\",\"dist_mm\":\"10\"}", 20.0f, p4, sizeof p4);
    ck("정규화: 형식이 틀리면 !", std::string(p4).find("dist_mm=!;") == 0, p4);

    std::cout << "\n[v1.1 판단 순서 §2.5]\n";
    using namespace saver_v11;
    Ledger L;
    L.reset(48213);
    Slot slot;
    Request q = {};
    q.seq = 1; q.cmd = "ARM"; q.params = "";
    ck("bid·aid 없음 → AID_REQUIRED", L.evaluate(q, &slot) == V_AID_REQUIRED);
    q.has_bid = true; q.bid = 48213; q.has_aid = true; q.aid = 0;
    ck("aid 0 → AID_REQUIRED", L.evaluate(q, &slot) == V_AID_REQUIRED);
    q.aid = 1; q.bid = 48214;
    ck("bid 다름 → BOOT_MISMATCH", L.evaluate(q, &slot) == V_BOOT_MISMATCH);
    q.bid = 48213;
    ck("새 aid → NEW", L.evaluate(q, &slot) == V_NEW);
    ck("기록 전 GET_INFO: last_seq·last_aid 없음", !L.hasLastSeq() && L.aidMax() == 0);
    Record& ra = L.recordNew(q, SLOT_CMD_LAST);
    buildAck(ra.ack, sizeof ra.ack, 1, false, NULL, "2-CU-011", "VACUUM_NOT_RELEASED");
    ck("기록 뒤 last_seq 1, last_aid 1", L.hasLastSeq() && L.lastSeq() == 1 && L.aidMax() == 1);
    ck("같은 aid·seq·파라미터 → 재전송(cmd_last)", L.evaluate(q, &slot) == V_REPLAY && slot == SLOT_CMD_LAST);
    Request q2 = q; q2.seq = 2;
    ck("같은 aid, 다른 seq → SEQ_CONFLICT", L.evaluate(q2, &slot) == V_CONFLICT);
    Request q3 = q; q3.cmd = "SET_TARGET";
    ck("같은 aid, 다른 명령 → SEQ_CONFLICT", L.evaluate(q3, &slot) == V_CONFLICT);
    ck("재전송·충돌·거부는 last_seq·last_aid 를 바꾸지 않음", L.lastSeq() == 1 && L.aidMax() == 1);

    /* 동작 aid 5 진행 중, aid 6 BUSY 거부 → aid 5 재전송은 여전히 원래 ACK(진행 기록은 교체되지 않음) */
    Request a5 = {}; a5.seq = 10; a5.cmd = "HOME"; a5.params = ""; a5.has_bid = true; a5.bid = 48213; a5.has_aid = true; a5.aid = 5;
    ck("동작 aid 5 → NEW", L.evaluate(a5, &slot) == V_NEW);
    Record& r5 = L.recordNew(a5, SLOT_ACT_CUR);
    buildAck(r5.ack, sizeof r5.ack, 10, true, "ARMED", NULL, NULL);
    Request a6 = a5; a6.seq = 11; a6.cmd = "HEAD_HOLD"; a6.params = "press_mm=10.00;drv_zero=false;"; a6.aid = 6;
    ck("동작 aid 6 → NEW", L.evaluate(a6, &slot) == V_NEW);
    Record& r6 = L.recordNew(a6, SLOT_ACT_REJ);
    buildAck(r6.ack, sizeof r6.ack, 11, false, NULL, "2-CU-011", "BUSY");
    ck("aid 5 재전송 → act_cur", L.evaluate(a5, &slot) == V_REPLAY && slot == SLOT_ACT_CUR &&
                                  !L.record(SLOT_ACT_CUR).done);
    ck("aid 6 재전송 → 원래 거부 ACK(act_rej)", L.evaluate(a6, &slot) == V_REPLAY && slot == SLOT_ACT_REJ &&
                                  strstr(L.record(SLOT_ACT_REJ).ack, "BUSY") != NULL);
    buildEvt(buf, sizeof buf, 99, "HOMING_DONE");
    ck("완료 저장(aid 일치)", L.storeDone(5, buf, "OK") && L.record(SLOT_ACT_CUR).done &&
                              !strcmp(L.record(SLOT_ACT_CUR).result, "OK"));
    ck("완료 저장(aid 다름) 거부", !L.storeDone(6, buf, "OK"));
    Request a7 = a6; a7.seq = 12; a7.aid = 7; a7.cmd = "DRIVE_STEP"; a7.params = "dist_mm=10.00;speed_mm_s=20.00;";
    L.recordNew(a7, SLOT_ACT_REJ);
    ck("기록 밖 옛 aid 6 → STALE_SEQ", L.evaluate(a6, &slot) == V_STALE);
    ck("기록 밖 옛 aid 2 → STALE_SEQ", [&]() { Request x = q; x.aid = 2; return L.evaluate(x, &slot) == V_STALE; }());
    ck("aid 5 는 여전히 재전송 가능(5번보다 3번을 먼저)", L.evaluate(a5, &slot) == V_REPLAY);
    ck("isRecordReason: 정지 예외 사유 4개",
       isRecordReason(V_AID_REQUIRED) && isRecordReason(V_BOOT_MISMATCH) && isRecordReason(V_STALE) &&
       isRecordReason(V_EXHAUSTED) && !isRecordReason(V_CONFLICT) && !isRecordReason(V_REPLAY));

    Request big = q; big.seq = 20; big.cmd = "SET_TEL_RATE"; big.params = "hz=20.00;"; big.aid = 4294967295u;
    ck("aid 4294967295 → NEW", L.evaluate(big, &slot) == V_NEW);
    L.recordNew(big, SLOT_CMD_LAST);
    Request after = q; after.seq = 21; after.aid = 8;
    ck("aid_max 소진 뒤 새 aid → AID_EXHAUSTED", L.evaluate(after, &slot) == V_EXHAUSTED);
    ck("소진 뒤에도 같은 aid 재전송은 원래 응답", L.evaluate(big, &slot) == V_REPLAY);
    L.reset(48214);
    ck("재부팅: 기록·aid_max 비움", L.aidMax() == 0 && !L.hasLastSeq() && L.evaluate(a5, &slot) == V_BOOT_MISMATCH);

    std::cout << "\n[v1.1 활성 오류 §3.1·§3.5]\n";
    FaultTable ft;
    ft.raise("1-PU-001", 1, false, "PPG_LOW");
    ft.raise("2-VU-007", 2, false, "VAC_DRIVER_ERR");
    ft.raise("3-CU-021", 3, true, "OVER_CURRENT");
    ft.raise("2-TU-001", 2, false, "YAW_LIMIT");
    ft.raise("3-TU-009", 3, true, "LOADCELL_STALE");
    const FaultEntry* fe[FaultTable::N];
    int nf = ft.sorted(fe, FaultTable::N);
    std::string order;
    for (int k = 0; k < nf; k++) order += std::string(fe[k]->code) + " ";
    ck("심각도 3→2→1, 같은 심각도는 먼저 생긴 순",
       order == "3-CU-021 3-TU-009 2-VU-007 2-TU-001 1-PU-001 ", order);
    ck("이미 있는 코드는 순서 유지", !ft.raise("2-VU-007", 2, false, "VAC_DRIVER_ERR") && ft.count() == 5);
    ft.causeCleared("2-VU-007");
    ck("비래치 원인 해소 → 목록에서 빠짐", !ft.has("2-VU-007") && ft.count() == 4);
    ft.causeCleared("3-CU-021");
    ck("래치는 원인 해소 뒤에도 남음", ft.has("3-CU-021") && !ft.causePresent("3-CU-021"));
    ck("원인이 남으면 RESET 실패", !ft.resetAll() && ft.count() == 4);
    ft.causeCleared("3-TU-009"); ft.causeCleared("2-TU-001"); ft.causeCleared("1-PU-001");
    ck("원인이 모두 없으면 RESET 성공", ft.resetAll() && ft.count() == 0 && ft.topCode() == NULL);

    static const char* const many[8] = { "3-TU-003", "3-TU-005", "3-TU-009", "3-TU-010", "3-CU-021",
                                         "3-CU-022", "3-CU-023", "3-VU-006" };
    Telemetry tf = {};
    tf.state = "FAULT"; tf.ms = 5; tf.sync = true; tf.tn = 9; tf.boot_id = 3;
    tf.faults_list = many; tf.faults_n = 8;
    buildTel(buf, sizeof buf, tf);
    ck("faults 8개 → 6개와 faults_more",
       strstr(buf, "\"faults\":[\"3-TU-003\",\"3-TU-005\",\"3-TU-009\",\"3-TU-010\",\"3-CU-021\",\"3-CU-022\"],\"faults_more\":true") != NULL, buf);
    ck("tel tn·boot_id", strstr(buf, "\"tn\":9,\"boot_id\":3") != NULL, buf);
    tf.faults_n = 2;
    buildTel(buf, sizeof buf, tf);
    ck("자르지 않으면 faults_more 생략", strstr(buf, "faults_more") == NULL, buf);

    std::cout << "\n[v1.1 mot·INFO·완료 이벤트]\n";
    MotStatus m0 = {};
    m0.ms = 1; m0.tn = 2; m0.boot_id = 3; m0.busy = "NONE";
    buildMot(buf, sizeof buf, m0);
    ck("mot 최소: rearm_lock·vac_released 는 항상", std::string(buf) ==
       "{\"v\":1,\"t\":\"mot\",\"ms\":1,\"tn\":2,\"boot_id\":3,\"busy\":\"NONE\",\"head_hold_active\":false,"
       "\"rearm_lock\":false,\"vac_released\":false}", buf);
    m0.drv_pos_mm = 12.0f;                      /* _ok 가 false 면 값이 있어도 생략 */
    buildMot(buf, sizeof buf, m0);
    ck("mot 측정 불가 필드 생략", strstr(buf, "drv_pos_mm") == NULL && strstr(buf, "drv_settled") == NULL, buf);

    static const char* const caps3[3] = { "MOT", "AID", "FAULTS" };
    InfoMsg i0 = {};
    i0.ms = 7; i0.seq = 3; i0.fw = "1.1.0"; i0.caps = caps3; i0.caps_n = 3; i0.boot_id = 48213; i0.drv_enc_mode = 2;
    buildInfo(buf, sizeof buf, i0);
    ck("INFO 기록 전: last_seq·last_aid 생략", std::string(buf) ==
       "{\"v\":1,\"t\":\"evt\",\"ms\":7,\"evt\":\"INFO\",\"seq\":3,\"fw\":\"1.1.0\",\"proto\":\"1.1\","
       "\"caps\":[\"MOT\",\"AID\",\"FAULTS\"],\"boot_id\":48213,\"drv_enc_mode\":2}", buf);
    i0.drv_enc_mode = 2; i0.drv_enc_side = 'L';
    buildInfo(buf, sizeof buf, i0);
    ck("INFO 모드 2 면 drv_enc_side 생략", strstr(buf, "drv_enc_side") == NULL, buf);

    MotionEvt ev = {};
    ev.ms = 184320; ev.evt = "DRIVE_DONE"; ev.seq = 1208; ev.aid = 37; ev.result = "OK";
    ev.drive_ok = true; ev.drive_mm = 9.96f; ev.drv_pos_ok = true; ev.drv_pos_mm = 40.1f;
    ev.drv_yaw_ok = true; ev.drv_yaw_deg = 0.6f; ev.force_ok = true; ev.cp_force_N = 28.4f;
    ev.force_n_ok = true; ev.force_n = 40;
    buildMotionEvt(buf, sizeof buf, ev);
    ck("DRIVE_DONE 초안 §4.2 예와 같음", std::string(buf) ==
       "{\"v\":1,\"t\":\"evt\",\"ms\":184320,\"evt\":\"DRIVE_DONE\",\"seq\":1208,\"aid\":37,\"result\":\"OK\","
       "\"drive_mm\":9.96,\"drv_pos_mm\":40.10,\"drv_yaw_deg\":0.60,\"cp_force_N\":28.40,\"force_n\":40}", buf);
    ev.result = "FAULT"; ev.code = "3-TU-005"; ev.drive_ok = ev.drv_pos_ok = ev.force_ok = false;
    buildMotionEvt(buf, sizeof buf, ev);
    ck("evt 의 seq·aid·code, 무효 값 생략", strstr(buf, "\"seq\":1208,\"aid\":37,\"result\":\"FAULT\",\"code\":\"3-TU-005\"") &&
       !strstr(buf, "drive_mm") && !strstr(buf, "cp_force_N"), buf);
    n = buildVacReleased(buf, sizeof buf, 201900, 1302, 88, true, -0.4f);
    ck("VACUUM_RELEASED 초안 §4.6 예와 같음", std::string(buf) ==
       "{\"v\":1,\"t\":\"evt\",\"ms\":201900,\"evt\":\"VACUUM_RELEASED\",\"seq\":1302,\"aid\":88,\"vac_kPa\":-0.40}", buf);
    buildFaultsEnd(buf, sizeof buf, 184400, 1210, 8);
    ck("FAULTS_END 초안 §3.5 예와 같음", std::string(buf) ==
       "{\"v\":1,\"t\":\"evt\",\"ms\":184400,\"evt\":\"FAULTS_END\",\"seq\":1210,\"n\":8}", buf);

    std::cout << "\n[v1.1 넘침: 잘린 JSON 을 만들지 않는다]\n";
    char small[40];
    ck("버퍼가 작으면 -1, 빈 문자열", buildMot(small, sizeof small, m0) == -1 && small[0] == '\0');
    ck("허용하지 않는 문자열 → -1", buildEvt(buf, sizeof buf, 1, "BAD\"NAME") == -1 && buf[0] == '\0');

    std::cout << "\n[v1.1 최악값 길이 §3.4 (본문 ≤ 511, LF 별도)]\n";
    std::vector<std::pair<std::string, std::string> > W = worstLines();
    for (size_t k = 0; k < W.size(); k++) {
        const std::string& line = W[k].second;
        bool okLine = !line.empty() && line.size() <= SAVER_MAX_BODY && saverJsonValid(line.c_str()) &&
                      line.find('\n') == std::string::npos && line.find('\r') == std::string::npos;
        ck(("최악값 " + W[k].first).c_str(), okLine, std::to_string(line.size()) + " byte");
    }

    std::cout << "\n[v1.1 최악값 고정 파일]\n";
    std::string want = fixtureText(), got, used;
    for (size_t k = 0; k < sizeof FIXTURE_PATHS / sizeof FIXTURE_PATHS[0] && used.empty(); k++) {
        std::ifstream in(FIXTURE_PATHS[k], std::ios::binary);
        if (!in) continue;
        std::stringstream ss; ss << in.rdbuf(); got = ss.str(); used = FIXTURE_PATHS[k];
    }
    ck("protocol/examples/v11_worst_case.jsonl 이 생성기와 같음", !used.empty() && got == want,
       used.empty() ? "파일 없음: ./test --write-fixture" : used);

    std::cout << "\n" << (fail == 0 ? "전체 통과" : "실패 있음")
              << "  (통과 " << pass << " / 실패 " << fail << ")\n\n";
    return fail == 0 ? 0 : 1;
}
