/* PC 시험용 spo2_algorithm.h 대용 (teensy/tests/host). 항상 무효 */
#ifndef SAVER_HOST_SPO2_H
#define SAVER_HOST_SPO2_H
#include <stdint.h>
inline void maxim_heart_rate_and_oxygen_saturation(uint32_t*, int32_t, uint32_t*, int32_t* spo2,
                                                   int8_t* spo2_valid, int32_t* hr, int8_t* hr_valid) {
  *spo2 = 0; *spo2_valid = 0; *hr = 0; *hr_valid = 0;
}
#endif
