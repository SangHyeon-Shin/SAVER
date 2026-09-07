/*
 * SAVER pressure-sensor-only bench test for Teensy 4.1.
 *
 * This sketch does not configure or drive any motor pin.
 *
 * Wiring assumed by the calculation:
 *
 *   sensor OUT -- 4.7 kOhm --+-- Teensy A8 (physical pin 22)
 *                            |
 *                           10 kOhm
 *                            |
 *                           GND
 *
 * Sensor GND and Teensy GND must share the same signal reference.
 * A8 must always remain between 0 V and 3.3 V.
 * With this divider, a 5.0 V sensor output would put about 3.40 V on A8 and
 * is therefore not safe; the assumed sensor output must not exceed 4.5 V.
 *
 * Serial Monitor: 115200 baud
 * Commands: READ, START, STOP, HELP
 * Streaming starts automatically because this sketch only reads a sensor.
 */

#include <Arduino.h>
#include <ctype.h>
#include <string.h>

constexpr uint8_t PIN_PRESSURE = A8;
constexpr uint16_t ADC_MAX_12BIT = 4095;
constexpr float ADC_REFERENCE_V = 3.3f;

/* Edit these values if the resistors are measured more accurately. */
constexpr float DIVIDER_R_TOP_OHM = 4700.0f;      // Sensor OUT to A8.
constexpr float DIVIDER_R_BOTTOM_OHM = 10000.0f;  // A8 to GND.
constexpr float DIVIDER_RATIO =
    DIVIDER_R_BOTTOM_OHM / (DIVIDER_R_TOP_OHM + DIVIDER_R_BOTTOM_OHM);

/*
 * Pressure conversion is intentionally disabled until the complete XGZP6847A
 * part number is confirmed.  KPG, KPGN, KPGPN, and the trailing "33" option
 * use different zero voltages, spans, and pressure directions.
 */

/* User-adjustable print interval: 100 ms = 10 samples per second. */
constexpr uint32_t SAMPLE_PERIOD_MS = 100;
constexpr size_t COMMAND_BUFFER_SIZE = 32;

static_assert(A8 == 22, "This sketch requires the Teensy 4.1 A8 pin mapping");
static_assert(DIVIDER_R_TOP_OHM > 0.0f && DIVIDER_R_BOTTOM_OHM > 0.0f,
              "Divider resistor values must be positive");

bool g_streaming = true;
uint32_t g_lastSampleMs = 0;
char g_commandBuffer[COMMAND_BUFFER_SIZE];
size_t g_commandLength = 0;
bool g_commandOverflow = false;

void printMeasurement() {
  const uint16_t adcRaw = (uint16_t)analogRead(PIN_PRESSURE);
  const float adcVoltage =
      (float)adcRaw * ADC_REFERENCE_V / (float)ADC_MAX_12BIT;

  /* Vin = Vout / divider ratio = Vout * (Rtop + Rbottom) / Rbottom. */
  const float sensorVoltage = adcVoltage / DIVIDER_RATIO;
  const bool adcRail = adcRaw <= 1U || adcRaw >= ADC_MAX_12BIT - 1U;

  Serial.print("DATA,ms=");
  Serial.print(millis());
  Serial.print(",adc_raw=");
  Serial.print(adcRaw);
  Serial.print(",adc_V=");
  Serial.print(adcVoltage, 4);
  Serial.print(",sensor_V=");
  Serial.print(sensorVoltage, 4);
  Serial.print(",adc_rail=");
  Serial.println(adcRail ? 1 : 0);
}

void printHelp() {
  Serial.println("COMMANDS,READ|START|STOP|HELP");
  Serial.println("NOTE,START_and_STOP_control_USB_output_only");
  Serial.println("NOTE,kPa_output_disabled_until_full_XGZP6847A_part_number_is_confirmed");
}

void handleCommand(char* line) {
  for (char* p = line; *p != '\0'; ++p) {
    *p = (char)toupper((unsigned char)*p);
  }

  if (strcmp(line, "READ") == 0) {
    printMeasurement();
  } else if (strcmp(line, "START") == 0) {
    g_streaming = true;
    Serial.println("OK,STREAMING_STARTED");
  } else if (strcmp(line, "STOP") == 0) {
    g_streaming = false;
    Serial.println("OK,STREAMING_STOPPED");
  } else if (strcmp(line, "HELP") == 0) {
    printHelp();
  } else {
    Serial.println("ERROR,UNKNOWN_COMMAND");
  }
}

void pollSerial() {
  while (Serial.available() > 0) {
    const char c = (char)Serial.read();
    if (c == '\r') continue;

    if (c == '\n') {
      if (g_commandOverflow) {
        Serial.println("ERROR,COMMAND_TOO_LONG");
      } else if (g_commandLength > 0) {
        g_commandBuffer[g_commandLength] = '\0';
        handleCommand(g_commandBuffer);
      }
      g_commandLength = 0;
      g_commandOverflow = false;
      continue;
    }

    if (g_commandLength < COMMAND_BUFFER_SIZE - 1U) {
      g_commandBuffer[g_commandLength++] = c;
    } else {
      g_commandOverflow = true;
    }
  }
}

void setup() {
  pinMode(PIN_PRESSURE, INPUT);
  analogReadResolution(12);
  analogReadAveraging(16);

  Serial.begin(115200);
  const uint32_t waitStartedMs = millis();
  while (!Serial && (uint32_t)(millis() - waitStartedMs) < 2000U) {
    /* Sensor acquisition can start after the short USB Serial wait. */
  }

  Serial.println("SAVER_PRESSURE_SENSOR_TEST_READY");
  Serial.println("PIN,A8=22,ADC_BITS=12,ADC_MAX_V=3.3");
  Serial.print("DIVIDER,sensor_out--4k7--A8--10k--GND,ratio=");
  Serial.println(DIVIDER_RATIO, 6);
  Serial.println("WARNING,sensor_output_must_not_reach_5V,A8_would_exceed_3V3");
  Serial.println("WARNING,pressure_kPa_disabled_full_sensor_part_number_required");
  Serial.println("DATA_HEADER,ms,adc_raw,adc_V,sensor_V,adc_rail");
  printHelp();
}

void loop() {
  pollSerial();

  const uint32_t nowMs = millis();
  if (g_streaming && (uint32_t)(nowMs - g_lastSampleMs) >= SAMPLE_PERIOD_MS) {
    g_lastSampleMs = nowMs;
    printMeasurement();
  }
}
