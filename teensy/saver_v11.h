/* ============================================================================
 * saver_v11.h — SAVER USB 프로토콜 v1.1 동작 기록과 중복 판단, 활성 오류 목록
 *
 * 2026-10-23 신상현(Claude Code) 작성, 한건민 검토 필요.
 * 기준: docs/protocol_v1.1_draft.md 8차 합의본(10/4) §2.5(기록과 판단 순서), §2.4(last_seq·last_aid),
 *       §3.1·§3.5(faults 순서, GET_FAULTS). Arduino 의존이 없어 PC 에서 test_saver_proto.cpp 로 시험한다.
 *
 * 쓰는 법(saver_teensy.ino)
 *   1. classify(cmd) 로 조회·동작·즉시 변경·정지를 가른다. 조회는 기록하지 않는다.
 *   2. 조회가 아니면 normalizeParams() 로 파라미터를 정규화하고 Ledger::evaluate() 로 판단한다.
 *   3. V_NEW 면 실행·거부를 정한 뒤 recordNew() 로 기록하고, 만든 ACK 를 그 기록의 ack 에 넣는다.
 *   4. 동작이 끝나면 storeDone() 으로 완료 이벤트를 act_cur 에 먼저 저장하고 그 뒤 보낸다.
 * ==========================================================================*/
#ifndef SAVER_V11_H
#define SAVER_V11_H

#include "saver_proto.h"

