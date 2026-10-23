#include <cmath>
#include <cstdio>
#include "../../src/demo/ppg_diagnostic.h"
// Compile with the INSTALLED REAL SparkFun/Maxim algorithm, not the mock result
// injector. Synthetic optical waveforms verify plumbing, not medical accuracy.
unsigned checks = 0;
#define CHECK(x)                                                                                   \
  do {                                                                                             \
    ++checks;                                                                                      \
    if (!(x)) {                                                                                    \
      std::fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x);                                         \
      return 1;                                                                                    \
    }                                                                                              \
  } while (0)
int main() {
  selectable::PpgDiagnostic diagnostic;
  saver_max_bench::OpticalWindow normal;
  diagnostic.enable(true);
  for (unsigned i = 0; i < 500; ++i) {
    const double phase = 6.283185307179586 * i / 20.0; // 25 samples/s, 20-sample period.
    const auto red = uint32_t(12000 + 300 * std::sin(phase));
    const auto ir = uint32_t(18000 + 600 * std::sin(phase));
    normal.push(red, ir, i * 40);
    diagnostic.push(red, ir, i * 40);
  }
  int32_t hr = 0, spo2 = 0;
  CHECK(!normal.hrOk() && !normal.spo2Ok() && !normal.count());
  CHECK(diagnostic.heartRate(19960, hr));
  CHECK(hr >= 72 && hr <= 78); // Known synthetic frequency, not a physiological criterion.
  CHECK(diagnostic.saturation(19960, spo2));
  CHECK(spo2 >= 0 && spo2 <= 100); // No claimed synthetic ground-truth oxygen saturation.
  CHECK(!diagnostic.heartRate(20160, hr));
  diagnostic.expire(20160);
  CHECK(!diagnostic.samples());
  diagnostic.enable(true);
  for (unsigned i = 0; i < 200; ++i)
    diagnostic.push(12000, 18000, 20200 + i * 40);
  CHECK(!diagnostic.heartRate(28160, hr));
  CHECK(!diagnostic.saturation(28160, spo2));
  CHECK(diagnostic.samples() == 75);
  diagnostic.push(saver_max_bench::ADC_MAX, 18000, 28200);
  CHECK(!diagnostic.samples());
  CHECK(!diagnostic.heartRate(28200, hr));
  diagnostic.enable(true);
  for (unsigned i = 0; i < 300; ++i) {
    const double phase = 6.283185307179586 * i / 20.0;
    diagnostic.push(uint32_t(120000 + 70000 * std::sin(phase)),
                    uint32_t(150000 + 80000 * std::sin(phase)), 30000 + i * 40);
  }
  CHECK(diagnostic.heartRate(41960, hr));
  CHECK(hr >= 72 && hr <= 78);
  std::printf("PASS %u real-library PPG diagnostic synthetic replay checks; NOT sensor/clinical "
              "validation\n",
              checks);
}
