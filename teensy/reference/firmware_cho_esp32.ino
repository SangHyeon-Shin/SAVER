enum DeviceState {
  ESTOP,
  STOP,
  IDLE,
  MEASURE_STIFFNESS,
  VACUUM_FIX,
  PRESS,
  ERROR
  // COOLING 상태는 제거하고 loop()에서 독립 제어
};

DeviceState currentState = IDLE;

// 핀 정의
const int loadCellPin = 34;       // 로드셀
const int vacuumMotorPin = 25;    // 음압 모터
const int blcdMotorPin = 28;      // BLDC 모터 (압박)
const int linearSensorPin = 26;   // 리니어 센서
const int hallSensorPin = 27;     // BLDC 홀 센서
const int moveMotorPin1 = 14;     // 구동계 모터
const int moveMotorPin2 = 12;
const int moveMotorPin3 = 13;
const int moveMotorPin4 = 15;
const int tempSensorPin = 35;     // 열 센서
const int coolingFanPin = 32;     // 팬
const int ppgSensorPin = 36;      // PPG 센서
const int currentSensorPin = 39;  // 전류 센서

// 기준값 예시
const int STIFFNESS_THRESHOLD = 1000;
const int PRESS_THRESHOLD = 2000;
const int TEMP_THRESHOLD = 40;    // Cooling 팬 기준
const int PPG_MIN = 300;          // PPG 정상 범위 최소
const int PPG_MAX = 700;          // PPG 정상 범위 최대

void logEvent(String msg) {
  Serial.println(msg);
}

// 안전/에러 체크 (젯슨 나노 에러 수신)
bool checkSafety() {
  if (Serial.available() > 0) {
    String msg = Serial.readStringUntil('\n');
    msg.trim();
    if (msg == "ERROR") {
      logEvent("Error signal received from Jetson Nano!");
      currentState = ERROR;
      return false; // 안전 체크 실패
    }
  }
  return true; // 안전함
}

void updateStateMachine() {
  switch(currentState) {

    case ESTOP: { // stop과 구분 기준 필요
      analogWrite(vacuumMotorPin, 0);
      analogWrite(blcdMotorPin, 0);
      analogWrite(moveMotorPin1, 0);
      analogWrite(moveMotorPin2, 0);
      analogWrite(moveMotorPin3, 0);
      analogWrite(moveMotorPin4, 0);
      analogWrite(coolingFanPin, 0);
      logEvent("ESTOP! Emergency stop.");
      break;
    }

    case STOP: { // 정상 범위라면
      analogWrite(vacuumMotorPin, 0);
      analogWrite(blcdMotorPin, 0);
      analogWrite(moveMotorPin1, 0);
      analogWrite(moveMotorPin2, 0);
      analogWrite(moveMotorPin3, 0);
      analogWrite(moveMotorPin4, 0);
      // 팬은 loop()에서 독립 제어
      logEvent("STOP state: motors and fans stopped.");
      break;
    }

    case IDLE: {
      analogWrite(vacuumMotorPin, 0);
      analogWrite(blcdMotorPin, 0);
      analogWrite(moveMotorPin1, 0);
      analogWrite(moveMotorPin2, 0);
      analogWrite(moveMotorPin3, 0);
      analogWrite(moveMotorPin4, 0);

      logEvent("IDLE: waiting...");
      if (checkSafety()) currentState = MEASURE_STIFFNESS;
      break;
    }

    case MEASURE_STIFFNESS: {
      logEvent("MEASURE_STIFFNESS: moving and measuring...");
      if (!checkSafety()) break;

      analogWrite(moveMotorPin1, 128);
      analogWrite(moveMotorPin2, 128);
      analogWrite(moveMotorPin3, 128);
      analogWrite(moveMotorPin4, 128);

      int loadValue = analogRead(loadCellPin);
      logEvent("LoadCell: " + String(loadValue));

      if (loadValue > STIFFNESS_THRESHOLD) { 
        analogWrite(moveMotorPin1, 0);
        analogWrite(moveMotorPin2, 0);
        analogWrite(moveMotorPin3, 0);
        analogWrite(moveMotorPin4, 0);
        currentState = VACUUM_FIX;
      }
      break;
    }

    case VACUUM_FIX: { 
      logEvent("VACUUM_FIX: applying vacuum...");
      if (!checkSafety()) break;
      analogWrite(vacuumMotorPin, 128);
      currentState = PRESS;
      break;
    }

    case PRESS: {
        logEvent("PRESS: performing press...");
        if (!checkSafety()) break;
        analogWrite(blcdMotorPin, 128);
        
        int linearVal = analogRead(linearSensorPin);
        int hallVal = digitalRead(hallSensorPin); // 홀 센서 값 읽기
        logEvent("Linear Sensor: " + String(linearVal) + " | Hall Sensor: " + String(hallVal));
        
        if (linearVal > PRESS_THRESHOLD) {
            analogWrite(blcdMotorPin, 0);
            currentState = IDLE;
        }
        break;
    }


    case ERROR: { 
      logEvent("ERROR state! All motors and fans stopped.");
      analogWrite(vacuumMotorPin, 0);
      analogWrite(blcdMotorPin, 0);
      analogWrite(moveMotorPin1, 0);
      analogWrite(moveMotorPin2, 0);
      analogWrite(moveMotorPin3, 0);
      analogWrite(moveMotorPin4, 0);
      analogWrite(coolingFanPin, 0);
      break;
    }
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(loadCellPin, INPUT);
  pinMode(vacuumMotorPin, OUTPUT);
  pinMode(blcdMotorPin, OUTPUT);
  pinMode(linearSensorPin, INPUT);
  pinMode(hallSensorPin, INPUT);

  pinMode(moveMotorPin1, OUTPUT);
  pinMode(moveMotorPin2, OUTPUT);
  pinMode(moveMotorPin3, OUTPUT);
  pinMode(moveMotorPin4, OUTPUT);

  pinMode(tempSensorPin, INPUT);
  pinMode(coolingFanPin, OUTPUT);

  pinMode(ppgSensorPin, INPUT);
  pinMode(currentSensorPin, INPUT);

  logEvent("Device initialized, state: IDLE");
}

void loop() {
  // 1️⃣ 모든 상태에서 PPG/전류/온도 센서 읽고 로그
  int ppgVal = analogRead(ppgSensorPin);
  int currentVal = analogRead(currentSensorPin);
  int tempVal = analogRead(tempSensorPin);
  logEvent("PPG Sensor: " + String(ppgVal) + " | Motor Current: " + String(currentVal) + " | Temp: " + String(tempVal));

  // 2️⃣ PPG 정상 범위이면 STOP 상태로 강제 (ESTOP 제외)
  if (ppgVal >= PPG_MIN && ppgVal <= PPG_MAX && currentState != ESTOP) {
    currentState = STOP;
  }

  // 3️⃣ 상태머신 실행
  updateStateMachine();

  // 4️⃣ 독립 쿨링 제어
  if (tempVal > TEMP_THRESHOLD) analogWrite(coolingFanPin, 200);
  else analogWrite(coolingFanPin, 0);

  delay(100); // 상태 확인 주기
}
