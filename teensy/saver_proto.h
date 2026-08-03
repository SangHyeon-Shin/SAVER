/* ============================================================================
 * saver_proto.h — SAVER USB 프로토콜 v1.0 파서/생성기
 *
 * 외부 라이브러리가 필요 없습니다 (ArduinoJson 불필요).
 * 프로토콜이 1단계 평면 JSON만 쓰기 때문에 이 정도로 충분합니다.
 * 순수 C++이라 PC에서 g++로 단위시험할 수 있습니다.
 *
 * 대상: Teensy 4.1 / 명세: protocol/teensy_usb_protocol_v1.md
 * ==========================================================================*/
#ifndef SAVER_PROTO_H
#define SAVER_PROTO_H

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>

#define SAVER_PROTO_VERSION 1
#define SAVER_MAX_LINE      512   /* 명세 §3: 512 byte 초과 시 폐기 */

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
 * 중복 seq 차단 — 명세 §4.1: 최근 8개 기억, 재실행 금지 / ACK는 재송신
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
 * 송신 문자열 생성 (Teensy에서는 Serial.println(buf) 로 내보냄)
 * -------------------------------------------------------------------------*/
static inline int buildAck(char* buf, size_t n, long seq, bool ok,
                           const char* state, const char* err, const char* why)
{
    if (ok) {
        return snprintf(buf, n,
            "{\"v\":%d,\"t\":\"ack\",\"seq\":%ld,\"ok\":true,\"state\":\"%s\"}",
            SAVER_PROTO_VERSION, seq, state ? state : "");
    }
    return snprintf(buf, n,
        "{\"v\":%d,\"t\":\"ack\",\"seq\":%ld,\"ok\":false,\"err\":\"%s\",\"why\":\"%s\"}",
        SAVER_PROTO_VERSION, seq, err ? err : "", why ? why : "");
}

static inline int buildErr(char* buf, size_t n, unsigned long ms,
                           const char* code, int sev, bool latched, const char* why)
{
    return snprintf(buf, n,
        "{\"v\":%d,\"t\":\"err\",\"ms\":%lu,\"code\":\"%s\",\"sev\":%d,"
        "\"latched\":%s,\"why\":\"%s\"}",
        SAVER_PROTO_VERSION, ms, code, sev, latched ? "true" : "false", why ? why : "");
}

static inline int buildEvt(char* buf, size_t n, unsigned long ms, const char* evt)
{
    return snprintf(buf, n, "{\"v\":%d,\"t\":\"evt\",\"ms\":%lu,\"evt\":\"%s\"}",
                    SAVER_PROTO_VERSION, ms, evt);
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
    const char* faults;              /* 예: "\"3-CU-021\"" 또는 NULL */
};

static inline int buildTel(char* buf, size_t n, const Telemetry& t)
{
    int p = snprintf(buf, n, "{\"v\":%d,\"t\":\"tel\",\"ms\":%lu,\"state\":\"%s\"",
                     SAVER_PROTO_VERSION, t.ms, t.state);
#define SAVER_F(cond, key, val) \
    if ((cond) && p > 0 && (size_t)p < n) p += snprintf(buf + p, n - p, ",\"" key "\":%.2f", (double)(val));
    SAVER_F(t.cp_rate_ok,    "cp_rate_cpm",  t.cp_rate_cpm)
    SAVER_F(t.cp_depth_ok,   "cp_depth_mm",  t.cp_depth_mm)
    SAVER_F(t.cp_force_ok,   "cp_force_N",   t.cp_force_N)
    SAVER_F(t.cp_motor_i_ok, "cp_motor_i_A", t.cp_motor_i_A)
    SAVER_F(t.vac_ok,        "vac_kPa",      t.vac_kPa)
    SAVER_F(t.ppg_hr_ok,     "ppg_hr_bpm",   t.ppg_hr_bpm)
    SAVER_F(t.spo2_ok,       "spo2_pct",     t.spo2_pct)
    SAVER_F(t.ppg_sqi_ok,    "ppg_sqi",      t.ppg_sqi)
    SAVER_F(t.temp_ok,       "temp_C",       t.temp_C)
    SAVER_F(t.fan_ok,        "fan_pct",      t.fan_pct)
#undef SAVER_F
    if (p > 0 && (size_t)p < n)
        p += snprintf(buf + p, n - p,
            ",\"estop\":%s,\"limit_top\":%s,\"limit_bottom\":%s,\"vac_leak\":%s,\"faults\":[%s]}",
            t.estop ? "true" : "false", t.limit_top ? "true" : "false",
            t.limit_bottom ? "true" : "false", t.vac_leak ? "true" : "false",
            t.faults ? t.faults : "");
    return p;
}

#endif /* SAVER_PROTO_H */
