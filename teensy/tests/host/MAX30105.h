/* PC 시험용 MAX30105.h 대용 (teensy/tests/host). 센서가 없는 것으로 동작한다(begin 실패) */
#ifndef SAVER_HOST_MAX30105_H
#define SAVER_HOST_MAX30105_H
#include <Wire.h>
#define I2C_SPEED_FAST 400000
class MAX30105 {
 public:
  bool begin(TwoWire&, uint32_t) { return false; }
  void setup(uint8_t, uint8_t, uint8_t, int, int, int) {}
  void check() {}
  bool available() { return false; }
  uint32_t getFIFOIR() { return 0; }
  uint32_t getFIFORed() { return 0; }
  void nextSample() {}
};
#endif
