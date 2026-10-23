#pragma once
#include <stddef.h>
#include <stdint.h>
#include <deque>
#include <string>
#include <vector>
#define ARDUINO_TEENSY41 1
#define LOW 0
#define HIGH 1
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2
#define INPUT_DISABLE 3
#define SERIAL_8N1 0
#define A8 22
#define A16 40
#define A0 14
#define A9 23
#define A17 41
extern uint64_t mockUs;
extern int mockAdc[42], mockDigital[42], mockPwm[42];
extern bool mockVescReplies;
inline uint32_t millis() { return uint32_t(mockUs / 1000); }
inline uint32_t micros() { return uint32_t(mockUs); }
inline void digitalWriteFast(uint8_t p, int v) { mockDigital[p] = v; }
inline int digitalRead(uint8_t p) { return mockDigital[p]; }
inline void pinMode(uint8_t, int) {}
inline void analogWriteResolution(int) {}
inline void analogWriteFrequency(uint8_t, int) {}
inline void analogWrite(uint8_t p, int v) { mockPwm[p] = v; }
inline void analogReadResolution(int) {}
inline void analogReadAveraging(int) {}
inline int analogRead(uint8_t p) { return mockAdc[p]; }
struct MockSerial {
  bool connected = true, uart = false;
  int space = 4096;
  std::deque<uint8_t> input;
  std::string output;
  std::string completedStreams;
  bool previousConnection = true;
  std::vector<int32_t> currents;
  unsigned queries = 0;
  int32_t motorCentiA = 100, inputCentiA = 50;
  void begin(unsigned, int = 0) {}
  void setRX(int) {}
  void setTX(int) {}
  explicit operator bool() {
    if (connected != previousConnection) {
      // Like the Jetson serial client, discard a partial receive line when
      // a connection closes. Never concatenate bytes from different streams.
      const auto end = output.rfind('\n');
      if (end != std::string::npos)
        completedStreams.append(output, 0, end + 1);
      output.clear();
      previousConnection = connected;
    }
    return connected;
  }
  int availableForWrite() const { return space; }
  int available() const { return int(input.size()); }
  int read() {
    const uint8_t v = input.front();
    input.pop_front();
    return v;
  }
  static uint16_t crc(const uint8_t *p, size_t n) {
    uint16_t c = 0;
    for (size_t i = 0; i < n; ++i) {
      c ^= uint16_t(p[i]) << 8;
      for (int j = 0; j < 8; ++j)
        c = uint16_t((c << 1) ^ ((c & 0x8000) ? 0x1021 : 0));
    }
    return c;
  }
  void reply(const uint8_t *p, size_t n) {
    input.push_back(2);
    input.push_back(uint8_t(n));
    for (size_t i = 0; i < n; ++i)
      input.push_back(p[i]);
    const auto c = crc(p, n);
    input.push_back(uint8_t(c >> 8));
    input.push_back(uint8_t(c));
    input.push_back(3);
  }
  size_t write(const uint8_t *p, size_t n) {
    output.append(reinterpret_cast<const char *>(p), n);
    if (!uart || n < 6 || p[0] != 2)
      return n;
    if (p[2] == 6 && n == 10) {
      const uint32_t v =
          (uint32_t(p[3]) << 24) | (uint32_t(p[4]) << 16) | (uint32_t(p[5]) << 8) | p[6];
      currents.push_back(int32_t(v));
    } else if (p[1] == 1) {
      ++queries;
      if (mockVescReplies && p[2] == 0) {
        const uint8_t v[] = {0, 6, 0};
        reply(v, 3);
      }
      if (mockVescReplies && p[2] == 4) {
        uint8_t v[54] = {};
        v[0] = 4;
        v[1] = 1;
        v[2] = 44; // 30 C
        for (unsigned i = 0; i < 4; ++i) {
          v[5 + i] = uint8_t(uint32_t(motorCentiA) >> ((3 - i) * 8));
          v[9 + i] = uint8_t(uint32_t(inputCentiA) >> ((3 - i) * 8));
        }
        v[27] = 0;
        v[28] = 240; // 24 V
        reply(v, 54);
      }
    }
    return n;
  }
  void feed(const std::string &s) {
    for (uint8_t c : s)
      input.push_back(c);
  }
};
extern MockSerial Serial, Serial7;
