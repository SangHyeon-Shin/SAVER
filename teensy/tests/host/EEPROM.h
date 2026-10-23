/* PC 시험용 EEPROM.h 대용 (teensy/tests/host). Teensy 4.1 크기(4284 byte), 지운 상태 0xFF */
#ifndef SAVER_HOST_EEPROM_H
#define SAVER_HOST_EEPROM_H
#include <Arduino.h>
#define E2END 4283
class EEPROMClass {
 public:
  EEPROMClass() { memset(data, 0xFF, sizeof data); }
  uint8_t read(int a) const { return (a >= 0 && a <= E2END) ? data[a] : 0xFF; }
  void write(int a, uint8_t v) { if (a >= 0 && a <= E2END) { data[a] = v; writes++; } }
  void update(int a, uint8_t v) { if (read(a) != v) write(a, v); }
  int length() const { return E2END + 1; }
  uint8_t data[E2END + 1];
  unsigned long writes = 0;
};
static EEPROMClass EEPROM;
#endif
