// mr60bha2_passthrough.ino — SAVER: MR60BHA2 레이더 원시 프레임을 XIAO ESP32C6 의 USB 로 그대로 전달
//
// 왜: Seeed MR60BHA2 키트의 XIAO ESP32C6 에는 기본으로 ESPHome 펌웨어가 들어 있어 USB 로 레이더 원시 프레임이
//     나오지 않는다. 젯슨(jetson/saver/radar.py)은 레이더 원시 프레임(Seeed TinyFrame)을 읽는다.
// 무엇: 레이더 UART(115200 8N1, XIAO D7 = GPIO17 ← 레이더 TX, D6 = GPIO16 → 레이더 RX)와 USB(CDC)를 양방향으로 잇는다.
//     Seeed-mmWave-library 의 examples/passthrough_mode(MIT)와 같은 동작이다. 핀을 적고, 버퍼를 늘리고, 묶음으로 옮긴다.
//     레이더 값을 해석하거나 바꾸지 않는다.
//
// 올리기 A (설치 없이, 미리 빌드한 bin/mr60bha2_passthrough_xiao_esp32c6.bin)
//   Chrome 또는 Edge 에서 https://espressif.github.io/esptool-js/ → Connect(키트 포트) → Flash Address 0x0 에 이 파일
//   → Program → 끝나면 USB 를 다시 꽂는다. 연결이 안 되면 BOOT 버튼을 누른 채 USB 를 꽂고 다시 Connect
// 올리기 B (Arduino IDE 2)
//   1. 파일 > 기본 설정 > 추가 보드 관리자 URL: https://espressif.github.io/arduino-esp32/package_esp32_index.json
//   2. 보드 관리자에서 esp32 (Espressif Systems) 설치
//   3. 도구 > 보드 > esp32 > XIAO_ESP32C6, 도구 > USB CDC On Boot > Enabled(기본값), 도구 > 포트 > 키트 포트
//   4. 업로드. 포트가 안 보이거나 실패하면 BOOT 버튼을 누른 채 USB 를 다시 꽂고 업로드
// 확인 (젯슨): saver-stop 뒤 saver-radar → 프레임 수가 늘고 호흡, 심박, 거리가 보인다
// 되돌리기: Seeed 위키의 MR60BHA2 키트 안내대로 기본(ESPHome) 펌웨어를 다시 올린다
//
// 상태(10/15): arduino-cli 1.1.1, esp32 3.3.12, XIAO_ESP32C6 로 빌드 확인(bin/BUILD_INFO.txt). 실기에 올려 보지는 않았다.

#include <Arduino.h>

#if !ARDUINO_USB_CDC_ON_BOOT
#error "도구 > USB CDC On Boot 를 Enabled 로 바꾼 뒤 다시 컴파일하세요(Serial 이 USB 여야 레이더 자료가 젯슨으로 간다)"
#endif

static const int RADAR_RX_PIN = 17;          // D7: 레이더 TX → XIAO
static const int RADAR_TX_PIN = 16;          // D6: XIAO → 레이더 RX
static const uint32_t RADAR_BAUD = 115200;   // MR60BHA2 기본 속도, 8N1

static uint8_t buf[256];

void setup() {
  Serial.setTxBufferSize(4096);              // USB 쪽: 젯슨이 잠깐 늦게 읽어도 버리지 않게(begin 전에)
  Serial.setRxBufferSize(512);
  Serial.begin(115200);                      // USB CDC 는 속도 값과 무관
  Serial0.setRxBufferSize(4096);             // 레이더 쪽(begin 전에)
  Serial0.begin(RADAR_BAUD, SERIAL_8N1, RADAR_RX_PIN, RADAR_TX_PIN);
}

void loop() {
  bool idle = true;
  int n = Serial0.available();               // 레이더 → USB
  if (n > 0) {
    size_t got = Serial0.read(buf, n < (int)sizeof(buf) ? (size_t)n : sizeof(buf));
    if (got > 0) {
      Serial.write(buf, got);
    }
    idle = false;
  }
  n = Serial.available();                    // USB → 레이더(젯슨은 보내지 않는다. 설정 도구용으로만 열어 둔다)
  if (n > 0) {
    size_t got = Serial.read(buf, n < (int)sizeof(buf) ? (size_t)n : sizeof(buf));
    if (got > 0) {
      Serial0.write(buf, got);
    }
    idle = false;
  }
  if (idle) {
    delay(1);                                // 다른 작업(감시 타이머 포함)에 차례를 넘긴다
  }
}
