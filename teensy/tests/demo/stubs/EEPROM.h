#pragma once
#include <stdint.h>
#include <cstring>
class MockMemory {
public:
  MockMemory() { memset(bytes, 0xFF, sizeof(bytes)); }
  int length() const { return sizeof(bytes); }
  uint8_t read(int p) const { return bytes[p]; }
  void update(int p, uint8_t v) {
    if (!failWrites)
      bytes[p] = v;
  }
  bool failWrites = false;
  uint8_t bytes[4284];
};
extern MockMemory EEPROM;
