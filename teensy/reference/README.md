# teensy/reference/ — Git 이력에서 복원한 이전 자료

이 폴더의 파일은 **저장소 이력에서 되살린 한건민의 2026년 1~2월 작업**입니다.
`b2ebf16` 등 최근 커밋에서 삭제됐지만, 여기 담긴 **하드웨어 사실**이
현재 명세에 반영되지 않은 부분이 있어 참고용으로 남겼습니다.

**이 코드를 그대로 쓰지 마세요.** 아래 §2 의 플랫폼 문제 때문입니다.

| 파일 | 원래 이름 | 커밋 | 날짜 |
|---|---|---|---|
| `PCB_circuit_diagram.md` | `PCB회로(중간).md` | `4afa1b1` | 2026-01-22 |
| `Pressure_logic_esp32.ino` | `Pressure_logic.ino` | `a47a6a5` | 2026-02-18 |
| `firmware_cho_esp32.ino` | `firware_cho.ino` | `a47a6a5` | 2026-02-18 |
| `esp32_sig.ino` | 동일 | `94777cb` | 2026-02-18 |
| `Jetson_serial_example.py` | `Jetson.py` | `09aba8b` | 2026-02-18 |

---

## 1. 여기서 얻은 하드웨어 사실

명세와 `saver_teensy.ino` 에 반영했거나, 확인이 필요한 항목입니다.

### 음압 목표값 — 명세 §11 미확정 항목의 후보

`Pressure_logic_esp32.ino` 에 이미 값이 있습니다.

```c
const int targetPressure = 30000;  // 목표 압력 (Pa) = 30 kPa
const int tolerance = 500;         // 허용 오차 ±0.5 kPa
const int maxRetries = 3;
const int retryDelay = 500;        // ms
```

프로토콜의 `vac_kPa` 는 **게이지압 기준 음수**이므로 `-30.0 kPa` 에 해당합니다.
**한건민 확인 필요**: 이 값이 설계 근거가 있는 확정값인지, 임시값인지.

### 압력 센서 사양

```c
const float sensorMinPressure = 2500;    // Pa
const float sensorMaxPressure = 700000;  // Pa
// 전압 → 압력: sensorMin + (V - 0.5) * (sensorMax - sensorMin) / 4.0
```

0.5~4.5 V 출력형 센서입니다. **주의**: 위 변환식은 ESP32 의 12 bit ADC(`/4095.0`)
기준입니다. Teensy 4.1 은 기본 10 bit 이므로 `analogReadResolution(12)` 를
호출하거나 분모를 바꿔야 합니다.

### 음압 모터 드라이버 신호 — 명세에 없던 정보

| 신호 | 방향 | 의미 |
|---|---|---|
| `ERR` | 입력 | LOW → 드라이버 오류 |
| `RUN` | 입력 | LOW → 모터 동작 중 |
| `EN` | 출력 | LOW → 긴급 정지, HIGH → 재가동 |
| `VSP` | 출력 | 속도 지령 PWM (0~3.3 V → 레벨 변환 0~5 V) |
| `FG` | 입력 | 속도 피드백 펄스 |

`ERR` 이 하드웨어 오류 신호이므로 프로토콜 `3-VU-xxx` 계열과 연결됩니다.

### 구동계(바퀴) 모터 — 프로토콜에 명령이 없음

`firmware_cho_esp32.ino` 에 `moveMotorPin1~4` 가 있습니다.
UI 화면 02(바퀴 내리기)·03(환자에게 이동)에 대응하는 것으로 보이는데,
**현재 프로토콜에 이동 관련 명령이 하나도 없습니다.**
명세 §11 에 미확정 항목으로 올렸습니다.

### 냉각 — 프로토콜에 필드가 없음

```c
const int tempSensorPin = 35;
const int coolingFanPin = 32;
const int TEMP_THRESHOLD = 40;   // °C
```

PCB 회로도에는 DS18B20(1-Wire)과 N-MOSFET 팬 드라이버가 있습니다.
telemetry 에 온도·팬 필드를 추가했습니다.

### 전원

PCB 회로도 기준: **Li-Po 4S 12~16.8 V** → 외부 Buck 5 V → PCB 내 Buck 3.3 V.
TVS 다이오드, 스타 접지, 테스트 포인트가 설계에 포함돼 있습니다.

