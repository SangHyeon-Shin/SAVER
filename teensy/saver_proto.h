/* ============================================================================
 * saver_proto.h — SAVER USB 프로토콜 v1.1 파서/생성기
 *
 * 외부 라이브러리가 필요 없습니다 (ArduinoJson 불필요).
 * 프로토콜이 1단계 평면 JSON만 쓰기 때문에 이 정도로 충분합니다.
 * 순수 C++(C++11)이라 PC에서 g++로 단위시험할 수 있습니다.
 *
 * 대상: Teensy 4.1 / 명세: protocol/teensy_usb_protocol_v1.md
 *
 * v1.1 추가 (2026-10-23, 신상현(Claude Code) 작성, 한건민 검토 필요)
 *   기준: docs/protocol_v1.1_draft.md 8차 합의본(10/4). 와이어 "v" 는 1 그대로다.
 *   - saverJsonValid  : 한 줄이 평면 JSON 객체인지 엄격히 검사(§2.5-0 정지 명령 구조 검사)
 *   - jsonU32         : 0~4294967295 정수 읽기(aid·seq·bid). jsonInt 는 float 를 거쳐 2^24 위에서 틀린다
 *   - saverF2         : 실수 형식(§3.4) 소수 둘째 자리, -9999.99~9999.99, -0.00 → 0.00, 범위 밖·NaN 은 생략
 *   - JsonLine        : 본문 511 byte 를 넘으면 실패(-1)하는 줄 작성기. 잘린 JSON 을 만들지 않는다
 *   - buildTel(tn·boot_id·faults_more), buildMot, buildInfo, buildMotionEvt(완료 이벤트),
 *     buildVacReleased, buildFaultsEnd
 *   반환값: 생성기는 본문 길이(LF 제외)를, 넘침·형식 위반이면 -1 을 돌려준다. -1 이면 보내지 않는다.
 * ==========================================================================*/
#ifndef SAVER_PROTO_H
#define SAVER_PROTO_H

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>

#define SAVER_PROTO_VERSION 1                     /* 와이어 "v". v1.1 도 1 (초안 머리표) */
#define SAVER_PROTO_REV     "1.1"                 /* GET_INFO 의 "proto" (§2.4) */
#define SAVER_MAX_LINE      512                   /* 명세 §3: LF 포함 512 byte */
#define SAVER_MAX_BODY      (SAVER_MAX_LINE - 1)  /* v1.1 §3.4: 본문 511 byte, 그 뒤 LF 하나 */
#define SAVER_TEL_FAULTS_MAX 6                    /* v1.1 §3.1: tel 의 faults 는 6개까지 */

/* ---------------------------------------------------------------------------
 * 내부: "key" 가 값 위치가 아닌 키 위치에 있는지 확인하며 탐색
 * (문자열 값 안에 우연히 같은 글자가 있어도 오인하지 않도록)
 * -------------------------------------------------------------------------*/
static inline const char* _saver_findKey(const char* s, const char* key)
{
    size_t klen = strlen(key);
    for (const char* p = s; (p = strchr(p, '"')) != NULL; p++) {
        /* 이 따옴표가 키의 시작인지: 앞쪽 공백을 건너뛰면 '{' 또는 ',' 여야 함 */
        const char* b = p - 1;
        while (b >= s && (*b == ' ' || *b == '\t')) b--;
        if (b < s || (*b != '{' && *b != ',')) continue;

        if (strncmp(p + 1, key, klen) != 0 || p[1 + klen] != '"') continue;

        const char* q = p + 2 + klen;
        while (*q == ' ' || *q == '\t') q++;
        if (*q != ':') continue;
        q++;
        while (*q == ' ' || *q == '\t') q++;
        return q;                    /* 값의 첫 글자 */
    }
    return NULL;
}

