#pragma once
#include "Arduino.h"
struct TwoWire {
  unsigned begins = 0, reads = 0, writes = 0;
  bool fail = false;
  uint8_t address = 0, pointer = 0, regs[256] = {};
  int32_t nauRaw = 100000;
  std::vector<uint8_t> tx;
  std::deque<uint8_t> rx;
  void setSDA(int) {}
  void setSCL(int) {}
  void begin() { ++begins; }
  void setClock(unsigned) {}
  void beginTransmission(uint8_t a) {
    address = a;
    tx.clear();
  }
  size_t write(uint8_t v) {
    tx.push_back(v);
    return 1;
  }
  int endTransmission(bool = true) {
    if (fail)
      return 4;
    if (tx.empty())
      return 4;
    pointer = tx[0];
    if (tx.size() > 1) {
      ++writes;
      regs[pointer] = tx[1];
      if (address == 0x57 && pointer == 9 && tx[1] == 0x40)
        regs[9] = 0;
      if (address == 0x2A && pointer == 2 && tx[1] == 0x74)
        regs[2] = 0x70;
    }
    return 0;
  }
  uint8_t requestFrom(uint8_t a, uint8_t n) {
    ++reads;
    rx.clear();
    if (fail)
      return 0;
    for (unsigned i = 0; i < n; ++i) {
      const uint8_t r = uint8_t(pointer + i);
      uint8_t v = regs[r];
      if (a == 0x2A && r == 0)
        v = 0x2E;
      if (a == 0x2A && r >= 0x12 && r <= 0x14)
        v = uint8_t(uint32_t(nauRaw) >> ((0x14 - r) * 8));
      rx.push_back(v);
    }
    return n;
  }
  int available() const { return int(rx.size()); }
  int read() {
    const auto v = rx.front();
    rx.pop_front();
    return v;
  }
};
extern TwoWire Wire, Wire1;
