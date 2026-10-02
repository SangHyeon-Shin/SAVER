# 이전 commissioning_v2 설명 보관본 — 현재 코드 사용법이 아님

전류 PID 전환 전 비교용 보관본입니다. 아래의 ERPM 제어, 고정 CALIBRATION, ZERO/CAL_RESET 의미와
50mm/110CPM 기본값은 현재 코드에 적용하지 않습니다. 현재 설명은 `README.md`를 읽으세요.

Arduino IDE에서 **`compression_linear_pid_test.ino`**를 엽니다. 보드 **Teensy 4.1**, USB Type
**Serial**. 같은 폴더의 `.h` 파일을 함께 둡니다. `.ino`가 실행 스케치이고 `.h`는 설정·제어·통신을
분리한 C++ 보조 코드입니다. 별도 라이브러리가 필요하지 않습니다.

**현재 상태: 컴파일 가능한 구동 잠금 버전. 실제 50mm·110회/분 왕복 성공을 검증한 코드가 아닙니다.**
센서 원시값과 명목 거리 표시, VESC 값 조회, 모터에 RPM을 보내지 않는 목표 궤적 미리보기를
먼저 할 수 있습니다. 잠금을 제거할 조건이 갖춰지면 같은 코드의 PID/RPM 경로를 사용합니다.

고정·방호된 **무부하 연구용 시험대 전용**입니다. 사람, 마네킹, 스프링 압박이나 임상 용도로
사용하지 않습니다. **2026-09-16 사용자가 모터 단독 구동 성공을 보고했습니다.** 이는 통합 코드의
정역회전 ERPM 제어·위치 추종·보호 동작을 확인한 결과가 아닙니다.
볼스크류와 이동부 자체의 무게·관성은 여전히 부하입니다. 특히 수직 축은 정지 지령 후에도
자중으로 내려올 수 있어, 기계적 지지/포획 장치가 필요합니다.

## 이번 사용자 확인 사항

| 항목 | 반영 / 미확인 구분 |
|---|---|
| 리니어 센서 | 사용자: YwRobot ELB030638, 75mm 슬라이드 포텐셔미터 10kΩ, 0~VCC 이중 출력. 3.3V 전원 사용 계획; 핀 순서·방향·실측 보정은 미확인 |
| 감속비 | 사용자 정정: **모터 1.5회전 = 스크류 1회전**, 1.5:1 감속 |
| 볼스크류 리드 | 제공 자료: 10mm/rev |
| 목표 | 편도 50mm, 완전한 왕복 110회/분. 의료 설정 확정이 아닌 이 시험의 요청값 |
| 모터 극쌍수 | 제공 자료: 4. 실제 모터/설정 확인 필요 |
| 시험 환경 | 사람·마네킹·스프링 없이 고정 시험대. 오늘 모터·볼스크류·센서를 기구적으로 연결할 계획 |
| 리미트 | 사용자: 상·하단 **모두 없음**. 설치 확인 전 구동 불가 |
| E-stop | 사용자 정정: **전용 E-stop이 아니라 파워서플라이 전원 스위치**. 독립 비상정지 회로·보조접점 검증 없음 |
| VESC 설정/전원 | 전원장치 정격 최대 10A. Current Limits·역회전·회생·timeout 검증 미완료. 공급장치 정격은 모터/입력 전류 설정값과 다름 |
| 시작점 | 물리적인 센서 끝에서 여유를 두고 작업 시작점을 상대적 0mm로 잡기로 함. 여유 거리·ADC 두 점은 미측정 |

기존 `test/test.ino`, 압박 단독 시험, 리니어 단독 시험, `teensy/`, `docs/`, `protocol/`을
수정하지 않았습니다. 이 스케치의 USB 명령은 사람이 쓰는 벤치 콘솔이며 **Jetson JSON
프로토콜과 호환되지 않습니다**. Jetson 브리지에 이 스케치를 운용 펌웨어로 연결하지 않습니다.

## 연결

USB와 24V를 모두 끈 뒤 배선합니다. 다른 모터/드라이버의 전원·제어 연결은 분리합니다.
이 스케치는 음압·팬·BTS7960 출력이나 EN을 초기화하지 않습니다.