/* 문자열 값 읽기. 이스케이프는 \" \\ \n 만 처리 (명세상 그 외는 쓰지 않음) */
static inline bool jsonStr(const char* line, const char* key, char* out, size_t outLen)
{
    const char* v = _saver_findKey(line, key);
    if (!v || *v != '"' || outLen == 0) return false;
    v++;
    size_t i = 0;
    while (*v && *v != '"') {
        if (*v == '\\' && v[1]) {
            v++;
            char c = (*v == 'n') ? '\n' : *v;
            if (i + 1 < outLen) out[i++] = c;
        } else {
            if (i + 1 < outLen) out[i++] = *v;
        }
        v++;
    }
    if (*v != '"') return false;     /* 닫는 따옴표 없음 = 잘린 패킷 */
    out[i] = '\0';
    return true;
}

static inline bool jsonNum(const char* line, const char* key, float* out)
{
    const char* v = _saver_findKey(line, key);
    if (!v) return false;
    if (*v != '-' && *v != '+' && *v != '.' && (*v < '0' || *v > '9')) return false;
    char* end = NULL;
    float f = (float)strtod(v, &end);
    if (end == v) return false;
    *out = f;
    return true;
}

static inline bool jsonInt(const char* line, const char* key, long* out)
{
    float f;
    if (!jsonNum(line, key, &f)) return false;
    *out = (long)f;
    return true;
}

/* v1.1: 부호 없는 32비트 정수(0~4294967295). 앞자리 0, 부호, 소수점, 지수, 넘침은 실패.
 * aid 는 4,294,967,295 까지 가므로 float 를 거치는 jsonInt 로 읽으면 안 된다. */
static inline bool jsonU32(const char* line, const char* key, uint32_t* out)
{
    const char* v = _saver_findKey(line, key);
    if (!v || *v < '0' || *v > '9') return false;
    if (v[0] == '0' && v[1] >= '0' && v[1] <= '9') return false;
    uint32_t n = 0;
    for (; *v >= '0' && *v <= '9'; v++) {
        uint32_t d = (uint32_t)(*v - '0');
        if (n > (UINT32_MAX - d) / 10u) return false;
        n = n * 10u + d;
    }
    if (*v == '.' || *v == 'e' || *v == 'E') return false;
    *out = n;
    return true;
}

static inline bool jsonBool(const char* line, const char* key, bool* out)
{
    const char* v = _saver_findKey(line, key);
    if (!v) return false;
    if (strncmp(v, "true", 4) == 0)  { *out = true;  return true; }
    if (strncmp(v, "false", 5) == 0) { *out = false; return true; }
    return false;
}

/* 필드 존재 여부만 확인 (명세 §6: 미측정 필드는 생략) */
static inline bool jsonHas(const char* line, const char* key)
{
    return _saver_findKey(line, key) != NULL;
}

/* ---------------------------------------------------------------------------
 * v1.1 §2.5-0 구조 검사: 한 줄이 평면 JSON 객체인가
 *   값: 문자열, 수, true/false/null, 스칼라만 담은 배열. 중첩 객체는 실패.
 *   문자열은 ASCII 인쇄 문자와 표준 이스케이프만. 본문 511 byte 이하.
 * 깨진 줄에서 "STOP_CPR" 같은 글자만 찾아 정지로 판단하지 않으려고 쓴다(v1.0 §3: 그 줄은 버린다).
 * -------------------------------------------------------------------------*/
static inline const char* _saver_ws(const char* p)
{
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    return p;
}