namespace saver_v11 {

/* ── 명령 분류 (§2.5 표) ─────────────────────────────────────────── */
enum CmdClass { CMD_UNKNOWN = 0, CMD_QUERY, CMD_ACTION, CMD_IMMEDIATE, CMD_STOP };

inline bool _inList(const char* c, const char* const* list, int n)
{
    for (int i = 0; i < n; i++) if (strcmp(c, list[i]) == 0) return true;
    return false;
}

inline CmdClass classify(const char* c)
{
    static const char* const Q[] = { "PING", "GET_STATUS", "GET_INFO", "GET_FAULTS" };
    static const char* const A[] = { "HOME", "HEAD_HOLD", "DRIVE_STEP", "SET_CP_POINT",
                                     "TRIAL_COMPRESSION", "STIFFNESS_SCAN" };
    static const char* const I[] = { "ARM", "SET_TARGET", "VACUUM_ON", "VACUUM_OFF", "START_CPR",
                                     "RESUME_CPR", "RESET_FAULT", "SET_TEL_RATE" };
    static const char* const S[] = { "DRIVE_STOP", "STOP_CPR", "PAUSE_CPR", "DISARM" };
    if (!c) return CMD_UNKNOWN;
    if (_inList(c, Q, 4)) return CMD_QUERY;
    if (_inList(c, S, 4)) return CMD_STOP;
    if (_inList(c, A, 6)) return CMD_ACTION;
    if (_inList(c, I, 8)) return CMD_IMMEDIATE;
    return CMD_UNKNOWN;
}

/* 정지 방향 명령 4개와 정확히 같은 이름인가(§2.5-0 ④). 대소문자·앞뒤 글자가 다르면 아니다 */
inline bool isStopCmd(const char* c) { return classify(c) == CMD_STOP; }

/* rearm_lock 동안 REARM_LOCKED 로 거부하는 재가동 명령(§2.2 DISARM, §0.5 확인 요청 1).
 * 허용: HOME(헤드 복귀만), VACUUM_OFF, SET_TEL_RATE, 정지 방향 명령, 조회 */
inline bool rearmBlocked(const char* c)
{
    static const char* const R[] = { "ARM", "VACUUM_ON", "SET_TARGET", "SET_CP_POINT",
                                     "TRIAL_COMPRESSION", "STIFFNESS_SCAN", "START_CPR",
                                     "RESUME_CPR", "HEAD_HOLD", "DRIVE_STEP" };
    return c && _inList(c, R, 10);
}

/* ── 파라미터 정규화 (§2.5) ───────────────────────────────────────
 * 명령마다 아는 파라미터만 정해진 순서로 "키=값;" 으로 잇는다. 모르는 필드는 무시한다(명세 §3).
 * 수는 소수 둘째 자리(saverF2), 불리언은 true/false, 문자열은 그대로. 생략은 "-", 형식이 틀리면 "!".
 * 생략한 선택 파라미터는 기본값으로 채운다: DRIVE_STEP speed_mm_s = defSpeed, HEAD_HOLD drv_zero = false. */
enum ParamKind { P_NUM, P_BOOL, P_STR };
struct ParamSpec { const char* cmd; const char* key; ParamKind kind; int dflt; };  /* dflt: 0 없음, 1 defSpeed, 2 false */

inline void _append(char* out, size_t n, const char* s)
{
    size_t l = strlen(out);
    if (l + 1 >= n) return;
    strncat(out, s, n - l - 1);
}

inline void normalizeParams(const char* cmd, const char* line, float defSpeed, char* out, size_t n)
{
    static const ParamSpec SPEC[] = {
        { "DRIVE_STEP",        "dist_mm",     P_NUM,  0 },
        { "DRIVE_STEP",        "speed_mm_s",  P_NUM,  1 },
        { "HEAD_HOLD",         "press_mm",    P_NUM,  0 },
        { "HEAD_HOLD",         "drv_zero",    P_BOOL, 2 },
        { "SET_CP_POINT",      "offset_mm",   P_NUM,  0 },
        { "SET_CP_POINT",      "idx",         P_NUM,  0 },
        { "SET_TARGET",        "bpm",         P_NUM,  0 },
        { "SET_TARGET",        "depth_mm",    P_NUM,  0 },
        { "SET_TARGET",        "force_max_N", P_NUM,  0 },
        { "VACUUM_ON",         "target_kPa",  P_NUM,  0 },
        { "STOP_CPR",          "reason",      P_STR,  0 },
        { "SET_TEL_RATE",      "hz",          P_NUM,  0 },
        { "TRIAL_COMPRESSION", "cycles",      P_NUM,  0 },
        { "STIFFNESS_SCAN",    "points",      P_NUM,  0 },
        { "STIFFNESS_SCAN",    "probe_N",     P_NUM,  0 },
    };
    if (!out || n == 0) return;
    out[0] = '\0';
    for (size_t i = 0; i < sizeof SPEC / sizeof SPEC[0]; i++) {
        const ParamSpec& p = SPEC[i];
        if (strcmp(cmd, p.cmd) != 0) continue;
        char val[40];
        bool have = jsonHas(line, p.key);
        if (!have) {
            if (p.dflt == 1) {
                if (!saverF2(val, sizeof val, defSpeed)) strcpy(val, "!");
            } else if (p.dflt == 2) {
                strcpy(val, "false");
            } else {
                strcpy(val, "-");
            }
        } else if (p.kind == P_NUM) {
            float f = 0.0f;
            if (!jsonNum(line, p.key, &f) || !saverF2(val, sizeof val, f)) strcpy(val, "!");
        } else if (p.kind == P_BOOL) {
            bool b = false;
            if (jsonBool(line, p.key, &b)) strcpy(val, b ? "true" : "false");
            else strcpy(val, "!");
        } else {
            if (!jsonStr(line, p.key, val, sizeof val) || !saverSafeStr(val)) strcpy(val, "!");
        }
        _append(out, n, p.key);
        _append(out, n, "=");
        _append(out, n, val);
        _append(out, n, ";");
    }
}

/* ── 기록 칸과 판단 (§2.5) ───────────────────────────────────────── */
const uint32_t AID_LIMIT = 4294967295u;
const size_t   REC_CMD = 20;      /* 가장 긴 명령 TRIAL_COMPRESSION(17자) + NUL */
const size_t   REC_PARAMS = 128;
const size_t   REC_ACK = 128;     /* ack 최악값 104 byte(§3 표) */

struct Record {
    bool     used;
    uint32_t aid;
    uint16_t seq;
    char     cmd[REC_CMD];
    char     params[REC_PARAMS];  /* 정규화한 파라미터 */
    char     ack[REC_ACK];        /* 원래 ACK 본문(LF 없음) */
    bool     done;                /* 완료 이벤트를 저장했는가(act_cur 만) */
    char     result[16];          /* 완료 이벤트의 result */
    char     evt[SAVER_MAX_LINE]; /* 완료 이벤트 본문 */
};

enum Slot { SLOT_NONE = 0, SLOT_ACT_CUR, SLOT_ACT_REJ, SLOT_CMD_LAST };

enum Verdict {
    V_NEW,            /* 6. aid > aid_max: 새 명령으로 평가 */
    V_REPLAY,         /* 3·4. 같은 aid, 같은 seq·명령·파라미터: 재실행 없이 원래 응답 */
    V_CONFLICT,       /* 3·4. 같은 aid, 다른 seq·명령·파라미터: SEQ_CONFLICT */
    V_AID_REQUIRED,   /* 1. bid 나 aid 없음, aid = 0 */
    V_BOOT_MISMATCH,  /* 2. bid ≠ boot_id */
    V_STALE,          /* 5. aid ≤ aid_max 이고 기록에 없음: STALE_SEQ */
    V_EXHAUSTED       /* 7. aid_max 가 4,294,967,295 에 이른 뒤 */
};

inline const char* verdictWhy(Verdict v)
{
    switch (v) {
    case V_CONFLICT:      return "SEQ_CONFLICT";
    case V_AID_REQUIRED:  return "AID_REQUIRED";
    case V_BOOT_MISMATCH: return "BOOT_MISMATCH";
    case V_STALE:         return "STALE_SEQ";
    case V_EXHAUSTED:     return "AID_EXHAUSTED";
    default:              return "";
    }
}

/* 기록 사유(정지 방향 명령은 이 사유로 거부하지 않는다, §2.5-0) */
inline bool isRecordReason(Verdict v)
{
    return v == V_AID_REQUIRED || v == V_BOOT_MISMATCH || v == V_STALE || v == V_EXHAUSTED;
}

struct Request {
    uint16_t    seq;
    const char* cmd;
    const char* params;           /* normalizeParams() 결과 */
    bool        has_bid;
    uint32_t    bid;
    bool        has_aid;
    uint32_t    aid;
};

class Ledger {
public:
    Ledger() { reset(0); }

