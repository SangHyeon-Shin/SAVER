# 하드웨어 매핑

SAVER의 Teensy 4.1 배선 기준입니다. 단일 기준은 `teensy/pinmap.h`이며, 이 문서와
코드의 핀 번호는 함께 변경합니다.

> **상태 (2026-10-02):** 음압 모터 2 RUN/EN은 32/37, WT901C Serial8은 RX34/TX35입니다.
> NAU7802는 독립 Wire1 SDA17/SCL16, 주행 모터 1 엔코더는 11/12로 옮겼습니다.
> 코드의 핀 배정/시험 버스 설정 반영과 실물 재배선·동작 검증은 별개입니다.
> 두 음압 FG는 계속 미사용이며 Hall은 VESC에만 연결합니다. 사용자 제공 단일 모터
> 구동 관찰·로그는 §3.2에 구분했습니다. 신호 전압·보호·보정·통합 검증 완료를 뜻하지 않습니다.
> 배선 변경 시 24 V와 USB를 모두 끄고, FG 및 모터 +5 V 출력선은 분리·절연합니다.

---

## 1. 제어 보드 — Teensy 4.1

이력의 ESP32-S3 회로도와 펌웨어는 신호 의미만 참고합니다. 핀 번호를 이식하지 않고
Teensy 4.1의 외곽 핀 기능을 기준으로 다시 배정했습니다. ESP32와 같은 숫자가 일부
나오는 것은 우연이며 같은 물리 핀을 뜻하지 않습니다.

| 항목 | Teensy 4.1 |
|---|---|
| ADC | `setup()`에서 `analogReadResolution(12)` 사용 |
| USB | 네이티브 USB Serial; GPIO 0/1과 무관 |
| 로직 전압 | GPIO와 ADC 모두 3.3 V 전용, 5 V tolerant 아님 |
| 핀 범위 | 외곽 through-hole `0~41`만 사용 |
| 하단 패드 | `42~54` 미사용; QSPI 패드 리드선 납땜 불필요 |
| 핀 배정 | `teensy/pinmap.h` 기준, 2026-10-02 Serial8 / 독립 Wire1 배정 |
| 실물 상태 | 단일 음압 모터 구동 사용자 관찰 있음; 전압·보호·통합 검증 미완료 |

### 1.1 전체 핀 배정

방향은 Teensy를 기준으로 적습니다.