static inline bool _saver_hex(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static inline const char* _saver_skipStr(const char* p)
{
    if (*p != '"') return NULL;
    p++;
    while (*p != '"') {
        unsigned char c = (unsigned char)*p;
        if (c < 0x20 || c > 0x7E) return NULL;          /* 끝(NUL), 제어문자, 비 ASCII */
        if (c == '\\') {
            p++;
            if (*p == 'u') {
                for (int i = 1; i <= 4; i++) if (!_saver_hex(p[i])) return NULL;
                p += 5;
                continue;
            }
            if (!*p || !strchr("\"\\/bfnrt", *p)) return NULL;
        }
        p++;
    }
    return p + 1;
}

static inline const char* _saver_skipNum(const char* p)
{
    if (*p == '-') p++;
    if (*p == '0') p++;
    else if (*p >= '1' && *p <= '9') { while (*p >= '0' && *p <= '9') p++; }
    else return NULL;
    if (*p == '.') {
        p++;
        if (*p < '0' || *p > '9') return NULL;
        while (*p >= '0' && *p <= '9') p++;
    }
    if (*p == 'e' || *p == 'E') {
        p++;
        if (*p == '+' || *p == '-') p++;
        if (*p < '0' || *p > '9') return NULL;
        while (*p >= '0' && *p <= '9') p++;
    }
    return p;
}

static inline const char* _saver_skipScalar(const char* p)
{
    if (*p == '"') return _saver_skipStr(p);
    if (strncmp(p, "true", 4) == 0)  return p + 4;
    if (strncmp(p, "false", 5) == 0) return p + 5;
    if (strncmp(p, "null", 4) == 0)  return p + 4;
    return _saver_skipNum(p);
}

static inline bool saverJsonValid(const char* s)
{
    if (!s) return false;
    size_t len = strlen(s);
    if (len == 0 || len > SAVER_MAX_BODY) return false;
    const char* p = _saver_ws(s);
    if (*p != '{') return false;
    p = _saver_ws(p + 1);
    if (*p == '}') return *_saver_ws(p + 1) == '\0';
    for (;;) {
        p = _saver_skipStr(p);
        if (!p) return false;
        p = _saver_ws(p);
        if (*p != ':') return false;
        p = _saver_ws(p + 1);
        if (*p == '[') {
            p = _saver_ws(p + 1);
            if (*p != ']') {
                for (;;) {
                    p = _saver_skipScalar(p);
                    if (!p) return false;
                    p = _saver_ws(p);
                    if (*p == ',') { p = _saver_ws(p + 1); continue; }
                    if (*p == ']') break;
                    return false;
                }
            }
            p++;
        } else {
            p = _saver_skipScalar(p);
            if (!p) return false;
        }
        p = _saver_ws(p);
        if (*p == ',') { p = _saver_ws(p + 1); continue; }
        if (*p == '}') return *_saver_ws(p + 1) == '\0';
        return false;
    }
}

/* ---------------------------------------------------------------------------
 * 중복 seq 차단 — 명세 v1.0 §4.1: 최근 8개 기억, 재실행 금지 / ACK는 재송신
 * v1.1 펌웨어는 쓰지 않는다(§2.5: 조회는 매번 실행, 나머지는 aid 로 가린다). v1.0 호환용으로 둔다.
 * -------------------------------------------------------------------------*/
class SeqDedupe {
public:
    SeqDedupe() : idx_(0) { for (int i = 0; i < N; i++) seen_[i] = 0; }

    bool isDuplicate(long seq) const {
        for (int i = 0; i < N; i++) if (seen_[i] == seq) return true;
        return false;
    }
    void remember(long seq) { seen_[idx_] = seq; idx_ = (idx_ + 1) % N; }

private:
    static const int N = 8;
    long seen_[N];
    int  idx_;
};

/* ---------------------------------------------------------------------------
 * v1.1 §3.4 실수 형식: 소수 둘째 자리 고정, -9999.99 ~ 9999.99, -0.00 은 0.00.
 * 범위 밖·NaN·Infinity 면 false 를 돌려주고 호출부는 그 필드를 생략한다(측정 불가).
 * printf 의 %f 대신 정수로 만들어 플랫폼마다 결과가 같다.
 * -------------------------------------------------------------------------*/
static inline bool saverF2(char* out, size_t n, double v)
{
    if (!(v >= -10000.0 && v <= 10000.0)) return false;        /* NaN, Inf 도 여기서 걸린다 */
    double r = v * 100.0;
    long c = (long)(r < 0 ? r - 0.5 : r + 0.5);                 /* 0.01 단위 반올림 */
    if (c > 999999L || c < -999999L) return false;
    unsigned long a = (unsigned long)(c < 0 ? -c : c);
    int w = snprintf(out, n, "%s%lu.%02lu", c < 0 ? "-" : "", a / 100ul, a % 100ul);
    return w > 0 && (size_t)w < n;
}

/* 문자열 값으로 쓸 수 있는지: ASCII 인쇄 문자, 따옴표·역슬래시 없음(§3.4) */
static inline bool saverSafeStr(const char* s)
{
    if (!s) return false;
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c < 0x20 || c > 0x7E || c == '"' || c == '\\') return false;
    }
    return true;
}

