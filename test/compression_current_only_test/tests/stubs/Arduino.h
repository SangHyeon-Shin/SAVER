#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <deque>
#include <string>
#include <vector>
#define SERIAL_8N1 0
extern uint32_t mockNow;
extern std::vector<int> mockOrder;
inline uint32_t millis() { return mockNow; }
struct MockFrame {
  uint32_t at;
  std::vector<uint8_t> data;
};
struct MockSerial {
  bool connected = true;
  bool uart = false;
  int space = 4096;
  std::deque<char> input;
  std::string output;
  std::vector<MockFrame> frames;
  explicit MockSerial(bool isUart = false) : uart(isUart) {}
  void setRX(int) {}
  void setTX(int) {}
  void begin(int, int = 0) {}
  explicit operator bool() const { return connected; }
  int available() const { return int(input.size()); }
  int read() {
    const unsigned char c = input.front();
    input.pop_front();
    return c;
  }
  int availableForWrite() const { return space; }
  size_t write(const uint8_t *p, size_t n) {
    if (space < int(n))
      return 0;
    mockOrder.push_back(uart ? 0 : 1);
    if (uart)
      frames.push_back(MockFrame{mockNow, std::vector<uint8_t>(p, p + n)});
    else
      output.append(reinterpret_cast<const char *>(p), n);
    return n;
  }
  void feed(const std::string &s) {
    for (char c : s)
      input.push_back(c);
  }
};
extern MockSerial Serial;
extern MockSerial Serial7;
