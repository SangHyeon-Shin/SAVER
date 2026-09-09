#ifndef SAVER_BENCH_PRESSURE_CONVERSION_H
#define SAVER_BENCH_PRESSURE_CONVERSION_H
#include <limits>

namespace saver_pressure_bench {
constexpr int ADC_MAX = 4095; // analogReadResolution(12).
constexpr float ADC_REFERENCE_V = 3.3f; // Nominal; actual reference has not been measured.
constexpr float DIVIDER_R_TOP_OHM = 4700.0f; // Sensor OUT -> A8.
constexpr float DIVIDER_R_BOTTOM_OHM = 10000.0f; // A8 -> GND.
constexpr float DIVIDER_GAIN =
    (DIVIDER_R_TOP_OHM + DIVIDER_R_BOTTOM_OHM) / DIVIDER_R_BOTTOM_OHM;

// User-identified XGZP6847A100KPGN, 5 V (NOT ...33 or KPGPN).
// CFSensor V2.7 pp. 2, 4, 5: -100..0 kPa, 0.5..4.5 V, K=0.04 V/kPa.
// https://cfsensor.com/wp-content/uploads/2022/11/XGZP6847A-Pressure-Sensor-V2.7.pdf
// Nominal 5 V transfer, with no automatic supply compensation or zero calibration.
constexpr float SENSOR_MIN_V = 0.5f;
constexpr float SENSOR_ZERO_V = 4.5f;
constexpr float PA_PER_V = 25000.0f;
// Electrical plausibility allowance: datasheet +/-2% FSS * 4 V = 0.08 V.
// NOT a medical limit or proof of connection/accuracy.
constexpr float OUTPUT_ALLOWANCE_V = 0.08f;
constexpr float INVALID = std::numeric_limits<float>::quiet_NaN();
constexpr float sensorVoltageToPa(float voltage) { return (voltage - SENSOR_ZERO_V) * PA_PER_V; }

struct PressureSample {
  int raw = -1;
  bool adcOk = false, rail = false, ok = false, inNominalRange = false;
  float adcV = INVALID, sensorV = INVALID, pressurePa = INVALID;
  const char* reason = "NOT_SAMPLED";
};
constexpr PressureSample convertPressure(int raw) {
  PressureSample r;
  r.raw = raw;
  if (raw < 0 || raw > ADC_MAX) { r.reason = "ADC_READ_INVALID"; return r; }
  r.adcOk = true;
  r.adcV = (float)raw * ADC_REFERENCE_V / (float)ADC_MAX;
  r.sensorV = r.adcV * DIVIDER_GAIN;
  r.rail = raw <= 1 || raw >= ADC_MAX - 1;
  if (r.rail) { r.reason = "ADC_RAIL"; return r; }
  if (r.sensorV < SENSOR_MIN_V - OUTPUT_ALLOWANCE_V ||
      r.sensorV > SENSOR_ZERO_V + OUTPUT_ALLOWANCE_V) {
    r.reason = "SENSOR_V_OUT_OF_RANGE"; return r;
  }
  r.ok = true;
  r.inNominalRange = r.sensorV >= SENSOR_MIN_V && r.sensorV <= SENSOR_ZERO_V;
  r.pressurePa = sensorVoltageToPa(r.sensorV); // Never clamp endpoint offsets to 0 Pa.
  r.reason = "OK";
  return r;
}
static_assert(DIVIDER_R_TOP_OHM > 0 && DIVIDER_R_BOTTOM_OHM > 0, "Positive resistors required");
static_assert(SENSOR_ZERO_V / DIVIDER_GAIN < ADC_REFERENCE_V, "Nominal voltage exceeds ADC range");
} // namespace saver_pressure_bench
#endif
