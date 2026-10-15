// mr60bha2_diag.ino — SAVER: MR60BHA2 키트(XIAO ESP32C6)에서 전달 펌웨어가 USB 로 0 바이트를 낼 때 원인을 가르는 진단 펌웨어
//
// 왜: 10/15 전달 펌웨어(../mr60bha2_passthrough)를 올리고 리셋했는데 젯슨에서 받은 바이트가 0 이었다. 그 펌웨어는
//     시작할 때 아무것도 출력하지 않아 (1) 앱이 돌지 않음 (2) USB(HWCDC) 쪽 출력이 막힘 (3) 레이더 UART 에 자료가 없음을
//     밖에서 가를 수 없다.
// 무엇: 레이더 자료를 전달하지 않고, 1초마다 사람이 읽는 글자 한 줄만 USB 로 낸다. 레이더에는 아무것도 보내지 않는다.
//   시작:  "SAVER-DIAG start reset=<리셋 사유 번호> cdc_on_boot=1 rx=17 tx=16 baud=115200"
//   1초마다: "SAVER-DIAG t=<초> rx_total=<레이더에서 받은 누적 바이트> rx_1s=<지난 1초> sof=<0x01 누적> rx_pin=<GPIO17 수준(UART 가 잡은 핀이라 gpio_get_level 로 읽음)>
//            usb_conn=<HWCDC 연결 판단> head=<최근 받은 앞 8바이트 16진수>"
// 읽는 법(젯슨): saver-stop 뒤 saver-radar --dump --seconds 10 (글자 줄이 그대로 보인다. 종류는 text 로 나온다)
//   - 아무것도 안 나옴            → 앱이 돌지 않거나 USB 출력 자체가 안 됨(원인 1 또는 2)
//   - 줄은 나오는데 rx_total=0    → 레이더 UART 에 자료가 없음(원인 3: 배선, 전원, 핀). rx_pin=1 이면 선은 대기 수준(High)
//   - rx_total 이 늘고 sof 도 늚  → 레이더 프레임이 들어옴. 전달 펌웨어의 USB 출력 쪽(원인 2)을 고친다
// 올리기: 전달 펌웨어와 같다(Flash Address 0x0, 합친 이미지). 빌드(PC):
//   arduino-cli compile --fqbn esp32:esp32:XIAO_ESP32C6 --export-binaries jetson/radar_fw/mr60bha2_diag
//   → build/esp32.esp32.XIAO_ESP32C6/mr60bha2_diag.ino.merged.bin 을 0x0 에 올린다
// 진단이 끝나면 전달 펌웨어로 다시 올린다. 이 펌웨어로는 saver-radar, 브리지가 레이더 값을 읽지 못한다.
//
// 상태(10/15): 젯슨에서 작성(빌드 도구 없음). 클라우드 세션에서 arduino-cli 1.1.1, esp32 3.3.12 로 빌드해
//   bin/mr60bha2_diag_xiao_esp32c6.bin(0x0 부터 합친 이미지)을 두었다(bin/BUILD_INFO.txt). 실기에 올려 보지는 않았다.
//   같은 날 전달 펌웨어는 USB 를 다시 꽂은 뒤 정상 동작해(레이더 프레임 수신) 이 펌웨어는 쓰지 않았다.

#include <Arduino.h>
#include <esp_system.h>
#include <driver/gpio.h>

#if !ARDUINO_USB_CDC_ON_BOOT
#error "도구 > USB CDC On Boot 를 Enabled 로 바꾼 뒤 다시 컴파일하세요(진단 줄이 USB 로 나가야 한다)"
#endif

static const int RADAR_RX_PIN = 17;          // D7: 레이더 TX → XIAO (전달 펌웨어와 같은 값)
static const int RADAR_TX_PIN = 16;          // D6: XIAO → 레이더 RX. 이 펌웨어는 보내지 않는다
static const uint32_t RADAR_BAUD = 115200;
static const uint32_t REPORT_MS = 1000;

static uint8_t buf[256];
static uint8_t head[8];
static size_t head_len = 0;
static uint32_t rx_total = 0;
static uint32_t rx_window = 0;
static uint32_t sof_total = 0;
static uint32_t last_report_ms = 0;

static void report_line(uint32_t now_ms) {
  char hex[8 * 3 + 1];
  size_t pos = 0;
  hex[0] = '\0';
  for (size_t i = 0; i < head_len; i++) {
    pos += snprintf(hex + pos, sizeof(hex) - pos, "%02x", head[i]);
  }
  Serial.printf("SAVER-DIAG t=%lu rx_total=%lu rx_1s=%lu sof=%lu rx_pin=%d usb_conn=%d head=%s\r\n",
                (unsigned long)(now_ms / 1000), (unsigned long)rx_total, (unsigned long)rx_window,
                (unsigned long)sof_total, gpio_get_level((gpio_num_t)RADAR_RX_PIN), Serial ? 1 : 0,
                head_len ? hex : "-");
}

void setup() {
  Serial.setTxTimeoutMs(0);                  // USB 쪽이 안 읽혀도 막히지 않게(진단 줄은 버려져도 된다)
  Serial.begin(115200);
  Serial0.setRxBufferSize(4096);
  Serial0.begin(RADAR_BAUD, SERIAL_8N1, RADAR_RX_PIN, RADAR_TX_PIN);
  delay(500);                                // 젯슨이 포트를 다시 열 틈
  Serial.printf("SAVER-DIAG start reset=%d cdc_on_boot=1 rx=%d tx=%d baud=%lu\r\n",
                (int)esp_reset_reason(), RADAR_RX_PIN, RADAR_TX_PIN, (unsigned long)RADAR_BAUD);
  last_report_ms = millis();
}

void loop() {
  int n = Serial0.available();
  if (n > 0) {
    size_t got = Serial0.read(buf, n < (int)sizeof(buf) ? (size_t)n : sizeof(buf));
    for (size_t i = 0; i < got; i++) {
      if (buf[i] == 0x01) {
        sof_total++;
      }
    }
    if (got > 0) {
      head_len = got < sizeof(head) ? got : sizeof(head);
      memcpy(head, buf, head_len);
    }
    rx_total += got;
    rx_window += got;
  }
  while (Serial.available() > 0) {           // USB 에서 온 것은 버린다(레이더로 보내지 않는다)
    Serial.read();
  }
  uint32_t now = millis();
  if (now - last_report_ms >= REPORT_MS) {   // 단조 시계, 넘침에도 안전한 뺄셈
    report_line(now);
    rx_window = 0;
    last_report_ms = now;
  }
  delay(1);
}