| 장치 신호 | Teensy 핀 | 이유 / 조건 |
|---|---|---|
| VESC COMM TX/SCL | **28 / RX7** | 기존 압박 UART 수신 핀 |
| VESC COMM RX/SDA | **29 / TX7** | 기존 UART 송신 핀; TX/RX 교차 |
| VESC COMM GND | GND | 비절연 UART 공통 기준; 모터 전류 귀환선과 분리 |
| VESC COMM 5V / 3.3V | **미연결·절연** | Teensy 전원과 묶지 않음 |
| 리니어 센서 전처리 OUT | **A16 / 물리 40** | 기존 센서 ADC, 디지털 16번이 아님 |
| 센서 신호 GND | GND | ADC 공통 기준 |
| 센서 전원 | 사용자 제공 0~VCC 포텐셔미터 모듈은 3.3V 기준 | 실물 VCC/GND/OUT 라벨 확인; 5V 공급/5V 출력 직결 금지 |
| 상단 리미트 | **3**, 현재 미설치 | 보호 입력 예약, 설치/극성 검증 필요 |
| 하단 리미트 | **4**, 현재 미설치 | 보호 입력 예약, 설치/극성 검증 필요 |
| E-stop 보조접점 | **6**, 현재 없음 | 파워서플라이 스위치를 이 입력/전용 차단 회로로 간주하지 않음 |

