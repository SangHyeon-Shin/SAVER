/* saver_proto.h 단위시험 — PC에서 실행
 *   g++ -std=c++11 -Wall -Wextra -o test test_saver_proto.cpp && ./test          */
#include "saver_proto.h"
#include <string>
#include <cmath>
#include <iostream>

static int pass = 0, fail = 0;
static void ck(const char* name, bool ok, const std::string& detail = "")
{
    std::cout << (ok ? "  OK   " : "  FAIL ") << name;
    if (!detail.empty()) std::cout << "  <" << detail << ">";
    std::cout << "\n";
    ok ? pass++ : fail++;
}

int main()
{
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

    std::cout << "\n[중복 seq 차단]\n";
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

    std::cout << "\n" << (fail == 0 ? "전체 통과" : "실패 있음")
              << "  (통과 " << pass << " / 실패 " << fail << ")\n\n";
    return fail == 0 ? 0 : 1;
}