| 구분 | 장치 | 신호 | 방향 | Teensy 핀 | 선택 근거 |
|---|---|---|---|---:|---|
| UART | 압박 드라이버 | RX7 ← Driver TX | 입력 | `28` | 고정 Serial7 RX 경로, USB와 독립 |
| UART | 압박 드라이버 | TX7 → Driver RX | 출력 | `29` | RX7과 인접한 고정 Serial7 TX 경로 |
| 인터럽트 | 압박부 | Encoder A | 입력 | `30` | B상 31과 인접 |
| 인터럽트 | 압박부 | Encoder B | 입력 | `31` | A상 30과 인접 |
| 예비 | 압박부 | Hall 미연결 예약 | — | `39` | 사용자 확인: 모터 Hall은 VESC 전용, Teensy로 분기 안 함 |
| UART 예약 | WT901C-TTL | RX8 ← Sensor TX | 입력 | `34` | 기본 Serial8 RX, Serial4/5 엔코더 충돌 회피 |
| UART 예약 | WT901C-TTL | TX8 → Sensor RX | 출력 예정 | `35` | 기본 Serial8 TX; 메인 드라이버 미구현 동안 high-Z |
| PWM | 음압 모터 1 | VSP | 출력 | `24` | `FlexPWM1_2`; 독립 주파수 그룹 |
| 입력 | 음압 모터 1 | RUN | 입력 | `25` | 모터 1 제어선과 인접 |
| 출력 | 음압 모터 1 | EN | 출력 | `26` | 모터 1 독립 disable |
| 입력 | 음압 모터 1 | ERR | 입력 | `27` | EN과 인접한 상태선 |
| 미연결 | 음압 모터 1 | FG | — | 없음 | 사용자 결정으로 속도 펄스 미사용, 선 끝 절연 |
| PWM | 음압 모터 2 | VSP | 출력 | `33` | `FlexPWM2_0`; 모터 1과 주파수 독립 |
| 입력 | 음압 모터 2 | RUN | 입력 | `32` | 기존 FG 예비를 재사용하여 Serial8 RX34 확보 |
| 출력 | 음압 모터 2 | EN | 출력 | `37` | 기존 FG 예비를 재사용하여 Serial8 TX35 확보, 독립 disable |
| 입력 | 음압 모터 2 | ERR | 입력 | `36` | 기존 상태 입력 유지, EN37과 인접 |
| 미연결 | 음압 모터 2 | FG | — | 없음 | 사용자 결정으로 속도 펄스 미사용, 선 끝 절연 |
| PWM | 냉각팬 1 | PWM | 출력 | `5` | `FlexPWM2_1`; 독립 주파수 그룹 |
| 인터럽트 | 냉각팬 1 | TACH | 입력 | `38` | 팬 RPM 펄스 측정용 |
| PWM | 냉각팬 2 | PWM | 출력 | `2` | `FlexPWM4_2`; 팬 1과 주파수 독립 |
| 인터럽트 | 냉각팬 2 | TACH | 입력 | `9` | DS18B20 제거로 해제된 외곽 핀 |
| 안전 | 상단 리미트 | 상태 | 입력 | `3` | 하단 리미트 4와 인접 |
| 안전 | 하단 리미트 | 상태 | 입력 | `4` | 상단 리미트 3과 인접 |
| 안전 | E-stop | 보조접점 상태 | 입력 | `6` | 물리 E-stop 상태 보고 전용 |
| ADC | 온도 | 2선 NTC 분압 | 입력 | `A0 / 14` | 아날로그 온도 측정용 |
| ADC | 압력 | 전처리 출력 | 입력 | `A8 / 22` | 센서용 외곽 ADC 입력 |
| 예비 | 확장 | ADC 미연결 예약 | — | `A9 / 23` | 로드셀은 NAU7802 I²C 사용; 원시 브리지 직결 금지 |
| ADC | 리니어 센서 | 변위 전압 | 입력 | `A16 / 40` | 외곽 고번호 ADC 입력 |
| ADC | 전류 센서 | 전류 전압 | 입력 | `A17 / 41` | A16과 인접한 센서 하네스 |
| I²C | MAX30102 | SDA | 양방향 | `18` | 기본 `Wire` SDA |
| I²C | MAX30102 | SCL | 출력 | `19` | 기본 `Wire` SCL |
| 미연결 | MAX30102 | INT | — | 없음 | 현재 폴링 방식에서 사용하지 않음 |
| I²C | NAU7802 | SDA | 양방향 | `17` | 독립 `Wire1` SDA, MAX30102와 버스 분리 |
| I²C | NAU7802 | SCL | 출력 | `16` | 독립 `Wire1` SCL, 기존 엔코더는 11/12로 이동 |
| 미연결 | NAU7802 | DRDY | — | 없음 | 신규 변환 준비 비트는 I²C로 폴링 |
| PWM | BTS7960 모터 1 | RPWM | 출력 | `7` | `FlexPWM1_3_B`; LPWM과 주파수 공유 |
| PWM | BTS7960 모터 1 | LPWM | 출력 | `8` | `FlexPWM1_3_A`; RPWM과 인접 |
| 출력 | BTS7960 모터 1 | R_EN + L_EN | 출력 | `15` | 모듈 1 양쪽 하프브리지 동시 disable |
| 인터럽트 | 구동 모터 1 | Encoder A | 입력 | `11` | B상 12와 인접, Wire1 SCL16 충돌 해소 |
| 인터럽트 | 구동 모터 1 | Encoder B | 입력 | `12` | A상 11과 인접, Wire1 SDA17 충돌 해소 |
| PWM | BTS7960 모터 2 | RPWM | 출력 | `0` | 남은 외곽 PWM 출력 `FlexPWM1_1` |
| PWM | BTS7960 모터 2 | LPWM | 출력 | `1` | 인접 PWM 출력 `FlexPWM1_0` |
| 출력 | BTS7960 모터 2 | R_EN + L_EN | 출력 | `10` | 모듈 2를 모듈 1과 독립 disable |
| 인터럽트 | 구동 모터 2 | Encoder A | 입력 | `20` | B상 21과 인접 |
| 인터럽트 | 구동 모터 2 | Encoder B | 입력 | `21` | A상 20과 인접 |
| 미연결 | BTS7960 두 모듈 | R_IS / L_IS | — | 없음 | 출력 특성과 전압 확인 전 사용하지 않음 |
| 예비 | 확장 | SCK 후보 | — | `13` | 현재 미연결, 내장 LED 연결 핀 |

`16/17`은 디지털 핀이고 `A16/A17`은 물리 핀 `40/41`입니다. 이름은 비슷하지만
서로 다른 핀이므로 충돌하지 않습니다.

### 1.2 PWM 타이머 그룹

Teensyduino 1.62.0 `cores/teensy4/pwm.c` 기준입니다.

| 출력 | 핀 | 그룹 | 관계 |
|---|---:|---|---|
| 음압 모터 1 VSP | `24` | `FlexPWM1_2` | 독립 |
| 음압 모터 2 VSP | `33` | `FlexPWM2_0` | 독립 |
| 팬 1 PWM | `5` | `FlexPWM2_1` | 독립 |
| 팬 2 PWM | `2` | `FlexPWM4_2` | 독립 |
| BTS7960 모터 1 RPWM/LPWM | `7/8` | `FlexPWM1_3` | 의도적으로 주파수 공유 |
| BTS7960 모터 2 RPWM | `0` | `FlexPWM1_1` | LPWM과 주파수를 각각 설정 |
| BTS7960 모터 2 LPWM | `1` | `FlexPWM1_0` | RPWM과 주파수를 각각 설정 |