    /* 부팅 때 한 번: 기록과 aid_max 는 RAM 에만 두고 부팅마다 비운다(§2.5 재부팅) */
    void reset(uint16_t bootId) {
        boot_ = bootId;
        aidMax_ = 0;
        hasLastSeq_ = false;
        lastSeq_ = 0;
        clear(cur_);
        clear(rej_);
        clear(last_);
    }

    /* 판단 순서 1~7. 상태를 바꾸지 않는다. 3·4번(같은 aid)은 aid_max 보다 작아도 5번보다 먼저 본다 */
    Verdict evaluate(const Request& r, Slot* slot) const {
        if (slot) *slot = SLOT_NONE;
        if (!r.has_bid || !r.has_aid || r.aid == 0) return V_AID_REQUIRED;
        if (r.bid != (uint32_t)boot_) return V_BOOT_MISMATCH;
        if (cur_.used && cur_.aid == r.aid) {
            if (slot) *slot = SLOT_ACT_CUR;
            return same(cur_, r) ? V_REPLAY : V_CONFLICT;
        }
        if (last_.used && last_.aid == r.aid) {
            if (slot) *slot = SLOT_CMD_LAST;
            return same(last_, r) ? V_REPLAY : V_CONFLICT;
        }
        if (rej_.used && rej_.aid == r.aid) {
            if (slot) *slot = SLOT_ACT_REJ;
            return same(rej_, r) ? V_REPLAY : V_CONFLICT;
        }
        if (aidMax_ >= AID_LIMIT) return V_EXHAUSTED;
        if (r.aid <= aidMax_) return V_STALE;
        return V_NEW;
    }

