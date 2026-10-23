#ifndef SAVER_DEMO_WIRE_PROTOCOL_H
#define SAVER_DEMO_WIRE_PROTOCOL_H
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
namespace saver_demo {
// Complete, flat JSON validation before any command (including STOP) is used.
// No heap, recursive parser, prefix command matching or float-to-int IDs.
class Object {
public:
  enum class Type { String, Number, Boolean, Null };
  struct Field {
    char key[32] = {}, value[96] = {};
    Type type = Type::Null;
  };
  bool parse(const char *s) {
    count_ = 0;
    const char *p = s;
    ws(p);
    if (*p++ != '{')
      return false;
    ws(p);
    if (*p == '}') {
      ++p;
      ws(p);
      return !*p;
    }
    while (*p) {
      if (count_ == 24)
        return false;
      Field &f = fields_[count_];
      if (!string(p, f.key, sizeof(f.key)) || find(f.key))
        return false;
      ws(p);
      if (*p++ != ':')
        return false;
      ws(p);
      if (*p == '"') {
        f.type = Type::String;
        if (!string(p, f.value, sizeof(f.value)))
          return false;
      } else {
        const char *begin = p;
        while (*p && *p != ',' && *p != '}' && *p != ' ' && *p != '\t' && *p != '\r')
          ++p;
        const size_t n = size_t(p - begin);
        if (!n || n >= sizeof(f.value))
          return false;
        memcpy(f.value, begin, n);
        f.value[n] = 0;
        if (!strcmp(f.value, "true") || !strcmp(f.value, "false"))
          f.type = Type::Boolean;
        else if (!strcmp(f.value, "null"))
          f.type = Type::Null;
        else {
          f.type = Type::Number;
          if (!numberSyntax(f.value) || !isfinite(strtod(f.value, nullptr)))
            return false;
        }
      }
      ++count_;
      ws(p);
      if (*p == '}') {
        ++p;
        ws(p);
        return !*p;
      }
      if (*p++ != ',')
        return false;
      ws(p);
    }
    return false;
  }
  const Field *find(const char *key) const {
    for (unsigned i = 0; i < count_; ++i)
      if (!strcmp(fields_[i].key, key))
        return &fields_[i];
    return nullptr;
  }
  const char *str(const char *key) const {
    const auto *f = find(key);
    return f && f->type == Type::String ? f->value : nullptr;
  }
  bool u32(const char *key, uint32_t &v) const {
    const auto *f = find(key);
    if (!f || f->type != Type::Number || !*f->value)
      return false;
    uint32_t n = 0;
    for (const char *p = f->value; *p; ++p) {
      if (*p < '0' || *p > '9' || n > (UINT32_MAX - uint32_t(*p - '0')) / 10)
        return false;
      n = n * 10 + uint32_t(*p - '0');
    }
    v = n;
    return true;
  }
  bool num(const char *key, float &v) const {
    const auto *f = find(key);
    if (!f || f->type != Type::Number)
      return false;
    v = float(strtod(f->value, nullptr));
    return isfinite(v);
  }
  // Sorted, typed canonical form; normalize protocol mm fields to 0.01 mm.
  bool canonical(char *out, size_t cap) const {
    unsigned order[24];
    for (unsigned i = 0; i < count_; ++i)
      order[i] = i;
    for (unsigned i = 1; i < count_; ++i)
      for (unsigned j = i; j && strcmp(fields_[order[j - 1]].key, fields_[order[j]].key) > 0; --j) {
        const unsigned k = order[j];
        order[j] = order[j - 1];
        order[j - 1] = k;
      }
    size_t used = 0;
    out[0] = 0;
    for (unsigned i = 0; i < count_; ++i) {
      const Field &f = fields_[order[i]];
      if (!strcmp(f.key, "ms"))
        continue;
      char value[96];
      if (f.type == Type::Number) {
        double v = strtod(f.value, nullptr);
        if (strstr(f.key, "_mm"))
          v = round(v * 100.0) / 100.0;
        snprintf(value, sizeof(value), "%.17g", v == 0 ? 0 : v);
      } else
        snprintf(value, sizeof(value), "%s", f.value);
      const int n = snprintf(out + used, cap - used, "%u:%s=%u:%u:%s;", unsigned(strlen(f.key)),
                             f.key, unsigned(f.type), unsigned(strlen(value)), value);
      if (n < 0 || size_t(n) >= cap - used)
        return false;
      used += size_t(n);
    }
    return true;
  }

private:
  static void ws(const char *&p) {
    while (*p == ' ' || *p == '\t' || *p == '\r')
      ++p;
  }
  static bool string(const char *&p, char *out, size_t cap) {
    if (*p++ != '"')
      return false;
    size_t n = 0;
    while (*p && *p != '"') {
      unsigned char c = uint8_t(*p++);
      if (c < 32 || c > 126)
        return false;
      if (c == '\\') {
        c = uint8_t(*p++);
        if (c != '"' && c != '\\' && c != '/')
          return false;
      }
      if (n + 1 >= cap)
        return false;
      out[n++] = char(c);
    }
    if (*p++ != '"')
      return false;
    out[n] = 0;
    return true;
  }
  static bool numberSyntax(const char *p) {
    if (*p == '-')
      ++p;
    if (*p == '0')
      ++p;
    else {
      if (*p < '1' || *p > '9')
        return false;
      while (*p >= '0' && *p <= '9')
        ++p;
    }
    if (*p == '.') {
      ++p;
      if (*p < '0' || *p > '9')
        return false;
      while (*p >= '0' && *p <= '9')
        ++p;
    }
    if (*p == 'e' || *p == 'E') {
      ++p;
      if (*p == '+' || *p == '-')
        ++p;
      if (*p < '0' || *p > '9')
        return false;
      while (*p >= '0' && *p <= '9')
        ++p;
    }
    return !*p;
  }
  Field fields_[24];
  unsigned count_ = 0;
};
inline bool query(const char *s) {
  return !strcmp(s, "PING") || !strcmp(s, "GET_STATUS") || !strcmp(s, "GET_INFO") ||
         !strcmp(s, "GET_FAULTS");
}
inline bool stopCommand(const char *s) {
  return !strcmp(s, "STOP_CPR") || !strcmp(s, "PAUSE_CPR") || !strcmp(s, "DISARM") ||
         !strcmp(s, "DRIVE_STOP");
}
inline bool action(const char *s) {
  return !strcmp(s, "HOME") || !strcmp(s, "HEAD_HOLD") || !strcmp(s, "DRIVE_STEP") ||
         !strcmp(s, "SET_CP_POINT") || !strcmp(s, "TRIAL_COMPRESSION") ||
         !strcmp(s, "STIFFNESS_SCAN");
}
struct Record {
  uint32_t aid = 0;
  char key[768] = {}, ack[320] = {}, event[384] = {};
};
class Session {
public:
  uint16_t boot = 0, lastSeq = 0;
  uint32_t maxAid = 0;
  Record accepted, rejected, last;
  const Record *lookup(uint32_t aid) const {
    if (!aid)
      return nullptr;
    if (accepted.aid == aid)
      return &accepted;
    if (rejected.aid == aid)
      return &rejected;
    return last.aid == aid ? &last : nullptr;
  }
  void remember(bool isAction, bool ok, uint32_t aid, uint16_t seq, const char *key,
                const char *ack) {
    Record &r = isAction ? (ok ? accepted : rejected) : last;
    r = Record{};
    r.aid = aid;
    snprintf(r.key, sizeof(r.key), "%s", key);
    snprintf(r.ack, sizeof(r.ack), "%s", ack);
    maxAid = aid;
    lastSeq = seq;
  }
};
// Explicit v1.0 bench compatibility; never selected merely because aid is absent.
class LegacyReceipts {
public:
  struct Receipt {
    uint16_t seq = 0;
    char key[768] = {}, ack[320] = {};
  };
  const Receipt *lookup(uint16_t seq) const {
    for (const auto &r : records_)
      if (r.seq == seq)
        return &r;
    return nullptr;
  }
  void remember(uint16_t seq, const char *key, const char *ack) {
    auto &r = records_[next_];
    next_ = (next_ + 1) % 8;
    r.seq = seq;
    snprintf(r.key, sizeof(r.key), "%s", key);
    snprintf(r.ack, sizeof(r.ack), "%s", ack);
  }

private:
  Receipt records_[8];
  unsigned next_ = 0;
};
// A separate, verified journal from the existing linear calibration slot.
// Two 8-byte slots at 4160 and 4168. Never wrap a used boot ID.
template <class Memory> uint16_t nextBoot(Memory &m) {
  if (m.length() < 4176)
    return 0;
  uint16_t counts[2] = {};
  for (int s = 0; s < 2; ++s) {
    const int p = 4160 + s * 8;
    const uint16_t n = uint16_t(m.read(p + 1)) | uint16_t(m.read(p + 2)) << 8;
    const uint16_t inv = uint16_t(m.read(p + 3)) | uint16_t(m.read(p + 4)) << 8;
    if (m.read(p) == 0xA5 && n && uint16_t(n ^ inv) == 0xFFFF && m.read(p + 5) == 0xD3)
      counts[s] = n;
  }
  const uint16_t previous = counts[0] > counts[1] ? counts[0] : counts[1];
  if (!previous) {
    for (int i = 4160; i < 4176; ++i)
      if (m.read(i) != 0xFF)
        return 0;
  }
  if (previous == 65535)
    return 0;
  const uint16_t n = previous + 1;
  const int p = 4160 + (counts[0] <= counts[1] ? 0 : 8);
  const uint8_t bytes[6] = {0xA5,        uint8_t(n),         uint8_t(n >> 8),
                            uint8_t(~n), uint8_t((~n) >> 8), 0xD3};
  m.update(p, 0);
  for (int i = 1; i < 6; ++i)
    m.update(p + i, bytes[i]);
  for (int i = 1; i < 6; ++i)
    if (m.read(p + i) != bytes[i])
      return 0;
  m.update(p, 0xA5);
  return m.read(p) == 0xA5 ? n : 0;
}
} // namespace saver_demo
#endif