핀 3은 핀 2와, 핀 4는 핀 33과 PWM 그룹을 공유하지만 리미트 스위치의 일반
디지털 입력으로만 사용하므로 PWM 주파수 설정에 참여하지 않습니다.

근거: [PJRC Teensy 4.1 핀 카드](https://www.pjrc.com/teensy/card11a_rev4_web.pdf),
[Teensy 4.x PWM 코어](https://github.com/PaulStoffregen/cores/blob/master/teensy4/pwm.c)

### 1.3 버스와 확장 제약

- 네이티브 USB Serial은 GPIO를 사용하지 않으므로 Jetson 통신에 영향이 없습니다.
- 압박 모터용 `Serial7`의 `28/29`와 MAX30102용 기본 `Wire`의 `18/19`는 보존합니다.
- WT901C는 `Serial8 RX34/TX35` 예약입니다. 센서 TX→34, 센서 RX←35로 교차 연결합니다.
  메인 수신 드라이버·baud 설정은 미구현이므로 핀 예약만으로 센서 취득을 주장하지 않습니다.
- NAU7802는 독립 `Wire1 SDA17/SCL16`, MAX30102는 `Wire SDA18/SCL19`입니다.
  두 버스의 SDA/SCL을 서로 묶지 않습니다. 전원과 신호 GND는 각 모듈 규격에 맞춥니다.
  메인 NAU7802 취득은 미구현이며 반력 기록 시험만 Wire1을 사용합니다.
- `Serial1~6`, `Wire2`의 기본 핀은 다른 신호가 사용합니다.
- 기본 SPI MOSI/MISO `11/12`는 주행 모터 1 엔코더, 기본 CS `10`은 모터 2 EN이 사용합니다.
  기본 외부 SPI를 함께 사용할 수 없습니다. Teensy 내장 microSD는 SDIO라 이 핀들과 별개입니다.
- `42~54`는 사용하지 않으므로 하단 SMT/QSPI 패드에 리드선을 납땜하지 않습니다.
- 실제 신호 배정 39개, 미연결 예비는 `13/23(A9)/39` 세 개입니다. 팬 TACH는 그대로 유지합니다.

### 1.4 이번 재배선 순서 (`planned`, 실물 미검증)

1. USB와 모든 모터·센서 전원을 끕니다. 구 펌웨어와 새 배선을 혼용하지 않습니다.
2. 음압 2 RUN **34→32**, EN **35→37**; VSP33/ERR36은 유지합니다.
3. 주행 모터 1 엔코더 A/B **16/17→11/12**; 모터 2 엔코더 20/21은 유지합니다.
4. NAU7802 SDA/SCL **18/19→17/16**. MAX30102는 18/19 유지입니다.
5. WT901C TX→34, RX←35는 TTL 전압·전원과 센서 설정을 확인한 뒤 연결합니다.
   새 메인 펌웨어는 아직 자이로를 읽지 않습니다. Hall 선은 VESC에만 두고 39는 미연결입니다.
6. 각 I²C 버스의 풀업을 별도로 확인합니다. 버스 분리는 공통 MCU·전원 장애의 독립을 뜻하지 않습니다.
   전압·도통·단락·EN 부팅 LOW 및 재연결은 모터 전원을 분리하고 검증합니다.

관련 코드: `teensy/pinmap.h`, 메인 초기화, `test/test.ino`, PPG EN 보호 핀,
`test/reaction_force_logger`의 초기화·읽기·재연결을 함께 변경합니다. `protocol/`은 변경하지 않습니다.

### 1.5 핀 변경 검증 기록 (2026-10-02)

실물 검증 수준은 계속 `planned`입니다. 아래는 컴파일/소스/모의 검사 결과이지
핀 전압·버스 통신·모터 작동 시험이 아닙니다. 장치 포트를 열거나 업로드하지 않았습니다.

- Teensyduino 1.62.0 / Arduino CLI 1.5.1 / `teensy:avr:teensy41`, `--warnings all`로
  메인, 음압+압력 통합, PPG, 반력 기록기 **4개 스케치 컴파일·링크 통과**.
  메인은 임시 `saver_teensy/` 폴더에 `.ino`/헤더/`src/`를 그대로 복사해 빌드했습니다.
- `teensy/tests/check_pinmap.ps1`: 실제 코어 헤더를 포함한 19개 컴파일타임 assertion,
  중복/하단 패드 금지/예비 핀 유지, 메인/통합/PPG/Wire1 코드 일치 검사 통과.
- 벤치 검사 11개 스크립트와 위 핀 검사에서 **소스에 명시된 assertion 총 646개 통과**.
  반력 CSV 검사기의 합성 fixture 8개도 통과했습니다. 모의 데이터이며 실측 기록이 아닙니다.
- `teensy/test_saver_proto.cpp`: ARM `-std=c++11 -Wall -Wextra -Werror -fsyntax-only` 통과.
  **PC용 g++가 없어 네이티브 실행 시험은 미실행**이며 ARM 문법 검사가 이를 대체하지 않습니다.
- 새 검사는 처음에 코어에 맞지 않는 C++14/키보드 매크로 누락으로 실패하여,
  설치된 Teensy 코어 기준 GNU++17/US_ENGLISH 옵션으로 맞췄습니다.
  `<Wire.h>`를 버스 접근으로 오인한 검사 정규식도 실제 메서드 호출만 검사하도록 고쳤습니다.
- 기존 `test/tests/test_combined.cpp`가 시간제한 제거 이전 API를 호출하여 실패했습니다.
  **실제 모터 제어 로직은 바꾸지 않고** 현재 수동 STOP API, 자동 종료 없음, 옛 시간 인자
  거부 검사를 맞췄습니다. ERR/USB/잘못된 명령 정지 검사는 유지했습니다.
- 새 별도 `saver-perfboard-solder-only-30x42.html` 연결표를 현재 로컬 핀맵과 동기화했습니다.
  브라우저 360/736/1600 px, 밝음/어두움 총 6개 표시 검사에서 스크립트 오류·외부 요청이
  없었고 연결 모델 검사도 통과했습니다. 기존 HTML 두 개는 해시가 그대로입니다.
  이는 `simulated` 연결 모델/표시 검사이며 실제 납 패턴의 도통·전류·내구성 시험이 아닙니다.
  이 핀 변경 검사 당시 압력 4.7 kΩ/10 kΩ 분압은 외부 회로 표시뿐이었습니다.
  이후 같은 새 HTML에 앞면 저항 실장과 뒷면 납 경로를 추가했습니다(`planned`, 실물 미제작).
  R1 4.7 kΩ: C14/R09↔C08/R09, R2 10 kΩ: C07/R09↔C07/R13(앞면 기준).
  센서 OUT은 H-T-4(C14/R07), A8은 소켓 C14/R11이며 두 신호망 사이에 R1이 있습니다.
  **C14/R10은 비워야 하며 원래 H-T-4→A8 직결 납 경로를 남기면 안 됩니다.**
  기존 외부 분압과 중복 사용하지 않습니다. 정상 출력 4.5 V의 공칭 분압 출력은 약 3.061 V지만,
  5.0 V 출력은 약 3.401 V이므로 일반적인 5 V 입력 보호 회로가 아닙니다.
  저항 크기·홀 피치·소켓/USB 간섭·납의 내구성·전원 순서·역급전은 실물 검증 전입니다.

컴파일 경고: 수정한 스케치/핀맵 자체는 0개. 외부 SparkFun 라이브러리 4종이 메인과
PPG에서 각각 관찰됐습니다. 외부 라이브러리는 수정하거나 경고를 숨기지 않았습니다.

| 외부 파일 | 경고 |
|---|---|
| `spo2_algorithm.h:83` | `an_x` 미사용 |
| `spo2_algorithm.h:84` | `an_y` 미사용 |
| `heartRate.cpp:108` | 비교식과 비트 AND의 괄호 권고 |
| `MAX30105.cpp:292` | `originalContents` 미사용 |

주요 재검증 명령(저장소 루트):

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File teensy/tests/check_pinmap.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File test/tests/check_combined.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File test/vacuum_motor_stage4_fg/tests/check_stage4.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File test/max30102_sensor_test/tests/check_contact.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File test/reaction_force_logger/tests/check_logger.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File test/reaction_force_logger/tools/check_recording.ps1 -SelfTest
arduino-cli compile --fqbn teensy:avr:teensy41 --warnings all --build-property 'recipe.hooks.postbuild.1.pattern=' <스케치 폴더>
& "$env:USERPROFILE/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe" tools/consistency_audit.py
git diff --check
```

추가 회귀검사: `test/linear_sensor_test/tests/check_linear.ps1`,
`test/compression_motor_single_test/tests/check_vesc_bench.ps1`,
`test/compression_linear_pid_test/tests/`의 `check_bench`, `check_calibration_capture`,
`check_current_control`, `check_depth_control`, `check_vesc_link` `.ps1`도 실행했습니다.
Loader 실행 방지를 위해 위 postbuild hook을 비웠으며 컴파일은 업로드가 아닙니다.
이 PC의 `python` 명령은 Microsoft Store 별칭이라 실패하므로 위 실제 실행 파일을 사용했습니다.

UART 핀 근거: 설치된 PJRC `HardwareSerial8.cpp` 및
[PJRC UART 핀 표](https://www.pjrc.com/teensy/td_uart.html).
Wire1 핀 근거: 설치된 `libraries/Wire/WireIMXRT.cpp`의 Teensy 4.1 SDA17/SCL16 설정.

---

## 2. 전원과 전기적 조건

설계 기준(`planned`) 주전원은 `docs/PROJECT_STATE.md` §2의 **6S Li-Po 공칭
22.2 V(19.8~25.2 V) ×2 병렬**입니다. 기존 제어 보드 PCB 회로도의 4S 입력과의
정합, 블로워 팬 커브의 24 V 조건과 실제 공급 전압의 차이는 같은 문서 §3의
미확정 항목으로 유지합니다. 이 설계 기록은 배터리·보호 회로의 실물 검증 완료를
뜻하지 않습니다.

현재 핀맵의 BTS7960 구동 모터는 사용자 확인 기준 24 V이며, 보고서 전원 분배의
구동 모터 12 V 서술과 다릅니다. 모터 실물 정격·공급 전압·보호 회로를 확인하기
전에는 두 구성을 같은 것으로 취급하지 않습니다. 아래는 24 V 벤치 전원 배선
개념이며 최종 배터리 회로가 아닙니다(`planned`).

```text
24 V 모터 전원
  ├─ 개별 퓨즈 → BTS7960 #1 B+/B-
  ├─ 개별 퓨즈 → BTS7960 #2 B+/B-
  └─ DC-DC → 절연되지 않은 5 V 논리/센서 레일

Teensy 3.3 V GPIO → 필요한 레벨 변환/버퍼 → 드라이버 입력
모든 비절연 신호의 GND → 공통 기준(스타 접지 권장)
```

- 24 V 모터 전류는 브레드보드를 통과시키지 않고 굵은 배선과 정격 단자를 사용합니다.
- 모터별 퓨즈, 전류 제한, 역극성 보호와 회생전압 억제 방식을 정해야 합니다.
- BTS7960 제어 커넥터의 `VCC`는 5 V 논리 전원이며 24 V를 연결하지 않습니다.
- USB 연결 중 외부 5 V가 Teensy `VIN/VUSB`로 역급전되지 않도록 전원 경로를 확인합니다.
- Teensy 전원 (10/4 한건민 결정, `planned`, 실물 미검증): 별도 벅 컨버터 5 V를 만능기판을
  거쳐 Teensy `VIN`/`GND`에 연결하고, 뒷면 `VIN`–`VUSB` 연결 패턴을 절단해 USB 전원과
  분리합니다. Jetson USB는 통신에만 씁니다. 검증 순서는 절단 후 `VIN`–`VUSB` 도통 끊김
  확인 → 벅 출력 전압(Teensy 미연결) → Teensy 전원 → USB 통신이며, 분리 확인 전에는
  외부 5 V와 USB를 동시에 연결하지 않습니다.
  - 미정: 벅 입력을 E-stop 주접점 앞(제어 계통)에서 받는지. 주접점 뒤라면 E-stop 때
    Teensy도 꺼져 E-stop 상태를 보고하지 못합니다.
  - 영향: USB를 뽑거나 Jetson이 재시작해도 Teensy는 꺼지지 않습니다. 통신 두절 동작
    (프로토콜 §9, `T_comm` 미확정)이 실기에서 실제로 작동하므로 별도로 시험합니다.
    Jetson은 세션 시작 seq를 무작위로 두어 직전 세션 seq와 겹치지 않게 합니다(§4.1 중복 차단).
- 출력 EN에는 리셋 중 기본 LOW가 되는 하드웨어 바이어스를 둡니다.
- E-stop 주접점은 Teensy/USB와 무관하게 위험한 구동 에너지를 차단합니다. 핀 6에는
  0~3.3 V 보조접점 상태만 넣습니다. E-stop 시 음압 전원 유지 범위는 별도 확정 사항입니다.

즉시 배선 공통 규칙:

- Teensy 입력은 3.3 V를 초과하면 안 됩니다.
- 5 V push-pull 출력은 레벨 변환 후 입력합니다.
- open-collector 출력은 외부 풀업 사양을 확인하고 3.3 V로 풀업합니다.
- 센서 출력이 3.3 V를 넘는다면 분압·버퍼·클램프를 포함한 전처리를 먼저 검증합니다.

---

## 3. 음압부

### 3.1 압력 센서

| 항목 | 값 | 상태 |
|---|---|---|
| 핀 | `A8 / 22` | 확정 |
| 센서 출력 후보 | 0.5~4.5 V | reference 기반, 실물 확인 필요 |
| 측정 범위 후보 | 2,500~700,000 Pa | reference 기반, 실물 확인 필요 |
| reference 변환식 | `P = 2500 + (V - 0.5) × (700000 - 2500) / 4.0` | 센서 원출력 전압 `V` 기준 |
| ADC 전처리 | 4.5 V를 3.3 V 이하로 낮추는 회로 | 분압비·회로 미확정 |

분압을 사용하면 Teensy가 읽은 ADC 전압을 센서 원출력 전압으로 복원한 뒤 reference
변환식에 넣어야 합니다. 단순 직렬저항과 RC 필터만으로는 정상상태 4.5 V를 3.3 V
이하로 제한할 수 없습니다.

### 3.2 음압 모터 드라이버 두 대

두 모터가 각각 `VSP/RUN/EN/ERR`와 공통 신호 GND를 사용합니다.
2026-09-08 사용자 결정으로 두 FG 출력을 모두 제외했습니다. FG 인터럽트·펄스
집계·RPM 환산을 하지 않으며, 측정하지 않는 RPM을 0으로 대신 보고하지 않습니다.

| 신호 | Teensy 방향 | 의미 | 실물 확인 사항 |
|---|---|---|---|
| `VSP` | 출력 | 0~5 V 또는 PWM 속도 지령; 현재 코드 10 kHz 유지 | 제품 자료의 주파수 범위 불일치 및 3.3 V 입력 여유 확인 |
| `RUN` | 입력 | 제공된 제품 설명상 LOW 동작 상태 출력 | HIGH 전압·구동 방식 확인 필요 |
| `EN` | 출력 | LOW blower 정지/브레이크 | 3.3 V 입력 여유·부팅 기본 LOW |
| `ERR` | 입력 | 제공된 제품 설명상 LOW 오류, HIGH 비활성 | HIGH 전압·실제 오류 시 차단 동작 확인 필요 |
| `FG` | 미연결 | 제품 설명상 5 V 속도 펄스, 현재 미사용 | Teensy 직결 금지; 분리·절연 유지 |
| `+5V` | 미연결 | 제품 설명상 외부 패널 전원 공급 출력, 최대 150 mA | Teensy VIN/3.3 V/GPIO에 연결하지 않음 |
| `GND` | 공통 기준 | 드라이버 신호 GND와 Teensy GND 연결 | 모터 주전류는 별도 굵은 전원 배선으로 복귀 |

사용자가 두 모터 모두 3.3 V 제어로 동작하는 것을 관찰했지만, 온도·노이즈·배선 길이
조건의 논리 여유는 아직 특성화하지 않았습니다.

**자료 불일치:** 이전 제공 핀 설명은 VSP PWM 10 kHz, 최근 제공 설명은
30 kHz~150 kHz를 명시합니다. 실물 모델/버전에 적용되는 범위를 확인해야 하며,
이번 FG 제외 작업에서는 기존 시험의 10 kHz 설정을 임의로 변경하지 않았습니다.

**사용자 관찰과 검증 범위 (2026-09-08):** FG 제외 전 단일 모터 Stage4 로그에서
10% / 15초 RUN 요청 수락, EN HIGH 및 RUN LOW, ERR HIGH/오류 잠금 없음,
중간 `OK,MOTOR_OFF` 이후 PWM 0/EN LOW/RUN HIGH가 확인됐고 사용자는 회전을
관찰했다고 보고했습니다. 이는 15초 자동 종료·고장 주입·양쪽 모터 동시 운전·정확한
RPM·압력·전류·입력 전압·보드 무손상을 검증한 결과가 아닙니다. FG 제외 후 펌웨어의
새 실물 시험도 아직 수행하지 않았습니다.

현재 수정 대상 시험 파일은 기존 경로
`test/vacuum_motor_stage4_fg/vacuum_motor_stage4_fg.ino`를 유지합니다. 이름에
`fg`가 남아 있어도 기능은 제거됐으며, 새 시작 메시지는
`SAVER_VACUUM_MOTOR_STAGE4_NO_FG_READY,build=no_fg_v1`입니다. ERR LOW 즉시
정지/잠금, HIGH 250 ms 유지 후 수동 `CLEAR_ERR`, USB 단절 및 시간제한 정지는
유지합니다. FG 삭제가 ERR를 무시하거나 모터를 자동 시작하게 만들지는 않습니다.
`teensy/reference/`의 ESP32 코드는 과거 참고 자료이며 현재 FG 배선 기준이 아닙니다.

### 3.3 확정 임계값

| 항목 | 값 |
|---|---:|
| 목표 음압 `VAC_TARGET_KPA` | `-22.5 kPa` |
| 허용오차 `VAC_TOL_KPA` | `2.5 kPa` |
| 시작 최소값 `VAC_MIN_START_KPA` | `-15.0 kPa` |
| 중단 임계 `VAC_ABORT_KPA` | `-10.0 kPa` |

설정값 출처는 `docs/PROJECT_STATE.md` §2와 `docs/DECISIONS.md` D-23~D-27입니다.
값의 확정과 제어 구현·실물 검증은 별개입니다.

### 3.4 설계 근거와 미검증 조건

| 항목 | 설계 기록 | 주장 수준·확인 사항 |
|---|---|---|
| 블로워 팬 커브 | BL6561-P, 24 V. dead-head 약 27.3 kPa, 최소 연속 운전 유량 400 L/min | `assumed`: 데이터시트 원문과 실물 모델 일치 확인 전, D-27 |
| 흡착 패드 | 300 × 300 mm ×2, 명목 면적 0.18 m², 챔버 높이 20 mm | `planned`: 설계 치수이며 실제 유효 면적은 1:1 CAD로 확인 |
| 이전 ESP32 설정 | −30 kPa ±0.5 kPa, 재시도 3회/500 ms | 폐기 이력, D-23. 현재 설정이나 실기 운전 근거로 사용하지 않음 |
| 간헐 운전 정책 | 목표 도달 후 정지·압력 저하 시 재기동 및 미도달 폴백 | `planned` / `assumed`: D-27의 조건부 안. 역류 방지·허용 운전 시간·실기 검증 없이 시험 코드에 적용된 것으로 보지 않음 |

---

## 4. 압박부

| 항목 | 값 | 상태 |
|---|---|---|
| 모터 | BL60120-2430 BLDC (Ke = 4.45 V/kRPM) | 기존 확정값 |
| 드라이버 인터페이스 | `Serial7`, RX `28`, TX `29` | 핀만 반영 |
| UART baud/명령 형식 | — | 미확정, 송신 금지 |
| 감속 | 타이밍 벨트 1.5:1 | 기존 확정값 |
| 이송 | 볼스크류 리드 10 mm | 기존 확정값 |
| 목표 속도 | 110 cpm | 확정 |
| 목표 깊이 | — | **미확정 · 한성윤** |
| 힘 한계 | 구조 설계 하중 600 N (FEA). 환자 힘 상한 `force_max_N` 은 **미확정 · 한성윤** | 구조 하중은 `simulated`; 환자 상한과 구분 |
| 과전류 임계 | 12.0 A (초회 구동 6.0 A) | 확정 (2026-08-03, 정격 16 A 이하) |

압박 드라이버 TX가 5 V이면 Teensy RX7 앞에 레벨 변환이 필요합니다. UART 정지 명령이
확정되기 전에는 펌웨어의 `motorsOff()`가 압박 모터를 소프트웨어로 정지시킨다고 볼 수
없으므로 압박 모터 전원을 분리합니다.

압박 엔코더 `30/31`은 예약이며 리니어 `A16`, 전류 `A17`, 상·하 리미트 `3/4`를 유지합니다.
사용자 확인상 모터 Hall은 VESC에만 연결하며 Teensy `39`는 미연결 예비입니다.
로드셀은 DYMH-103 + NAU7802의 독립 Wire1 `17/16`을 사용합니다. `A9/23`은 미연결
예비이며 원시 `SG+/SG-`를 직결하지 않습니다. 핀 배정과 메인 취득 구현은 별개입니다.

---

## 5. 냉각

| 항목 | 구성 | 상태 |
|---|---|---|
| 온도 센서 | 2선식 NTC, 분압 중간점 `A0 / 14` | 핀만 반영 |
| 팬 | 5 V 4핀 PWM 팬 2대 | 실물 동작 미검증 |
| 팬 1 | PWM `5`, TACH `38` | 핀 확정 |
| 팬 2 | PWM `2`, TACH `9` | 핀 확정 |
| 팬 가동 온도 | `TEMP_FAN_ON_C = 40.0 °C` | 확정 |
| 정지 온도 | `TEMP_STOP_C = 70.0 °C` | 확정 |
| 팬 제어 주체 | Teensy 자율 또는 Jetson 요청 | 미확정 |

NTC의 `R25`, B값, 고정저항과 분압 방향이 확정되기 전에는 `temp_C`를 만들지 않고
측정 불가로 생략합니다. 팬 PWM 입력과 TACH 출력의 pull-up 전압·open-collector 여부도
모델별 데이터시트 또는 실측으로 확인합니다. 팬 전원은 GPIO에서 공급하지 않습니다.

`protocol/teensy_usb_protocol_v1.md`의 DS18B20 표현은 공동 소유 명세 변경이므로
신상현과 합의한 뒤 별도 수정합니다.

---

## 6. 구동계 — 24 V DC 모터 2대와 BTS7960 2대

현재 확인된 하드웨어는 24 V 감속 DC 모터 2대이며, 각 모터에 BTS7960 H-bridge
모듈 한 대를 사용합니다. 핀은 예약했지만 프로토콜에 구동계 명령이 없으므로 펌웨어가
모터를 움직이는 기능은 구현하지 않습니다.

### 6.1 BTS7960 제어 커넥터

| 모듈 핀 | 기능 | 모터 1 | 모터 2 |
|---:|---|---:|---:|
| 1 | RPWM | Teensy `7` | Teensy `0` |
| 2 | LPWM | Teensy `8` | Teensy `1` |
| 3 | R_EN | Teensy `15` | Teensy `10` |
| 4 | L_EN | 해당 모듈 R_EN과 묶음 | 해당 모듈 R_EN과 묶음 |
| 5 | R_IS | 미연결 | 미연결 |
| 6 | L_IS | 미연결 | 미연결 |
| 7 | VCC | 정전압 5 V | 정전압 5 V |
| 8 | GND | 공통 GND | 공통 GND |

두 모듈의 EN을 서로 묶지 않습니다. 모듈 1의 R_EN/L_EN만 핀 15에 묶고, 모듈 2의
R_EN/L_EN만 핀 10에 묶어 독립적으로 정지할 수 있게 합니다.

제공된 모듈 사진에는 5 V `74HC244D`가 있어 Teensy 3.3 V HIGH를 충분한 여유로
인식한다고 보장할 수 없습니다. `74HCT244` 또는 `74AHCT244` 같은 3.3→5 V 논리
버퍼를 사용하고 입력에는 기본 LOW 바이어스를 둡니다.

### 6.2 구동 모터 엔코더

> **주의 (2026-10-04, 신상현 기록):** 아래 표의 선 색은 판매 페이지 설명이며 **실물과 다릅니다.**
> 실물 모터의 권선은 **노랑–하양**입니다(한건민 시험 보고). 빨강–검정 사이는 6.57 kΩ로 권선이 아니고,
> 여기에 24 V를 직결했을 때 약 0.05 A만 흐르며 엔코더가 뜨거워졌으므로 엔코더 전원으로 추정합니다.
> 이 사고로 **주행 모터 1의 엔코더(11/12)가 고장** 났고, 모터 1 본체와 모터 2(본체, 엔코더)는 정상입니다
> (`docs/DECISIONS.md` D-36). 엔코더 A/B와 전원의 실제 색 배정은 아직 실측하지 않았습니다.
> 새 모터를 포함해 **색으로 판단하지 말고**, 전원을 넣기 전에 저항으로 권선을 먼저 찾습니다
> (사양 24 V, 정지전류 5.2 A에서 계산한 권선 저항은 약 5 Ω). 엔코더 전원은 5 V만 씁니다.

| 모터선 | 의미 | 모터 1 | 모터 2 | 주의 |
|---|---|---:|---:|---|
| Red/Black | 24 V 모터 권선 | BTS7960 M+/M- | BTS7960 M+/M- | Teensy 연결 금지 |
| Blue | 엔코더 전원 | 검증된 5 V | 검증된 5 V | 24 V 연결 금지 |
| Green | 엔코더 GND | 공통 GND | 공통 GND | — |
| Yellow | Encoder A | Teensy `11` | Teensy `20` | 출력 전압 확인 |
| White | Encoder B | Teensy `12` | Teensy `21` | 출력 전압 확인 |

엔코더 출력이 5 V push-pull이면 레벨 변환하고, open-collector이면 3.3 V pull-up을
사용합니다. 회전당 11펄스 표기가 채널당인지 quadrature edge 기준인지 실측해야 합니다.

구동계 명령과 상태 표현은 프로토콜 공동 결정 사항입니다. 신상현과 합의하기 전에는
`protocol/`을 변경하지 않습니다.

---

## 7. 그 밖의 센서

| 센서 | 연결 | 담당 | 상태 |
|---|---|---|---|
| PPG (MAX30102) | I²C `18/19`, INT 미사용 | Teensy | 핀 반영, 실물값 미검증 |
| 로드셀 (DYMH-103 + NAU7802) | 독립 Wire1 SDA`17` / SCL`16`, DRDY 미사용 | Teensy | 반력 기록 시험 버스 변경; 메인 취득 미구현 |
| 자이로 (WT901C-TTL) | Serial8 RX`34` / TX`35` | Teensy | 핀 예약, 메인 드라이버 미구현 |
| MR60BHA2 레이더 | UART → Jetson | Jetson | 연결 형태 미확정 |
| Raspberry Pi Camera V2.1 | CSI → Jetson | Jetson | 미착수 |

MAX30102 모듈의 버스 pull-up 선택 패드는 3.3 V로 설정합니다. 모듈 `VIN` 허용범위와
내장 레귤레이터 유무는 해당 모듈 자료를 따르며 SDA/SCL에는 3.3 V를 넘기지 않습니다.

---

## 8. Jetson Nano

| 항목 | 값 |
|---|---|
| Teensy 연결 | 네이티브 USB Serial (`/dev/ttyACM*`) |
| JetPack | 실물에서 확인 필요 |
| Ubuntu | 실물에서 확인 필요 |
| Python | 실물에서 확인 필요 |
| 전원 | 기존 계획: 5 V 4 A 배럴잭 + J48 점퍼; 현 구성에서 재검증 필요 |
| 터치스크린 | 7인치 1024×600 IPS |
| Chromium | 112.0.5615.49 (기존 확인값) |

Jetson과 Teensy의 USB 통신은 핀 0/1의 Serial1과 별개입니다. 구동 모터 2가 핀 0/1을
PWM으로 사용해도 네이티브 USB Serial 자체는 영향을 받지 않습니다.
