#pragma once
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include <stddef.h>
#include <stdint.h>
#include <deque>
#include <string>

#define LOW 0
#define HIGH 1
#define INPUT 0
#define OUTPUT 1
#define A8 22

extern uint32_t mockNow;
extern int mockAdc;
extern int mockDigital[42];
extern int mockPwm[42];
extern unsigned mockAdcReads;

inline uint32_t millis() { return mockNow; }
inline void digitalWriteFast(uint8_t pin, int value) { mockDigital[pin] = value; }
inline int digitalRead(uint8_t pin) { return mockDigital[pin]; }
inline void pinMode(uint8_t, int) {}
inline void analogWriteResolution(int) {}
inline void analogWriteFrequency(uint8_t, float) {}
inline void analogWrite(uint8_t pin, int value) { mockPwm[pin] = value; }
inline void analogReadResolution(int) {}
inline void analogReadAveraging(int) {}
inline int analogRead(uint8_t) {
  ++mockAdcReads;
  return mockAdc;
}

struct MockSerial {
  bool connected = true;
  int space = 4096;
  std::deque<char> input;
  std::string output;
  void begin(int) {}
  explicit operator bool() const { return connected; }
  int availableForWrite() const { return space; }
  int available() const { return int(input.size()); }
  int read() {
    const unsigned char value = input.front();
    input.pop_front();
    return value;
  }
  size_t write(const uint8_t *data, size_t size) {
    output.append(reinterpret_cast<const char *>(data), size);
    return size;
  }
  size_t write(uint8_t value) {
    output.push_back(char(value));
    return 1;
  }
  void feed(const std::string &text) {
    for (char value : text)
      input.push_back(value);
  }
};
extern MockSerial Serial;
