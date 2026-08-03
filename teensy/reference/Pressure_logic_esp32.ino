// ---------------- 핀 정의 ----------------
const int sensorPin = 34;       // 압력 센서
const int ledPin = 2;           // 경고 LED
const int startSignalPin = 33;  // 시작 신호
const int stopSignalPin = 32;   // 종료 신호

// 모터 제어 핀
const int motorErrPin = 26;     // ERR (LOW → 오류)
const int motorRunPin = 14;     // RUN (LOW → 모터 동작)
const int motorEnPin = 12;      // EN (LOW → 긴급 정지, HIGH → 재가동)
const int motorVSPPin = 25;     // VSP (PWM 출력, 0~3.3V → 레벨 변환 0~5V)
const int motorFGPin = 27;      // FG (속도 피드백 입력)

const int targetPressure = 30000; // 목표 압력 (Pa)
const int tolerance = 500;        // 허용 오차 ±0.5kPa
const int maxRetries = 3;         // 재시도 최대 횟수
const int retryDelay = 500;       // 재시도 딜레이(ms)

const float sensorMinPressure = 2500;   // 센서 최소 압력 (Pa)
const float sensorMaxPressure = 700000; // 센서 최대 압력 (Pa)

int retryCount = 0;  // 재시도 카운트

enum SystemState { IDLE, NORMAL, RETRY, WARNING, STOP };
SystemState state = IDLE;

// ---------------- setup ----------------
void setup() {
  // 센서 및 신호
  pinMode(sensorPin, INPUT);
  pinMode(ledPin, OUTPUT);
  pinMode(startSignalPin, INPUT);
  pinMode(stopSignalPin, INPUT);

  // 모터 핀
  pinMode(motorErrPin, INPUT);
  pinMode(motorRunPin, INPUT);  // 모터 상태 확인용
  pinMode(motorFGPin, INPUT);   // 회전수 확인용
  pinMode(motorEnPin, OUTPUT);
  pinMode(motorVSPPin, OUTPUT);

  // 모터 초기 상태
  digitalWrite(motorEnPin, LOW);     // 비활성화
  analogWrite(motorVSPPin, 0);       // 속도 0
  digitalWrite(ledPin, LOW);         // 경고 OFF

  Serial.begin(115200);
}

// ---------------- loop ----------------
void loop() {
    int stopSignal = digitalRead(stopSignalPin);
    int motorErr = digitalRead(motorErrPin);

    // 1️⃣ STOP 신호 → 전체 장치 정지
    if (stopSignal == HIGH) {
        analogWrite(motorVSPPin, 0);
        digitalWrite(motorEnPin, LOW);
        Serial.println("STOP signal received! Device halted.");
        state = IDLE;
        retryCount = 0;
        return;
    }

    // 2️⃣ 모터 ERR → 모터만 정지, 경고
    if (motorErr == LOW) {
        analogWrite(motorVSPPin, 0);
        digitalWrite(motorEnPin, LOW);
        digitalWrite(ledPin, HIGH);
        Serial.println("Motor ERROR detected! Motor halted.");
        state = WARNING; // 또는 STOP
        return;
    }

  int startSignal = digitalRead(startSignalPin);
  int adcValue = analogRead(sensorPin);

  // 🔹 센서 ADC → 전압
  float voltage = adcValue * (3.3 / 4095.0);
  // 🔹 전압 → 압력(Pa)
  float pressurePa = sensorMinPressure + (voltage - 0.5) * (sensorMaxPressure - sensorMinPressure) / 4.0;

  // 🔹 FG 신호 읽기 (옵션: RPM 계산)
  int fgPulse = digitalRead(motorFGPin); // 펄스 읽기, 필요 시 pulseCount 세어 속도 계산 가능

  switch(state) {
    case IDLE:
      analogWrite(motorVSPPin, 0);
      digitalWrite(motorEnPin, HIGH); // 재가동 가능
      digitalWrite(ledPin, LOW);
      if(startSignal == HIGH){
        Serial.println("Start signal received, activating pressure control...");
        state = NORMAL;
        retryCount = 0;
      }
      break;

    case NORMAL:
      digitalWrite(motorEnPin, HIGH); // 모터 활성화
      // PWM으로 VSP 출력
      {
        int pwmValue = map(targetPressure - pressurePa, -30000, 30000, 0, 255);
        pwmValue = constrain(pwmValue, 0, 255);
        analogWrite(motorVSPPin, pwmValue);
      }

      if(abs(pressurePa - targetPressure) > tolerance){
        Serial.println("Pressure out of range, retrying...");
        state = RETRY;
        retryCount = 0;
      }
      break;

    case RETRY:
      digitalWrite(motorEnPin, HIGH); // 모터 활성화
      {
        int pwmValue = map(targetPressure - pressurePa, -30000, 30000, 0, 255);
        pwmValue = constrain(pwmValue, 0, 255);
        analogWrite(motorVSPPin, pwmValue);
      }

      delay(retryDelay);
      adcValue = analogRead(sensorPin);
      voltage = adcValue * (3.3 / 4095.0);
      pressurePa = sensorMinPressure + (voltage - 0.5) * (sensorMaxPressure - sensorMinPressure) / 4.0;

      if(abs(pressurePa - targetPressure) <= tolerance){
        Serial.println("Pressure back to normal");
        state = NORMAL;
      } else {
        retryCount++;
        if(retryCount >= maxRetries){
          Serial.println("Retry failed, triggering warning and stop");
          state = WARNING;
        }
      }
      break;

    case WARNING:
      digitalWrite(ledPin, HIGH);
      state = STOP;
      break;

    case STOP:
      analogWrite(motorVSPPin, 0);
      digitalWrite(motorEnPin, LOW); // 긴급 정지
      break;
  }

  // 🔹 모터 RUN 상태 출력 확인
  int motorRunState = digitalRead(motorRunPin);
  if(motorRunState == LOW){
    Serial.println("Motor is running");
  } else {
    Serial.println("Motor stopped");
  }

  delay(100);
}
//12