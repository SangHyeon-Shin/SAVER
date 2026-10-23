#pragma once
#include <stdint.h>
// Integration tests do not substitute invented physiological results.
inline void maxim_heart_rate_and_oxygen_saturation(uint32_t *, int32_t, uint32_t *, int32_t *spo2,
                                                   int8_t *spo2Valid, int32_t *hr,
                                                   int8_t *hrValid) {
  *spo2 = *hr = 0;
  *spo2Valid = *hrValid = 0;
}