VESC는 이전에 제공한 사진의 **5번 COMM** UART를 사용합니다. 모터 3상과 Hall은 VESC에
연결하고 Teensy 30/31/39로 옮기지 않습니다. 실제 단자 라벨과 UART 출력 전압을 확인합니다.
**Teensy GPIO/ADC는 3.3V 전용이며 5V tolerant가 아닙니다.** 전원/회생 전류는 브레드보드나
Teensy의 GND 가는 배선을 통과시키지 않습니다.
[PJRC 보드 사양](https://www.pjrc.com/store/teensy41.html)

리미트/E-stop은 검증 플래그가 설정된 후 INPUT_PULLUP으로 읽습니다. 접점이 정상 닫힘으로
GND에 연결되고 단선/작동 때 HIGH가 되는 회로를 검토할 수 있지만, **실제 회로 없이 극성을
추측하지 않습니다**. 사용 회로에 맞춰 바이어스와 활성 레벨을 확인해야 합니다. 내부 풀업은
과전압 보호나 접점/배선 안전성 검증을 대신하지 않습니다. 현재 미연결 입력은 정상 false로
보고하지 않고 `inputs_ok=0`으로 보고합니다.

## 제어 방식

1. Teensy에서 5ms 간격(200Hz)으로 12-bit ADC를 읽고, 검증된 두 점으로 위치를 mm 환산합니다.
2. 목표 깊이를 계단처럼 갑자기 명령하지 않고 대칭 코사인 왕복 기준 궤적을 생성합니다.
3. 위치 PID가 속도 보정량을 계산하고, 기준 궤적의 속도에 더합니다.
4. 속도·가속도 상한과 적분 포화 방지를 적용한 뒤, 검증된 방향으로 signed ERPM을 보냅니다.
5. VESC가 내부 속도/전류 제어를 수행합니다. Teensy가 VESC의 전류 설정을 변경하지 않습니다.

`D`=편도 깊이, `f`=왕복/초, `x0`=수동 기준 위치:

```text
x_ref(t) = x0 + D/2 × (1 − cos(2πft))
v_ref(t) = Dπf × sin(2πft)
error    = x_ref − measured_mm
v_cmd    = v_ref + Kp × error + integral − Kd × measured_velocity
ERPM     = v_cmd[mm/s] × 60 × 1.5 / 10 × 4 × direction_sign
```

상승/하강 끝점에 유지시간은 없습니다. `RUN 1`은 내려갔다 돌아오는 **한 왕복**을 뜻합니다.
지정 사이클 시간이 끝나면 0A 정지 요청을 합니다. 실제 위치가 정확히 돌아왔다는 뜻이 아니며,
최종 위치도 추종 오차 한계를 넘으면 `END_POSITION_ERROR`로 정지·잠금 처리합니다.
`PROFILE_ENDED`에도 `tracking_success_verified=0`을 명시합니다. 자동 원점 복귀·하중 유지 기능은
없습니다. 실제 목표 추종 여부는 독립 거리계/위치 로그와 시간 측정으로 확인해야 합니다.

제어 계산 안에는 USB 출력/UART 송신을 넣지 않았습니다. USB 요약 출력은 20Hz이며 수신이 느리면
로그를 버립니다. 지령 UART에 공간이 없으면 새 RPM 요청을 중단하고 정지 요청을 우선합니다.

### 감속비 정정 후 계산

50mm·110CPM·리드 10mm·감속비 1.5·극쌍 4를 가정한 **정지 구간 없는 코사인 기준값**입니다.

| 항목 | 계산값 |
|---|---:|
| 왕복 주기 / 편도 시간 | 545.45ms / 272.73ms |
| 모터 편도 회전량 | 7.5회전 |
| 모터 평균 절대 RPM / ERPM | 1,650 / 6,600 |
| 모터 피크 RPM / ERPM | 약 2,592 / 10,367 |
| 선속도 피크 | 약 287.98mm/s |
| 선가속도 피크 | 약 3,317.28mm/s² |

이는 속도 요구량이지 달성 보증이나 권장 VESC 한계값이 아닙니다. 위치 보정에는 추가 여유가
필요합니다. VESC 자체 속도 ramp/minimum ERPM, 역회전·Hall 설정과 전원/회생 조건에 따라
끝점 추종이 달라집니다. 이전 증속 계산의 733RPM·2,933ERPM·Hall80카운트는 여기 적용하지 않습니다.
Hall을 모터 1회전당 24개 전이로 센다는 추가 가정이면 50mm에 180개, 약 0.278mm/전이입니다.
**이 스케치는 Hall 카운트로 위치를 정하지 않습니다.**

## 지금 가능한 사용

모터 주전원/축 구동은 분리하고 먼저 센서·코드 표시부터 확인합니다. 시리얼 모니터 115200,
New Line 또는 Both NL & CR. 부팅은 절대 자동 구동하지 않습니다.

```text
STATUS
CHECK
TARGET 50 110
PREVIEW 3
STOP
```

- `STATUS`: 잠금 이유, 센서 raw/명목 mm, VESC 응답, 확인 플래그와 진단 카운터.
- `CHECK`: 구동 허가에 쓰는 **동일한 14개 설정 검사**를 한 줄씩 표시. `passed=0`이 미충족이며
  보고만 하고 설정·ARM·구동을 바꾸지 않습니다. 송신 큐가 차면 기다리지 않고 다음 주기에 재시도합니다.
- `TARGET depth_mm cpm`: 멈춘 상태에서 목표 변경. 0보다 크고 50mm/110CPM 이하. ARM 해제.
- `PREVIEW [cycles]`: 실제 PID 구동 없이 가상 원점 0mm의 기준 궤적만 생성. 기본 1회.
  센서 실측값을 생성/위조하지 않습니다. 설정 잠금 중에도 가능하고 RPM 지령은 보내지 않습니다.
- `STOP` 또는 `DISARM`: 미리보기/구동 중단·ARM 해제·0A 요청. 데이터 읽기는 계속됩니다.
- `HELP`: 명령 목록.

미리보기/잠금/대기 상태에도 VESC 버전·값 조회 및 **0A 요청**은 송신합니다. 완전한 UART 무송신은
아닙니다. VESC가 다른 소프트웨어로 운전 중인 상태에서 이 스케치를 연결하지 않습니다.

### 오늘 먼저 할 센서 보정값 수집 (`commissioning_v2`)

**현재 코드는 자동 왕복 구동을 허용하지 않습니다.** 상·하단 리미트가 없고 파워서플라이 스위치만
있으며 센서 보정·제어/전기적 한계값도 미확인입니다. 단독 모터가 돌았다는 사실로 잠금을 해제하지
않습니다. 파워서플라이를 끄는 조작을 전용 물리 E-stop 또는 수직 축 유지 기능으로 보지 않습니다.

1. **모터 주전원을 분리하고 기구를 움직이지 않도록 지지**합니다. 수직 부품의 낙하/끼임을 먼저
   방지합니다. 가능하면 아직 기구에 연결하지 않은 센서만 별도로 측정합니다. USB로 Teensy를 켜고
   리니어 센서의 정상 전압 범위를 확인합니다. 이 절차에서 VESC 값이 없어도 ADC 수집은 가능합니다.
   전원을 끄기 직전의 VESC 응답이 남아 있으면 통신 만료로 첫 수집이 취소될 수 있습니다. 이때는
   `CAPTURE_CANCELLED`/`CAPTURE_STATUS`를 확인하고 연결 상태가 안정된 뒤 다시 수집합니다.
2. 센서를 자 등으로 확인한 첫 위치에 고정합니다. 압박 방향으로 증가하는 **센서 보정 좌표**를
   사용합니다. `CAPTURE 실제위치_mm`를 한 줄로 입력하고 약 1초 동안 움직이지 않습니다.
   예: 실제로 10mm 위치에 놓았을 때만 `CAPTURE 10`을 입력합니다. 숫자는 이동 지령이 아닙니다.
3. `CAL_POINT` 출력 뒤 두 번째 실제 위치에서 같은 작업을 합니다. 두 점 사이에 앞으로 사용할
   전체 작업 구간이 들어가도록 측정하며, 출력 rail/물리적 끝을 작업 목표로 쓰지 않습니다.
4. `CAL_SHOW`를 입력하고 `CAL_POINT` 두 줄과 `CAL_CANDIDATE` 한 줄을 보냅니다. 중간 위치도
   별도로 거리/ADC를 확인해 선형성·방향을 검토해야 합니다. 두 점만으로 정확도가 검증되지는 않습니다.

`CAPTURE`는 정상적인 200Hz 샘플링에서 약 1초간 200개 ADC의 평균·최소·최대와 rail 이력을
수집합니다. 2초 안에 완료되지 않으면 실패합니다. 제어 루프에서 USB 출력이나 대기를 하지 않습니다.
명령은 ARM과 기존 작업 원점을 무효화하고 0A 요청을 먼저 보냅니다. **실제 전원 분리 여부를 코드가
알 수 있는 것은 아니므로 작업자가 먼저 분리해야 합니다.** STOP·오류·USB 단절은 진행 중 수집도 취소합니다.

- `CAPTURE measured_mm`: 0~75mm 범위의 실제 측정 위치에 ADC를 대응시킴. 한 번에 한 점 수집.
- `CAL_SHOW`: 마지막 두 수집점 및 검토 후보를 표시. 두 점의 raw/거리가 같거나 raw 범위가 겹치거나
  rail/잘못된 ADC가 있었으면 후보를 거부합니다. 넓은 잡음·움직임의 허용 범위는 임의로 정하지 않고
  min/max를 그대로 보고하며 `quality_verified=0`입니다. `available=1`은 정확도/방향 검증이 아닙니다.
- `CAL_RESET`: 수집 기록만 삭제하고 정지/ARM 해제. 실제 제어 보정 상수는 바꾸지 않습니다.
- 세 번째 `CAPTURE`부터 slot 1/2를 번갈아 덮어씁니다. 실패/취소한 새 수집의 slot은 비워지므로
  이전 값을 새 값으로 오해하지 않습니다. 기록은 RAM에만 있으며 전원 재시작·USB 재연결 시 지웁니다.
  USB 연결을 유지한 채 센서 배선을 바꿨다면 먼저 `CAL_RESET`합니다. USB 출력이 유실되면 `CAL_SHOW`를
  다시 보내 저장된 수집 결과를 확인합니다.

**보정 후보는 자동 적용하지 않습니다 (`applied=0`).** `bench_config.h`의 `CALIBRATION`,
PID·안전 입력·전류 설정은 그대로입니다. 수집 성공만으로 `position_ok=1`이나 구동 가능 상태가 되지
않습니다. 구동용 `ZERO`와 이번 측정점 좌표는 다른 개념입니다. 실제 보정과 정지 속도 기준이 설정되기
전에는 `ZERO`가 거부되는 것이 정상입니다. 원시 ADC와 `nominal_mm`는 계속 확인할 수 있습니다.

단독 시험 파일에는 현재 듀티 상한 50%, 기본 10%, 5초가 들어 있고 일부 주석은 5%로 남아 있습니다.
이번에는 그 파일을 수정하거나 그 듀티를 양방향 PID의 ERPM/전류 한계로 복사하지 않았습니다.

### 센서 값의 의미

```text
adc_V      = adc_raw × 3.3 / 4095
nominal_mm = adc_raw × 75 / 4095     (아직 명목값, 방향 미확인)
position_mm = mm0 + (adc_raw − raw0) × (mm1 − mm0) / (raw1 − raw0)
depth_mm    = position_mm − manual_zero_mm
```

명목 환산은 0V=0mm, 3.3V=75mm로 **표시만** 합니다. 실물에서 반대라면 두 점 보정의 raw 순서를
뒤집으면 됩니다. 실제 압박 방향으로 mm가 증가하게 정합니다. `position_ok=0`일 때 명목값을
검증된 깊이로 사용하지 않습니다. 보정 미확인/rail/보정 범위 밖에서는 `position_mm`·`depth_mm`을
생략하며 0으로 대체하지 않습니다.

ADC 한 코드의 이론적 간격 약 0.0183mm는 정확도가 아닙니다. raw 0/1 또는 4094/4095는 제어에서
거부합니다. 센서 전압 끝점과 단락/단선은 구별되지 않을 수 있습니다. 중간 전압에 고정되거나
미연결로 떠 있는 ADC도 정상처럼 보일 수 있어 독립 리미트/배선 보호가 필수입니다.

## 구동을 열기 전에 필요한 값

**`bench_config.h`** 한 파일에 모았습니다. 확인되지 않은 값은 0/false/-1이며 그 상태에서 ARM/RUN을
거부하는 것이 정상입니다. 오류 메시지를 없애기 위해 임의 숫자나 true를 넣지 않습니다.

| 설정 묶음 | 필요한 확인 |
|---|---|
| `CALIBRATION` | 두 실제 위치(mm)/ADC 측정점, 압박 방향. 보정 구간 밖으로는 외삽하지 않음 |
| 리미트/E-stop 플래그·활성 레벨 | 실제 스위치 설치, 보호 회로·보조접점, 작동/단선 시 반응 |
| `MOTOR_DIRECTION_VERIFIED`, 방향 부호 | 단독 회전·낮은 속도 이동으로 VESC 부호와 mm 증가 방향 확인 |
| `VESC_SETTINGS_AND_TIMEOUT_VERIFIED` | 모터/극쌍/Hall·UART·전류/전압·회생·속도·timeout 설정 및 단절 반응 |
| `SENSOR_WIRING_AND_FAULT_TEST_VERIFIED` | 출력 전압, 단선·단락·위치 이상, 포화 시 거부 확인 |
| soft min/max·시작 오차·정지 속도 범위·추종 오차·속도/노이즈 한계 | 실제 기계 가동 범위 안에서 정지거리 여유를 두고 검증 |
| `PID`, `MAX_COMMAND_ERPM` | 낮은 속도/작은 이동부터 PID 및 제한을 검증. 최종 110CPM 즉시 시험하지 않음 |
| `MAX_RUN_MS` | 유한한 시험 시간 상한. 현재 0=잠금, 최대 문법 한계 30초 |
| 전압·모터/전원 전류·FET 온도 한계 | 측정 의미/전원 사양에 맞춘 검토값. 0 상태에서는 구동 불가 |
| `BENCH_DRIVE_CONFIRMED` | 모든 조건이 실제로 충족된 뒤 마지막으로 확인 |

PID 필드 순서는 `kp, ki, kd, maxVelocityMmS, maxAccelerationMmS2, integralLimitMmS`입니다.
Kp는 1/s, Ki는 1/s², Kd는 무차원이며 듀티 PID 게인을 그대로 넣으면 안 됩니다. 미분은 측정 위치에
적용하고, 적분이 속도/가속도 포화를 더 키우는 방향이면 누적을 막습니다. 미분 필터는 별도로
없으므로 센서 노이즈와 실제 ADC 지연을 포함해 튜닝해야 합니다. VESC 내부 속도 PID도 영향을 줍니다.

모든 준비가 끝난 뒤의 문법은 `CLEAR_ERR`(고장 있었을 때) → `ZERO` → `ARM` → `RUN 1` → `STOP`입니다.
`ZERO`는 사람이 안전한 시작 위치에 놓았다는 요청으로 현재 mm를 기준으로 잡을 뿐 **홈 센서를
검증하거나 모터를 움직이지 않습니다**. ZERO/ARM/RUN에는 측정 속도가 `MAX_START_VELOCITY_MM_S`
이하로 250ms 이상 안정되어 있어야 합니다. 실행 시작 위치가 ZERO에서 벗어나면 거부합니다.
전체 요청 경로가 두 점 보정으로 확인한 mm 구간 안에 있어야 하며, 그 밖으로 외삽하지 않습니다.
ARM은 10초 후 만료하며, 구동 중 TARGET/RUN/ARM/CLEAR 등 중복 명령은 정지·잠금 처리됩니다.

## 보호와 미검증 한계

- 위치 rail/보정 범위 이탈, 위치 점프·과도한 추종 오차, soft limit, 상/하 리미트, 물리 E-stop
  보조접점, VESC fault, 오래된 값, USB 단절, 손상된 UART/USB 명령에 정지 요청·로컬 오류 잠금.
- ADC/PID 갱신이 20ms 이상 늦어지면 구동 중단. 통신 값은 300ms 이상이면 오래된 것으로 처리.
  이는 벤치의 소프트웨어 deadline이며 의료 통신 지속시간 `T_comm`과 무관합니다.
- **0A 요청은 브레이크/위치 유지/하드웨어 EN LOW가 아닙니다.** 물리 E-stop은 독립 하드웨어로
  구동 에너지를 차단합니다. 축 관성과 자중, 회생 전압, UART 단선은 코드만으로 해결되지 않습니다.
- MCU 하드웨어 watchdog, 이중 위치센서, 힘 제어/로드셀 보호는 구현하지 않았습니다.
- VESC 전류/온도 응답은 20Hz 감독용이지 즉각적인 과전류 보호가 아닙니다. 전류 센서 위치·정의가
  확인되지 않아 저장소의 첫 구동 6A/정상12A를 VESC 설정에 자동 복사하지 않았습니다.
- Gemini의 Motor16A/Brake-16A/Absolute25A/Battery15A는 확인된 안전 설정이 아닙니다. 필요한
  전류는 질량·회전체 관성·마찰·힘·토크상수 정의·효율·온도 및 전원/회생 능력이 있어야 산정됩니다.
  기존 의료/안전 임계값과 VESC 설정을 변경하지 않았습니다.
- VESC 조회 응답은 RPM 명령 실행 ACK가 아닙니다. `drive_queued`는 Teensy 송신 큐에 넣은 횟수이며,
  실제 회전·거리·속도·정지를 증명하지 않습니다. 통신 회복 뒤 자동 재시작하지 않습니다.

## 소프트웨어 검증

저장소 루트에서 실행합니다. 기본 스크립트는 설치된 Teensy ARM C++ 컴파일러로 실제 헤더의
`constexpr`/`static_assert`를 평가합니다. PC 실행형 시험이나 실제 모터 시험과 구분합니다.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File test/compression_linear_pid_test/tests/check_depth_control.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File test/compression_linear_pid_test/tests/check_vesc_link.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File test/compression_linear_pid_test/tests/check_bench.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File test/compression_linear_pid_test/tests/check_calibration_capture.ps1
arduino-cli compile --fqbn teensy:avr:teensy41 --warnings all --build-property 'recipe.hooks.postbuild.1.pattern=' test/compression_linear_pid_test
```

2026-09-16 `commissioning_v2` 검사 결과:

- `check_depth_control.ps1` 71개 + `check_vesc_link.ps1` 36개 + `check_bench.ps1` 51개 +
  `check_calibration_capture.ps1` 34개 = **컴파일타임 검증 192개 통과**.
  핀맵·제어/통신 분리·수집 명령의 정지/원점 무효화 소스 검사도 통과했습니다.
- 위 네 스크립트를 `powershell.exe -NoProfile -ExecutionPolicy Bypass -File ...`로 실행했습니다.
  영구 실행 정책은 변경하지 않았습니다. 실행형 하드웨어 모의 시험이 아니라 ARM 컴파일러의
  `-std=c++14 -Wall -Wextra -Werror -fsyntax-only` 검사입니다.
- Arduino CLI로 `--fqbn teensy:avr:teensy41 --warnings all
  --build-property 'recipe.hooks.postbuild.1.pattern='
  --build-path C:\Users\Administrator\AppData\Local\Temp\saver-depth-pid-20260916` 옵션 컴파일 성공:
  **오류 0·경고 0**. 처음 제한 환경에서 Arduino15 접근이 거부됐고 접근 허용 후 재실행했습니다.
  Teensy Loader 실행 후처리는 끈 상태로 컴파일만 했습니다.
- 수정 C++/스케치에 clang-format 적용 후 `--dry-run --Werror` 통과. `git diff --check`와
  이 폴더 전체 후행 공백 검사 통과. 기존 다른 파일의 LF→CRLF 안내는 컴파일 경고가 아닙니다.
- 이 폴더의 스케치·설정·허가 검사·README·시험 파일만 변경했습니다. 성공한 단독 모터 코드,
  `test/test.ino`, `teensy/`, `docs/`, `protocol/`은 변경하지 않았습니다.
- **업로드·시리얼 포트 접속·실물 ADC 수집·모터 왕복·실제 정지 시험·커밋·푸시는 하지 않았습니다.**
  사용자 보고인 단독 구동 성공 외에 통합 하드웨어 동작을 검증했다고 주장하지 않습니다.

이전 2026-09-15 소프트웨어 검사 이력 (아래 기존 타 폴더 시험은 9/16에 재실행하지 않음):

- PID·보정·실제 코사인 기준함수 71개, VESC 프레임/해석 36개, 기본 잠금·명령/허가 44개:
  **컴파일타임 검증 총 151개 통과**. 새 핀 배정과 제어/통신 분리 소스 검사도 통과.
- Arduino CLI 1.5.1 / Teensy platform 1.62.0 / `teensy:avr:teensy41`:
  컴파일 성공, **최종 오류 0·경고 0**. Loader postbuild hook을 비활성화해 컴파일만 수행.
- 최초 제한 환경에서는 Arduino15 패키지 접근 거부로 컴파일을 시작하지 못했습니다.
  권한 허용 후 발견한 Arduino `PI` 매크로와의 이름 충돌은 `TRAJECTORY_PI`로 고쳤고 재검사했습니다.
- 새 C++ 파일 clang-format 적용/검사 및 공백 검사 통과. 기본 `git diff --check`에서도 공백 오류
  없음. 기존 변경 파일에 대한 Git의 LF→CRLF 안내는 컴파일 경고가 아닙니다.
- 기존 리니어 24개·음압 stage4 35개·MAX30102 21개 검증 통과. 압박 단독 로직의 51개 C++
  검증도 통과했지만 그 폴더의 기본 검사 전체는 **현재 듀티 10% / 검사 기대값 5% 불일치로 실패**.
- 기존 음압 통합 검사 `test/tests/check_combined.ps1`는 **실패**: 시간 제한 제거 작업의
  `CombinedState::start`는 4인자이나 기존 시험이 6인자를 전달합니다. 이 새 폴더는 해당 파일을
  포함하지 않으며, 이전의 미완성 변경은 이번 범위 밖이라 수정하지 않았습니다.
- `teensy/test_saver_proto.cpp`는 ARM 컴파일러 `-std=c++11 -Wall -Wextra -fsyntax-only`
  문법 검사만 통과. PC용 C++ 컴파일러가 없어 실행형 단위시험은 실행하지 못했습니다.

실제 Arduino CLI 실행 파일은 `C:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe`;
위 컴파일 옵션에 `--build-path C:\Users\Administrator\AppData\Local\Temp\saver-depth-pid-20260915`를
추가했습니다. 보조 컴파일러는 Arduino15의 `teensy-compile/15.2.1/arm/bin/arm-none-eabi-g++.exe`,
포맷 도구는 Arduino IDE의 `clang-format.exe`를 사용했습니다.

순수 로직의 검증과 `.ino` 컴파일은 `DepthBench::tick()`의 ADC/USB/UART 타이밍·상호작용을
종단간 실행한 시험이 아닙니다. 업로드·GPIO/ADC 실측·UART 실통신·모터
정역회전·50mm 깊이·110CPM·오차·정지거리·비상정지·전류/발열 시험은 수행하지 않았습니다.
이 코드의 검증을 기존 전체 저장소 검증이나 압박 기구 검증으로 확대해서 해석하지 않습니다.

## 근거

- [VESC 명령 ID](https://github.com/vedderb/bldc/blob/master/datatypes.h): RPM=8, current=6.
- [VESC 명령/값 처리](https://github.com/vedderb/bldc/blob/master/comm/commands.c): signed int32
  big-endian ERPM, 구동 명령에 실행 ACK 없음. FW 5/6 공통 values prefix만 처리.
- [VESC FOC 제어](https://github.com/vedderb/bldc/blob/master/motor/mcpwm_foc.c): 전기적 RPM과
  전류 0 / 속도 0의 서로 다른 경로. 실제 보드 펌웨어와 설정의 호환성은 확인 필요.
- [VESC timeout](https://github.com/vedderb/bldc/blob/master/timeout.c): timeout=0 비활성.
  다른 VESC Tool/PPM/ADC/CAN/Lisp/ALIVE가 경쟁 제어·timeout 갱신을 하지 않도록 분리.
