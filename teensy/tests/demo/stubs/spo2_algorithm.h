#pragma once
#include <stdint.h>
// Synthetic serialization/flag injection only, never physiological validation.
// Defaults remain invalid. Replay tests separately compile the real library.
struct MockPpgAlgorithm {
  unsigned calls = 0;
  int32_t hr = 0, spo2 = 0;
  int8_t hrValid = 0, spo2Valid = 0;
};
extern MockPpgAlgorithm mockPpgAlgorithm;
inline void maxim_heart_rate_and_oxygen_saturation(uint32_t *, int32_t, uint32_t *, int32_t *spo2,
                                                   int8_t *spo2Valid, int32_t *hr,
                                                   int8_t *hrValid) {
  ++mockPpgAlgorithm.calls;
  *hr = mockPpgAlgorithm.hr;
  *spo2 = mockPpgAlgorithm.spo2;
  *hrValid = mockPpgAlgorithm.hrValid;
  *spo2Valid = mockPpgAlgorithm.spo2Valid;
}
