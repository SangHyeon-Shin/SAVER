/* PC 시험용 Arduino.h 대용 (teensy/tests/host). 실제 Teensy 코어가 아니다.
 * saver_teensy.ino 가 쓰는 것만 흉내 낸다: 시간, 핀 입출력, ADC, USB Serial.
 * 2026-10-23 신상현(Claude Code) 작성, 한건민 검토 필요. */
#ifndef SAVER_HOST_ARDUINO_H
#define SAVER_HOST_ARDUINO_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <math.h>
#include <string>
#if defined(__unix__) || defined(__APPLE__)
#include <errno.h>
#include <unistd.h>
#endif

#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2
#define INPUT_DISABLE 5   /* PC 시험 전용 값 */

/* Teensy 4.1 아날로그 별칭 */
constexpr uint8_t A0 = 14, A1 = 15, A2 = 16, A3 = 17, A4 = 18, A5 = 19, A6 = 20, A7 = 21,
                  A8 = 22, A9 = 23, A10 = 24, A11 = 25, A12 = 26, A13 = 27, A14 = 38,
                  A15 = 39, A16 = 40, A17 = 41;

namespace host {
struct Pins {
  uint64_t now_us = 0;
  int mode[64] = {};
  int level[64] = {};          /* digitalRead 가 돌려줄 값(시험이 정한다) */
  int written[64] = {};        /* 마지막 digitalWrite 값 */
  unsigned long writes[64] = {};
  int analog_in[64] = {};      /* analogRead 가 돌려줄 값 */
  int analog_out[64] = {};
  unsigned long analog_writes[64] = {};
};
inline Pins& pins() { static Pins p; return p; }
}  /* namespace host */

inline uint32_t millis() { return (uint32_t)(host::pins().now_us / 1000u); }
inline uint32_t micros() { return (uint32_t)host::pins().now_us; }
inline void delay(uint32_t ms) { host::pins().now_us += (uint64_t)ms * 1000u; }
inline void pinMode(uint8_t p, int m) { host::pins().mode[p & 63] = m; }
inline void digitalWrite(uint8_t p, int v) { host::pins().written[p & 63] = v; host::pins().writes[p & 63]++; }
inline int digitalRead(uint8_t p) { return host::pins().level[p & 63]; }
inline void analogWrite(uint8_t p, int v) { host::pins().analog_out[p & 63] = v; host::pins().analog_writes[p & 63]++; }
inline int analogRead(uint8_t p) { return host::pins().analog_in[p & 63]; }
inline void analogReadResolution(int) {}
inline void analogReadAveraging(int) {}

/* USB Serial 대용. 기본은 메모리 버퍼(rx: 시험이 넣는 입력, tx: 펌웨어 출력).
 * pty_fd 를 정하면 가상 시리얼(pty)로 실제 바이트를 주고받는다(host_test --pty). */
class HostSerial {
 public:
  void begin(unsigned long) {}
  int available() { pump(); return (int)(rx.size() - rpos); }
  int read() {
    pump();
    if (rpos >= rx.size()) return -1;
    int c = (unsigned char)rx[rpos++];
    if (rpos > 4096 && rpos == rx.size()) { rx.clear(); rpos = 0; }
    return c;
  }
  size_t write(uint8_t c) { tx.push_back((char)c); out(); return 1; }
  size_t write(const uint8_t* b, size_t n) { tx.append((const char*)b, n); out(); return n; }
  size_t write(const char* b, size_t n) { tx.append(b, n); out(); return n; }
  int availableForWrite() { return room; }
  size_t print(const char* s) { tx += s; out(); return strlen(s); }
  size_t println(const char* s) { tx += s; tx += "\r\n"; out(); return strlen(s) + 2; }   /* Arduino 와 같은 CRLF */
  void flush() {}
  explicit operator bool() const { return true; }

  std::string rx;
  size_t rpos = 0;
  std::string tx;
  int room = 6144;       /* availableForWrite() 값. 시험이 줄여 송신 버퍼 부족을 흉내 낸다 */
  int pty_fd = -1;

 private:
  void pump() {
#if defined(__unix__) || defined(__APPLE__)
    if (pty_fd < 0) return;
    char buf[512];
    for (;;) {
      ssize_t n = ::read(pty_fd, buf, sizeof buf);
      if (n <= 0) break;
      rx.append(buf, (size_t)n);
    }
#endif
  }
  void out() {
#if defined(__unix__) || defined(__APPLE__)
    if (pty_fd < 0) return;
    size_t sent = 0;
    while (sent < tx.size()) {
      ssize_t n = ::write(pty_fd, tx.data() + sent, tx.size() - sent);
      if (n > 0) { sent += (size_t)n; continue; }
      if (n < 0 && errno != EAGAIN && errno != EINTR) break;
      usleep(1000);
    }
    tx.clear();
#endif
  }
};
static HostSerial Serial;

#endif /* SAVER_HOST_ARDUINO_H */