    /* V_NEW 를 평가한 결과를 기록한다. 동작 수락 → act_cur, 동작 거부 → act_rej,
     * 즉시 변경·정지 → cmd_last. aid_max 와 last_seq 를 갱신한다. ack 는 호출부가 채운다. */
    Record& recordNew(const Request& r, Slot slot) {
        aidMax_ = r.aid;
        hasLastSeq_ = true;
        lastSeq_ = r.seq;
        Record& rec = record(slot);
        clear(rec);
        rec.used = true;
        rec.aid = r.aid;
        rec.seq = r.seq;
        copy(rec.cmd, sizeof rec.cmd, r.cmd);
        copy(rec.params, sizeof rec.params, r.params);
        return rec;
    }

    /* 동작 결과를 act_cur 에 저장(busy 를 풀기 전에, 송신 전에). aid 가 다르면 false */
    bool storeDone(uint32_t aid, const char* evtLine, const char* result) {
        if (!cur_.used || cur_.aid != aid) return false;
        copy(cur_.evt, sizeof cur_.evt, evtLine);
        copy(cur_.result, sizeof cur_.result, result);
        cur_.done = true;
        return true;
    }

    Record& record(Slot s) {
        if (s == SLOT_ACT_REJ) return rej_;
        if (s == SLOT_CMD_LAST) return last_;
        return cur_;
    }
    const Record& record(Slot s) const {
        if (s == SLOT_ACT_REJ) return rej_;
        if (s == SLOT_CMD_LAST) return last_;
        return cur_;
    }

    uint16_t bootId() const { return boot_; }
    uint32_t aidMax() const { return aidMax_; }      /* GET_INFO last_aid (0 이면 생략) */
    bool     hasLastSeq() const { return hasLastSeq_; }
    uint16_t lastSeq() const { return lastSeq_; }    /* GET_INFO last_seq */

private:
    static void clear(Record& r) {
        r.used = false; r.aid = 0; r.seq = 0; r.done = false;
        r.cmd[0] = r.params[0] = r.ack[0] = r.result[0] = r.evt[0] = '\0';
    }
    static void copy(char* dst, size_t n, const char* src) {
        if (!src) src = "";
        strncpy(dst, src, n - 1);
        dst[n - 1] = '\0';
    }
    static bool same(const Record& rec, const Request& r) {
        return rec.seq == r.seq && strcmp(rec.cmd, r.cmd ? r.cmd : "") == 0 &&
               strcmp(rec.params, r.params ? r.params : "") == 0;
    }

    uint16_t boot_;
    uint32_t aidMax_;
    bool     hasLastSeq_;
    uint16_t lastSeq_;
    Record   cur_, rej_, last_;
};

/* ── 활성 오류 목록 (§3.1 faults, §3.5 GET_FAULTS, §6 원인 지속 거부) ─────── */
struct FaultEntry {
    bool     used;
    char     code[12];        /* s-UU-nnn 8자 */
    uint8_t  sev;
    bool     latched;
    bool     cause;           /* 원인이 남아 있는가 */
    uint32_t order;           /* 처음 생긴 순서 */
    char     why[28];         /* 24자 이하 */
};

class FaultTable {
public:
    static const int N = 16;

    FaultTable() { clearAll(); }

    void clearAll() {
        next_ = 1;
        for (int i = 0; i < N; i++) e_[i].used = false;
    }