/* ---------------------------------------------------------------------------
 * v1.1 줄 작성기. 본문이 SAVER_MAX_BODY(511) 또는 버퍼를 넘으면 실패 상태가 되고
 * end() 가 -1 을 돌려준다. 실패한 줄은 버퍼를 비워 두어 실수로 보내도 빈 줄이 된다.
 * -------------------------------------------------------------------------*/
class JsonLine {
public:
    JsonLine(char* buf, size_t cap)
        : b_(buf), cap_(cap), n_(0), ok_(buf != NULL && cap > 0), first_(true)
    { if (ok_) b_[0] = '\0'; }

    void begin(const char* type) {
        raw("{\"v\":");
        char t[12];
        snprintf(t, sizeof t, "%d", SAVER_PROTO_VERSION);
        raw(t);
        first_ = false;
        str("t", type);
    }
    void str(const char* k, const char* v) {
        if (!saverSafeStr(v)) { ok_ = false; return; }
        key(k); raw("\""); raw(v); raw("\"");
    }
    void u32(const char* k, unsigned long v) {        /* 0~4294967295 */
        if ((unsigned long long)v > 4294967295ull) { ok_ = false; return; }
        char t[12];
        snprintf(t, sizeof t, "%lu", v);
        key(k); raw(t);
    }
    void i32(const char* k, long v) {
        char t[12];
        snprintf(t, sizeof t, "%ld", v);
        key(k); raw(t);
    }
    void boolean(const char* k, bool v) { key(k); raw(v ? "true" : "false"); }
    /* 실수: 형식에 맞지 않으면 필드를 생략하고 false */
    bool f2(const char* k, double v) {
        char t[16];
        if (!saverF2(t, sizeof t, v)) return false;
        key(k); raw(t);
        return true;
    }
    void f2If(bool have, const char* k, double v) { if (have) f2(k, v); }
    void strList(const char* k, const char* const* items, int count) {
        key(k); raw("[");
        for (int i = 0; i < count; i++) {
            if (!saverSafeStr(items[i])) { ok_ = false; return; }
            if (i) raw(",");
            raw("\""); raw(items[i]); raw("\"");
        }
        raw("]");
    }
    /* 미리 만든 목록을 그대로 넣는다(v1.0 호환 Telemetry::faults). 따옴표·쉼표·영숫자·'-' 만 허용 */
    void rawList(const char* k, const char* list) {
        if (!list) list = "";
        for (const char* p = list; *p; p++) {
            char c = *p;
            bool okc = c == '"' || c == ',' || c == '-' || (c >= '0' && c <= '9') ||
                       (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
            if (!okc) { ok_ = false; return; }
        }
        key(k); raw("["); raw(list); raw("]");
    }
    int end() {
        raw("}");
        if (!ok_) { if (b_ && cap_) b_[0] = '\0'; return -1; }
        return (int)n_;
    }
    bool ok() const { return ok_; }

private:
    void raw(const char* s) {
        if (!ok_) return;
        size_t l = strlen(s);
        if (n_ + l > SAVER_MAX_BODY || n_ + l + 1 > cap_) { ok_ = false; return; }
        memcpy(b_ + n_, s, l);
        n_ += l;
        b_[n_] = '\0';
    }
    void key(const char* k) {
        raw(first_ ? "\"" : ",\"");
        first_ = false;
        raw(k);
        raw("\":");
    }
    char*  b_;
    size_t cap_;
    size_t n_;
    bool   ok_;
    bool   first_;
};

/* ---------------------------------------------------------------------------
 * 송신 문자열 생성. 반환: 본문 길이(LF 제외), 실패면 -1.
 * Teensy 에서는 본문 뒤에 LF 하나만 붙여 Serial.write 로 보낸다(v1.1 §3.4, CR 없음).
 * -------------------------------------------------------------------------*/
static inline int buildAck(char* buf, size_t n, long seq, bool ok,
                           const char* state, const char* err, const char* why)
{
    JsonLine j(buf, n);
    j.begin("ack");
    j.i32("seq", seq);
    j.boolean("ok", ok);
    if (ok) {
        j.str("state", state ? state : "");
    } else {
        j.str("err", err ? err : "");
        j.str("why", why ? why : "");
    }
    return j.end();
}

static inline int buildErr(char* buf, size_t n, unsigned long ms,
                           const char* code, int sev, bool latched, const char* why)
{
    JsonLine j(buf, n);
    j.begin("err");
    j.u32("ms", ms);
    j.str("code", code ? code : "");
    j.i32("sev", sev);
    j.boolean("latched", latched);
    j.str("why", why ? why : "");
    return j.end();
}

static inline int buildEvt(char* buf, size_t n, unsigned long ms, const char* evt)
{
    JsonLine j(buf, n);
    j.begin("evt");
    j.u32("ms", ms);
    j.str("evt", evt);
    return j.end();
}

/* 텔레메트리. 측정 불가 필드는 호출부에서 valid=false 로 넘기면 생략됩니다
 * (명세 §6: 0 을 보내지 말 것) */
struct Telemetry {
    const char* state;
    unsigned long ms;
    float cp_rate_cpm;  bool cp_rate_ok;
    float cp_depth_mm;  bool cp_depth_ok;
    float cp_force_N;   bool cp_force_ok;
    float cp_motor_i_A; bool cp_motor_i_ok;
    float vac_kPa;      bool vac_ok;
    float ppg_hr_bpm;   bool ppg_hr_ok;
    float spo2_pct;     bool spo2_ok;
    float ppg_sqi;      bool ppg_sqi_ok;
    float temp_C;       bool temp_ok;
    float fan_pct;      bool fan_ok;
    bool  estop, limit_top, limit_bottom, vac_leak;
    const char* faults;              /* v1.0 호환: 예 "\"3-CU-021\"" 또는 NULL. faults_list 가 있으면 쓰지 않음 */
    /* v1.1 §3.1 */
    bool  sync;                      /* true 면 tn, boot_id 를 싣는다 */
    unsigned long tn;                /* 주기 번호. 같은 주기의 mot 와 같다 */
    unsigned boot_id;
    const char* const* faults_list;  /* 정렬한 활성 오류(심각도 3→2→1, 먼저 생긴 순) */
    int   faults_n;                  /* faults_list 개수. 6개를 넘으면 앞 6개만 싣는다 */
    bool  faults_more;               /* 잘랐을 때 true (faults_n > 6 이면 자동으로 true) */
};

static inline int buildTel(char* buf, size_t n, const Telemetry& t)
{
    JsonLine j(buf, n);
    j.begin("tel");
    j.u32("ms", t.ms);
    if (t.sync) {
        j.u32("tn", t.tn);
        j.u32("boot_id", t.boot_id);
    }
    j.str("state", t.state ? t.state : "");
    j.f2If(t.cp_rate_ok,    "cp_rate_cpm",  t.cp_rate_cpm);
    j.f2If(t.cp_depth_ok,   "cp_depth_mm",  t.cp_depth_mm);
    j.f2If(t.cp_force_ok,   "cp_force_N",   t.cp_force_N);
    j.f2If(t.cp_motor_i_ok, "cp_motor_i_A", t.cp_motor_i_A);
    j.f2If(t.vac_ok,        "vac_kPa",      t.vac_kPa);
    j.f2If(t.ppg_hr_ok,     "ppg_hr_bpm",   t.ppg_hr_bpm);
    j.f2If(t.spo2_ok,       "spo2_pct",     t.spo2_pct);
    j.f2If(t.ppg_sqi_ok,    "ppg_sqi",      t.ppg_sqi);
    j.f2If(t.temp_ok,       "temp_C",       t.temp_C);
    j.f2If(t.fan_ok,        "fan_pct",      t.fan_pct);
    j.boolean("estop", t.estop);
    j.boolean("limit_top", t.limit_top);
    j.boolean("limit_bottom", t.limit_bottom);
    j.boolean("vac_leak", t.vac_leak);
    bool more = t.faults_more;
    if (t.faults_list) {
        int k = t.faults_n;
        if (k > SAVER_TEL_FAULTS_MAX) { k = SAVER_TEL_FAULTS_MAX; more = true; }
        j.strList("faults", t.faults_list, k < 0 ? 0 : k);
    } else {
        j.rawList("faults", t.faults);
    }
    if (more) j.boolean("faults_more", true);
    return j.end();
}

/* v1.1 §3.2 mot: 매 주기 tel 바로 뒤, 같은 ms·tn. 측정할 수 없는 값은 *_ok=false 로 생략 */
struct MotStatus {
    unsigned long ms, tn;
    unsigned boot_id;
    const char* busy;                       /* NONE DRIVE HOMING HEAD_SEARCH CP_ZERO */
    bool busy_ids; unsigned busy_seq; unsigned long busy_aid;
    bool head_hold_active;
    bool drv_settled_ok;  bool drv_settled;         /* 엔코더가 무효면 생략 */
    bool drv_pos_ok;      float drv_pos_mm;         /* 주행 원점이 없거나 엔코더 무효면 생략 */
    bool drv_yaw_ok;      float drv_yaw_deg;
    bool drv_pitch_ok;    float drv_pitch_deg;
    bool drv_roll_ok;     float drv_roll_deg;
    bool head_pos_ok;     float head_pos_mm;        /* HOME 전이거나 리니어 센서 무효면 생략 */
    bool head_contact_ok; float head_contact_mm;
    bool last_ok; unsigned last_seq; unsigned long last_aid;
    const char* last_cmd; const char* last_result;  /* act_cur 가 끝났을 때만 */
    bool rearm_lock, vac_released;                  /* 항상 싣는다 */
};

static inline int buildMot(char* buf, size_t n, const MotStatus& m)
{
    JsonLine j(buf, n);
    j.begin("mot");
    j.u32("ms", m.ms);
    j.u32("tn", m.tn);
    j.u32("boot_id", m.boot_id);
    j.str("busy", m.busy ? m.busy : "NONE");
    if (m.busy_ids) {
        j.u32("busy_seq", m.busy_seq);
        j.u32("busy_aid", m.busy_aid);
    }
    j.boolean("head_hold_active", m.head_hold_active);
    if (m.drv_settled_ok) j.boolean("drv_settled", m.drv_settled);
    j.f2If(m.drv_pos_ok,      "drv_pos_mm",      m.drv_pos_mm);
    j.f2If(m.drv_yaw_ok,      "drv_yaw_deg",     m.drv_yaw_deg);
    j.f2If(m.drv_pitch_ok,    "drv_pitch_deg",   m.drv_pitch_deg);
    j.f2If(m.drv_roll_ok,     "drv_roll_deg",    m.drv_roll_deg);
    j.f2If(m.head_pos_ok,     "head_pos_mm",     m.head_pos_mm);
    j.f2If(m.head_contact_ok, "head_contact_mm", m.head_contact_mm);
    if (m.last_ok) {
        j.u32("mot_last_seq", m.last_seq);
        j.u32("mot_last_aid", m.last_aid);
        j.str("mot_last_cmd", m.last_cmd ? m.last_cmd : "");
        j.str("mot_last_result", m.last_result ? m.last_result : "");
    }
    j.boolean("rearm_lock", m.rearm_lock);
    j.boolean("vac_released", m.vac_released);
    return j.end();
}

/* v1.1 §2.4 GET_INFO 응답 이벤트 */
struct InfoMsg {
    unsigned long ms;
    unsigned seq;                    /* GET_INFO 의 seq */
    const char* fw;                  /* 16자 이하 */
    const char* const* caps; int caps_n;
    unsigned boot_id;
    bool has_last_seq; unsigned last_seq;
    bool has_last_aid; unsigned long last_aid;
    int  drv_enc_mode;               /* 1 또는 2 */
    char drv_enc_side;               /* 모드 1: 'L' 또는 'R', 아니면 0(생략) */
};

static inline int buildInfo(char* buf, size_t n, const InfoMsg& m)
{
    JsonLine j(buf, n);
    j.begin("evt");
    j.u32("ms", m.ms);
    j.str("evt", "INFO");
    j.u32("seq", m.seq);
    j.str("fw", m.fw ? m.fw : "");
    j.str("proto", SAVER_PROTO_REV);
    j.strList("caps", m.caps, m.caps_n);
    j.u32("boot_id", m.boot_id);
    if (m.has_last_seq) j.u32("last_seq", m.last_seq);
    if (m.has_last_aid) j.u32("last_aid", m.last_aid);
    j.i32("drv_enc_mode", m.drv_enc_mode);
    if (m.drv_enc_mode == 1 && (m.drv_enc_side == 'L' || m.drv_enc_side == 'R')) {
        char side[2] = { m.drv_enc_side, '\0' };
        j.str("drv_enc_side", side);
    }
    return j.end();
}

/* v1.1 §4 완료 이벤트(HOMING_DONE, HEAD_HELD, CP_ZERO_SET, DRIVE_DONE, TRIAL_RESULT, SCAN_RESULT).
 * 원래 명령의 seq·aid 와 result 를 싣는다. 측정할 수 없는 값은 *_ok=false 로 생략(0 으로 대신 보내지 않음). */
struct MotionEvt {
    unsigned long ms;
    const char* evt;
    unsigned seq; unsigned long aid;
    const char* result;              /* OK NO_CONTACT STOPPED ABORTED FAULT SETTLE_FAIL SHORT */
    const char* code;                /* ABORTED·FAULT·SHORT·NO_CONTACT 일 때 오류 코드, 아니면 NULL */
    bool drive_ok;        float drive_mm;           /* DRIVE_DONE: 엔코더가 유효할 때만 */
    bool drv_pos_ok;      float drv_pos_mm;
    bool drv_yaw_ok;      float drv_yaw_deg;
    bool head_contact_ok; float head_contact_mm;    /* HEAD_HELD, HOMING_DONE, CP_ZERO_SET */
    bool head_pos_ok;     float head_pos_mm;
    bool force_ok;        float cp_force_N;         /* 반력 평균이 유효할 때만 */
    bool force_n_ok;      unsigned force_n;         /* 평균에 쓴 신규 변환 샘플 수 */
    int  drv_enc_mode;                              /* 1 일 때만 싣는다(§4.7 기록용) */
};

static inline int buildMotionEvt(char* buf, size_t n, const MotionEvt& e)
{
    JsonLine j(buf, n);
    j.begin("evt");
    j.u32("ms", e.ms);
    j.str("evt", e.evt ? e.evt : "");
    j.u32("seq", e.seq);
    j.u32("aid", e.aid);
    j.str("result", e.result ? e.result : "");
    if (e.code) j.str("code", e.code);
    j.f2If(e.drive_ok,        "drive_mm",        e.drive_mm);
    j.f2If(e.drv_pos_ok,      "drv_pos_mm",      e.drv_pos_mm);
    j.f2If(e.drv_yaw_ok,      "drv_yaw_deg",     e.drv_yaw_deg);
    j.f2If(e.head_contact_ok, "head_contact_mm", e.head_contact_mm);
    j.f2If(e.head_pos_ok,     "head_pos_mm",     e.head_pos_mm);
    j.f2If(e.force_ok,        "cp_force_N",      e.cp_force_N);
    if (e.force_n_ok) j.u32("force_n", e.force_n);
    if (e.drv_enc_mode == 1) j.i32("drv_enc_mode", 1);
    return j.end();
}

/* v1.1 §4.6 VACUUM_RELEASED: 대기 중이던 VACUUM_OFF 요청의 seq·aid */
static inline int buildVacReleased(char* buf, size_t n, unsigned long ms, unsigned seq,
                                   unsigned long aid, bool vac_ok, float vac_kPa)
{
    JsonLine j(buf, n);
    j.begin("evt");
    j.u32("ms", ms);
    j.str("evt", "VACUUM_RELEASED");
    j.u32("seq", seq);
    j.u32("aid", aid);
    j.f2If(vac_ok, "vac_kPa", vac_kPa);
    return j.end();
}

/* v1.1 §3.5 GET_FAULTS 끝 표시. n = 보낸 err 줄 수 */
static inline int buildFaultsEnd(char* buf, size_t n, unsigned long ms, unsigned seq, unsigned count)
{
    JsonLine j(buf, n);
    j.begin("evt");
    j.u32("ms", ms);
    j.str("evt", "FAULTS_END");
    j.u32("seq", seq);
    j.u32("n", count);
    return j.end();
}

#endif /* SAVER_PROTO_H */
