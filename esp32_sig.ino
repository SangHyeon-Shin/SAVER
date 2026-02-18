// ESP32 시리얼 통신 예제
void setup() {
  Serial.begin(115200);    // 시리얼 시작
  delay(1000);
  Serial.println("ESP32 ready!");  // 시작 메시지
}

void loop() {
  // Jetson에서 데이터 받기 확인
  if (Serial.available() > 0) {
    String received = Serial.readStringUntil('\n'); // 줄바꿈 기준으로 읽기
    Serial.print("Received from Jetson: ");
    Serial.println(received);
  }

  // 1초마다 데이터 전송
  static unsigned long lastSend = 0;
  if (millis() - lastSend > 1000) {
    lastSend = millis();
    float dummyPressure = 120.5;      // 예제 센서 값
    int motorPWM = 128;                // 예제 모터 PWM
    // 간단한 CSV 형태로 전송
    Serial.print(dummyPressure);
    Serial.print(",");
    Serial.println(motorPWM);
  }
}