---

## 2. 플랫폼 — Teensy 4.1 로 확정 (2026-08-03)

**PCB 는 발주하지 않았습니다.** 이 폴더의 회로도·펌웨어는 ESP32-S3 기준이므로
**그대로 쓸 수 없습니다.** 하드웨어 사실(센서 사양, 드라이버 신호, 전원 구성)만
참고하고, 핀 배정과 ADC 처리는 Teensy 4.1 기준으로 새로 하세요.

바뀌는 것:

| 항목 | ESP32-S3 | Teensy 4.1 |
|---|---|---|
| ADC 분해능 | 12 bit (0~4095) | 기본 10 bit, 설정 시 12 bit |
| 핀 번호 | 위 코드의 34/33/32/26/14/12/25/27 | **전부 다시 배정 필요** |
| PWM | `analogWrite` 8 bit | `analogWriteResolution` 조정 가능, 고주파 가능 |
| USB | UART 브리지(`/dev/ttyUSB0`) | 네이티브 USB(`/dev/ttyACM0`) |
| 부동소수 | 소프트웨어 | 하드웨어 FPU (제어 루프에 유리) |
| PCB | 현 회로도 그대로 | **회로도 수정 필요** |

`Jetson_serial_example.py` 가 `/dev/ttyUSB0` 을 쓰는 것도 ESP32 전제입니다.
현재 명세는 Teensy 네이티브 USB 기준으로 `/dev/ttyACM0`, VID/PID `16C0:0483` 입니다.

### Teensy 4.1 로 옮길 때 반드시 고칠 것

```cpp
// ESP32 (이전)
float voltage = analogRead(pin) * (3.3 / 4095.0);

// Teensy 4.1 — setup() 에서 분해능을 먼저 지정
analogReadResolution(12);                       // 기본 10 bit → 12 bit
float voltage = analogRead(pin) * (3.3f / 4095.0f);
```

- 핀 번호는 **전부 다시 배정**합니다. 이전 코드의 34/33/32/26/14/12/25/27 은 ESP32 번호입니다.
- `analogWrite` 해상도·주파수도 `analogWriteResolution()`, `analogWriteFrequency()` 로 조정합니다.
- Teensy 4.1 은 하드웨어 FPU 가 있어 제어 루프에서 `float` 연산 부담이 적습니다.
- PCB 회로도를 Teensy 4.1 기준으로 다시 그려야 합니다 (명세 §11-7a).

---

## 3. 상태 이름 대응

`firmware_cho_esp32.ino` 의 상태와 현재 명세 §7 의 대응입니다.

| 이전 (ESP32) | 현재 명세 | 비고 |
|---|---|---|
| `IDLE` | `IDLE` | 동일 |
| `MEASURE_STIFFNESS` | `VACUUM_HOLD` + `STIFFNESS_SCAN` 명령 | 상태가 아니라 명령으로 바뀜 |
| `VACUUM_FIX` | `VACUUM_HOLD` | |
| `PRESS` | `CPR_RUNNING` | |
| `ERROR` | `FAULT` | |
| `STOP` | `HOMED` 또는 `IDLE` | 구분 기준이 코드 주석에도 "필요"로 남아 있음 |
| `ESTOP` | `ESTOP` | 동일 |
| — | `ARMED`, `HOMED`, `CPR_READY`, `CPR_PAUSED` | 신규 |

이전 코드에는 `ARM`/`HOME` 개념이 없었습니다. 현재 명세는 구동 전
인터록 검사(`ARM`)와 원점 복귀(`HOME`)를 분리했습니다.

---

## 4. 참고할 만한 이전 로직

버릴 필요 없는 부분입니다.

- **음압 재시도 로직** — 목표 미달 시 최대 3회 재시도 후 경고.
  현재 명세의 `2-VU-004`(음압 목표 미달)와 잘 맞습니다.
- **STOP 신호 · 모터 ERR 우선 처리** — `loop()` 맨 앞에서 검사하는 구조.
  현재 `.ino` 골격의 `checkEstop()` 과 같은 발상입니다.
- **PCB 테스트 포인트** — ADC, I²C, UART, 1-Wire, 팬 PWM, 모터 PWM.
  8/2 통합시험 때 여기 프로브를 걸면 디버깅이 빨라집니다.
