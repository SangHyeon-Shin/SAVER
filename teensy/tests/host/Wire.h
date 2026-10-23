/* PC 시험용 Wire.h 대용 (teensy/tests/host). I²C 장치 없음 */
#ifndef SAVER_HOST_WIRE_H
#define SAVER_HOST_WIRE_H
#include <Arduino.h>
class TwoWire {
 public:
  void begin() {}
  void setSDA(uint8_t) {}
  void setSCL(uint8_t) {}
  void setClock(uint32_t) {}
};
static TwoWire Wire;   /* 메인은 Wire1(NAU7802)을 아직 쓰지 않는다 */
#endif