    /* 오류 발생. 이미 있으면 원인만 다시 켠다(처음 생긴 순서와 래치는 유지, 래치는 한 번 켜지면 남는다).
     * 새로 넣었으면 true. 목록이 차면 우선순위가 가장 낮은 것을 밀어낸다. */
    bool raise(const char* code, int sev, bool latched, const char* why) {
        int i = find(code);
        if (i >= 0) {
            e_[i].cause = true;
            e_[i].latched = e_[i].latched || latched;
            if (sev > e_[i].sev) e_[i].sev = (uint8_t)sev;
            return false;
        }
        i = freeSlot();
        if (i < 0) {
            i = lowest();
            if (i < 0 || !outranks(sev, e_[i])) return false;
        }
        FaultEntry& f = e_[i];
        f.used = true;
        strncpy(f.code, code, sizeof f.code - 1); f.code[sizeof f.code - 1] = '\0';
        strncpy(f.why, why ? why : "", sizeof f.why - 1); f.why[sizeof f.why - 1] = '\0';
        f.sev = (uint8_t)sev;
        f.latched = latched;
        f.cause = true;
        f.order = next_++;
        return true;
    }

    /* 원인 해소. 래치가 아니면 목록에서 뺀다. 래치면 RESET_FAULT 까지 남긴다 */
    void causeCleared(const char* code) {
        int i = find(code);
        if (i < 0) return;
        e_[i].cause = false;
        if (!e_[i].latched) e_[i].used = false;
    }

    /* RESET_FAULT 성공 처리: 원인이 남은 오류가 없을 때만 모두 지우고 true */
    bool resetAll() {
        for (int i = 0; i < N; i++) if (e_[i].used && e_[i].cause) return false;
        for (int i = 0; i < N; i++) e_[i].used = false;
        return true;
    }

    bool has(const char* code) const { return find(code) >= 0; }
    bool causePresent(const char* code) const { int i = find(code); return i >= 0 && e_[i].cause; }
    int count() const { int n = 0; for (int i = 0; i < N; i++) if (e_[i].used) n++; return n; }

    /* 심각도 3 → 2 → 1, 같은 심각도는 먼저 생긴 순. 개수를 돌려준다 */
    int sorted(const FaultEntry** out, int max) const {
        const FaultEntry* tmp[N];
        int n = 0;
        for (int i = 0; i < N; i++) if (e_[i].used) tmp[n++] = &e_[i];
        for (int a = 1; a < n; a++) {                     /* 삽입 정렬(최대 16개) */
            const FaultEntry* x = tmp[a];
            int b = a - 1;
            while (b >= 0 && before(*x, *tmp[b])) { tmp[b + 1] = tmp[b]; b--; }
            tmp[b + 1] = x;
        }
        int k = n < max ? n : max;
        for (int i = 0; i < k; i++) out[i] = tmp[i];
        return n;
    }

    /* 가장 먼저 보일 오류 코드(없으면 NULL). FAULT_ACTIVE·FAULT_PERSISTS 의 err 에 쓴다 */
    const char* topCode() const {
        const FaultEntry* one[1];
        return sorted(one, 1) > 0 ? one[0]->code : NULL;
    }

private:
    static bool before(const FaultEntry& a, const FaultEntry& b) {
        if (a.sev != b.sev) return a.sev > b.sev;
        return a.order < b.order;
    }
    static bool outranks(int sev, const FaultEntry& victim) { return sev > victim.sev || !victim.latched; }
    int find(const char* code) const {
        for (int i = 0; i < N; i++) if (e_[i].used && strcmp(e_[i].code, code) == 0) return i;
        return -1;
    }
    int freeSlot() const {
        for (int i = 0; i < N; i++) if (!e_[i].used) return i;
        return -1;
    }
    int lowest() const {
        int w = -1;
        for (int i = 0; i < N; i++) {
            if (!e_[i].used) continue;
            if (w < 0 || before(e_[w], e_[i])) w = i;
        }
        return w;
    }

    FaultEntry e_[N];
    uint32_t   next_;
};

}  /* namespace saver_v11 */

#endif /* SAVER_V11_H */
