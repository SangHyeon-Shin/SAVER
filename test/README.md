# 음압 모터 + 압력 센서 통합 벤치 시험

Arduino IDE에서 **이 폴더의 `test.ino`**를 열고 보드는 Teensy 4.1, USB Type은
Serial로 선택합니다. 같은 폴더의 `bench_logic.h`, `pressure_conversion.h`도 필요합니다.
두 하위 폴더의 단독 시험 코드를 실행하는 것과 다릅니다. Jetson용 프로토콜도 아닙니다.

**고정된 연구용 시험대 전용입니다. 사람에게 사용하지 않습니다.** 오류·STOP·USB 단절 시
음압 모터를 끄므로 흡착으로 장치나 하중을 지탱하면 안 됩니다. 물리 정지 수단이 필요하며,
압박 제어, 물리 E-stop 입력 감시, 압력 자동 조절, 전류 제한, 하드웨어 watchdog은 없습니다.

**2026-09-11 사용자 요청: 이 통합 시험만 작동시간 제한을 제거했습니다.** `RUN` 후 수동
`STOP` 또는 보호 조건이 발생할 때까지 계속 구동합니다. 운전 중 자리를 비우지 마세요.
ERR 오류, 압력 측정 이상, USB 단절, 잘못된 명령에 따른 정지는 유지합니다.
센서값이 타당한 범위 안이면 목표 음압에 도달해도 자동으로 꺼지지 않습니다.
다른 음압/압박 모터 단독 시험의 작동시간 제한은 변경하지 않았습니다.

## 배선

배선 변경 전 USB와 24 V를 모두 끕니다. 모터 주전류는 브레드보드/Teensy를 통과시키지 않습니다.

**2026-10-02 핀 변경:** 모터 2 RUN은 34→32, EN은 35→37입니다.
`build=no_fg_pa_manual_v4_relative_zero` 업로드 전에 두 선을 함께 옮기세요.
34/35는 WT901C Serial8용으로 예약되며 이 시험은 자이로를 구동하지 않습니다.

| 신호 | 모터 1 | 모터 2 | 비고 |
|---|---:|---:|---|
| VSP | 24 | 33 | 기존 시험의 PWM 10 kHz 유지 |
| EN | 26 | 37 | LOW 정지/브레이크 지령 |
| ERR | 27 | 36 | 입력, LOW 오류·HIGH 비활성 |
| RUN | 25 | 32 | 입력, LOW 작동 상태 신호; 시작 명령선 아님 |
| FG / 모터 +5V 출력 | 미연결 | 미연결 | 각각 분리·절연, Teensy 전원에 연결 금지 |
| 신호 GND | Teensy GND | Teensy GND | 비절연 신호의 공통 기준 |

모터 1만 연결할 때는 `RUN1`만 사용합니다. 미연결 모터 2 입력은 의미가 없고
오류 잠금이 생길 수 있지만 모터 1 운전을 중단시키지는 않습니다. `RUN2/RUNBOTH` 전에
모터 2까지 실제로 연결·점검해야 합니다. 입력 핀의 HIGH만으로 연결 유무를 알아낼 수 없습니다.
ERR/RUN의 출력 전압·방식을 확인하고 **0~3.3 V로 변환한 신호**만 Teensy에 넣습니다.
내부 풀업으로 5 V 출력을 안전하게 만들 수 없습니다.

압력 센서는 사용자가 확인한 **XGZP6847A100KPGN, 5 V형, −100~0 kPa, 0.5~4.5 V 출력** 기준입니다.

```text
센서 OUT ── 4.7kΩ ──┬── Teensy A8 (디지털 번호 22)
                     └── 10kΩ ── GND
센서 VDD ── 검증된 정전압 5 V
센서 GND ── Teensy 신호 GND
```

센서 5 V 전원과 모터 커넥터의 +5V 출력은 별개입니다. VIN이라는 이름만 보고 5 V라고
가정하지 말고 센서 VDD–GND를 측정하세요. 3.3 V 전원에서는 이 환산식을 사용하지 않습니다.
분압의 4.5 V 입력은 약 3.061 V지만 **5 V 입력이면 약 3.401 V이므로 보호되지 않습니다.**
저항 실제 값·방향, A8 전압, 역급전을 전원 인가 전에 확인해야 합니다.

## Pa 환산

`pressure_conversion.h`에서 다음 명목식을 사용합니다.

```text
adc_V       = adc_raw × 3.3 / 4095
sensor_V    = adc_V × (4700 + 10000) / 10000
pressure_Pa = (sensor_V − 4.5) × 25000
```

대기압 기준 **게이지압**입니다. 4.5 V → 0 Pa, 3.5 V → −25,000 Pa,
0.5 V → −100,000 Pa입니다. 절대압 101,325 Pa를 더하지 않습니다.
사용자 로그 raw=3794는 이 명목식으로 약 −139 Pa입니다. 실측 정확도 인증이 아닙니다.

전원·ADC 기준은 각각 5 V/3.3 V, 저항은 4.7kΩ/10kΩ으로 가정합니다. 압력 교정·전원 보상은 없습니다.
제조사 ±2% FSS에 해당하는 출력 여유 0.08 V를 포함해 0.42~4.58 V를 전기적 타당성
검사에 사용합니다. 이 값은 의학적 압력 한계가 아닙니다.
작은 양수/음수 오프셋을 0으로 강제 보정하지 않으며, 명목 0.5~4.5 V 밖이면
`pressure_in_nominal_range=0`입니다. 소수점 표시는 그만큼 정확하다는 뜻이 아닙니다.

ADC rail·ADC 오류·전압 범위 이탈은 `pressure_ok=0`이고 **pressure_Pa 필드를 생략**합니다.
ARM/RUN을 거부하고, 운전 중 감지되면 모터를 끄고 DISARM합니다. 센서가 회복되어도
자동 재시작하지 않습니다. 범위 안에 우연히 들어온 단선·고정 출력·잘못된 전원은 ADC만으로
구별할 수 없으므로 `pressure_ok=1`은 연결·압력 정확도·안전성의 보증이 아닙니다.

근거: [CFSensor 데이터시트 V2.7, 2·4·5쪽](https://cfsensor.com/wp-content/uploads/2022/11/XGZP6847A-Pressure-Sensor-V2.7.pdf).
기존 `docs/hardware_mapping.md` 압력 절과 센서 단독 시험의 미확정 설명은 이번 범위에서
수정하지 않았습니다. 이 통합 시험의 센서 모델은 이후 사용자가 확인한 위 모델을 따릅니다.

## 현재 값을 표시용 영점으로 잡기 (`ZERO`)

**2026-10-15 implemented: 사용자 요청으로 수동 상대 영점만 추가했습니다.**
사용자 실측 센서 VDD–GND는 3.8 V로 보고되었으며, 5 V형의 허용 공급전압
4.75~5.25 V 밖입니다. 따라서 아래 출력은 **압력 정확도가 검증되지 않은 신호 변화 표시**이며,
정확한 게이지압·흡착력·합격 판정·안전 인터록의 근거로 사용하지 않습니다.
근거: [제조사 V2.8, 2쪽](https://cfsensor.com/wp-content/uploads/2026/09/XGZP6847A-Pressure-Sensor-V2.8.pdf).

업로드 후 같은 시리얼 연결에서 다음을 한 줄씩 보냅니다. 모터 구동 명령은 필요 없습니다.

```text
ZERO
PLOT
```

- `ZERO`: 먼저 두 모터 정지·DISARM, TEXT 모드로 바꾼 뒤 새 ADC 표본을 기준으로 잡습니다.
  예전 로그의 고정 오프셋이나 부팅 자동 영점을 사용하지 않습니다. 센서가 연결·고정된 상태에서
  보내며, 실제 대기압 영점과 비교하려면 압력 포트를 대기에 개방해야 합니다.
- `OK,DISPLAY_ZEROED,...,relative_only=1,pressure_calibrated=0` 응답 후
  `pressure_relative_Pa = pressure_Pa - zero_reference_Pa`를 추가 표시합니다.
  기준 순간은 0이며, 뒤의 잡음·드리프트를 0으로 강제하지 않습니다. 단위 환산도 명목식 그대로라
  저전압에서의 실제 압력 변화량과 일치한다고 보장하지 않습니다.
- `TEXT`/`READ`: 원래 `pressure_Pa`, `adc_raw`, 전압 추정값은 바꾸지 않고 상대값도 함께 기록합니다.
  `PLOT`은 영점이 있으면 `pressure_relative_Pa`, 없으면 기존 `pressure_Pa`를 그립니다.
- `UNZERO`: 두 모터 정지·DISARM 후 기준을 지우고 TEXT로 돌아갑니다.
- 재부팅·USB 연결 해제/재연결·측정 오류 시 기준은 삭제되며 다시 `ZERO`가 필요합니다.
  `ADC_RAIL`·전압 범위 오류·ERR 정지·ARM/RUN 조건은 그대로이고 영점으로 오류를 숨기지 않습니다.

소프트웨어 검증은 `tests/check_combined.ps1`과 아래 실제 스케치 모의 실행 시험으로 수행합니다.
실물 압력 교정·공급전압 복구·센서 손상 여부·모터 정지를 검증한 결과는 아닙니다.

저장소 루트에서 실행하는 검증 명령입니다. 실행 파일은 저장소 밖 임시 폴더에 만듭니다.
Windows에서 이번 네이티브 시험은 설치된 Zig의 `c++` 드라이버를 사용합니다.

```text
powershell.exe -NoProfile -ExecutionPolicy Bypass -File test/tests/check_combined.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File test/vacuum_motor_stage4_fg/tests/check_stage4.ps1
zig c++ -std=c++14 -Wall -Wextra -Werror -DARDUINO_TEENSY41 -I test/tests/stubs test/tests/test_display_zero.cpp -o <임시폴더>/test_display_zero.exe
<임시폴더>/test_display_zero.exe
zig c++ -std=c++11 -Wall teensy/test_saver_proto.cpp -o <임시폴더>/test_saver_proto.exe
<임시폴더>/test_saver_proto.exe
arduino-cli compile --fqbn teensy:avr:teensy41 --warnings all --build-property "recipe.hooks.postbuild.1.pattern=" --build-path <임시빌드폴더> test
python tools/consistency_audit.py
git diff --check
```

Teensy 컴파일의 Loader 후처리는 비워 업로드·포트 연결을 하지 않습니다.
주변의 원래 코드 형식은 보존하고 수정 구간·신규 C++ 파일에 `clang-format`을 적용합니다.

2026-10-15 검증 결과: 컴파일 타임 assertion 90개, 실제 스케치의 모의 ADC/GPIO/USB 실행
35개, 기존 Stage4 assertion 35개, 프로토콜 네이티브 시험 26개가 통과했습니다.
Teensy 4.1 빌드는 오류·경고 없이 완료했고 확정값 일관성 검사도 통과했습니다.
이는 소프트웨어 시험이며 실물 시험 수준의 `tested` 주장이 아닙니다.
Windows 네이티브 준비 중 Zig 내장 libunwind의 dllexport 재선언 경고가 발생했으나,
시험 소스의 `-Werror` 검사와 실행 결과에는 실패가 없었습니다.

## 실시간 그래프: Arduino IDE 2 시리얼 플로터

1. 배선을 검증한 후 사용자가 업로드합니다. 부팅은 모터 OFF / DISARM입니다.
2. 같은 포트를 쓰는 시리얼 모니터·브리지를 닫고 **도구 → 시리얼 플로터**를 엽니다.
3. 전송 설정은 115200, 줄바꿈(New Line 또는 Both NL & CR)입니다.
4. 플로터 전송 칸에서 `PLOT`을 보내면 압력을 100 ms 간격으로 표시합니다.
5. `pressure_Pa`만 체크하면 Pa 그래프가 보입니다. 나머지는 구동 지령/ARM/오류의 0·1 표시입니다.
6. 모터를 시험하려면 **동일한 플로터의 전송 칸에서** 아래 명령을 한 줄씩 보냅니다.

```text
ARM
RUN1 10
```

위 명령은 **10% 듀티, 수동 정지까지 계속 구동**하는 문법 예시입니다. 정격 전류의 10%나 1 A 설정이 아니며,
실물에 안전하다고 검증한 조건도 아닙니다. 수동 중단은 `STOP`, 구동 허가까지 해제는 `DISARM`입니다.
모터 정지 후에도 압력은 계속 읽어서 표시합니다. 플로터의 가로축은 수신 표본 순서이며,
일반 주기 표본은 0.1초 간격입니다. 명령 직후 추가 출력·로그 누락이 있으면 정확한 시간축은
아닙니다. 정확한 시각은 `TEXT` 모드의 `DATA,ms=...`를 사용하세요.

모니터에서 RUN한 뒤 플로터로 바꾸지 마세요. USB 포트 닫힘/DTR 해제로 정지·DISARM하고,
재연결은 TEXT 모드에서 새 명령을 기다립니다. 재연결 직후 준비 중에 보낸 바이트도 폐기합니다.
플로터의 일시정지 버튼은 화면 정지일 뿐 **모터 STOP 명령이 아닙니다.**

오류 시 PLOT을 종료하고 TEXT 오류 메시지를 보냅니다. **그래프가 멎으면 마지막 선을
현재 압력으로 보지 마세요.** `STOP`을 보내고, 모터 전원을 끈 후 시리얼 모니터에서
`STATUS`를 확인하세요. 오류에서 압력 0이나 이전 정상값을 계속 그리지 않습니다.

플로터 모드에서는 일반 텍스트 ACK 대신 `armed`, `m1_commanded`, `m2_commanded` 등
숫자 상태를 표시합니다. 정확한 거부 이유나 듀티는 `TEXT`/`STATUS`로 확인합니다.
잔여시간은 없으며 `remaining_ms` 대신 `run_mode=MANUAL_STOP`을 출력합니다.
회전 여부 실측이나 RPM은 아니며 FG는 사용하지 않습니다.

형식 근거: [Arduino Serial Plotter 공식 안내](https://docs.arduino.cc/software/ide-v2/tutorials/ide-v2-serial-plotter).

## 명령 요약

| 명령 | 동작 |
|---|---|
| `ARM` | 두 모터를 끈 뒤 구동 요청 허가; 압력 타당성 검사, ERR는 채널별 RUN에서 확인 |
| `RUN` / `RUN1 [pct]` | 모터 1, 기본 6%; 먼저 ARM 필요, 수동 정지까지 계속 |
| `RUN2 [pct]` | 모터 2, 기본 6%; 먼저 ARM 필요, 수동 정지까지 계속 |
| `RUNBOTH pct1 pct2` | 양쪽 조건 통과 후 함께 시작, 수동 정지까지 계속 |
| `SET` / `SET1 pct`, `SET2 pct` | 이미 구동 중인 채널 듀티 변경, 0이면 해당 채널 정지 |
| `STOP`, `STOP1`, `STOP2`, `DISARM` | 전체/개별 정지, DISARM은 구동 허가도 해제 |
| `CLEAR_ERR` / `CLEAR_ERR1`, `CLEAR_ERR2` | 해당 ERR가 HIGH 250 ms 유지되면 수동 잠금 해제; 자동 기동 없음 |
| `PLOT`, `TEXT` | 그래프 / 상세 로그 모드 |
| `READ`, `STATUS`, `HELP` | 즉시 샘플 / 진단(TEXT로 전환) / 도움말(TEXT로 전환) |

실행 중 RUN을 다시 보내면 멈추고 DISARM합니다. 계속 운전하려고 RUN을 반복할 필요는 없습니다.
이전 형식 `RUN1 10 15000` 또는 `RUNBOTH 10 10 15000`처럼 시간을 보내면
`RUN_TIME_ARGUMENT_REMOVED` 오류로 정지·DISARM합니다. 시간 인자를 무시하고 무기한 실행하지 않습니다.
잘못된 명령·범위 초과·부분 명령 1초 초과 시 전체 정지·DISARM합니다. 허용 범위는
듀티 1~100%이며 작동시간 설정은 없습니다. 1초 제한은 미완성 명령의 통신 처리용이고
모터 작동시간이 아닙니다. 종료 타이머를 없애도 전원 인가·USB 재연결·오류 해제 후 자동 시작하지 않습니다.
구동 중인 채널 ERR LOW는 전체 모터 정지·DISARM·해당 오류 잠금이며, STOP/ARM으로
잠금이 지워지지 않습니다. 원인 해소 뒤 해당 `CLEAR_ERR1/2`를 사용합니다.

## 검증 범위

`tests/check_combined.ps1`은 ARM/RUN/ERR/수동 정지/장시간 유지/파서·분압·Pa 식의 컴파일 타임 시험과
핀맵/소스 검사를 수행합니다. Arduino CLI 컴파일은 Teensy 4.1을 대상으로 합니다.
`bench_logic.h` 앞부분의 기존 Stage4 시간제한 클래스는 비교/회귀시험용으로 그대로 보존하며,
통합 시험의 실제 제어에는 작동시간 변수가 없는 별도 `ManualRunState`만 사용합니다.
그 자체로 실제 모터 정지·회전, 압력 정확도, 10 kHz 적합성, 전류, USB 단절 지연,
전압 레벨, 실제 시리얼 플로터 화면을 검증하지는 않습니다. 업로드·실물 구동은 별도입니다.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File test/tests/check_combined.ps1
```

### 이번 작업의 실행 결과 (2026-09-08)

- 선행 FG 제외 변경 7개 파일을 `75e158b` (`[teensy] 음압 FG 제외 및 벤치 안전 정지 보완`)로
  로컬 커밋했습니다. Push하지 않았으며 이 통합 시험의 새 변경은 해당 커밋 뒤 작업입니다.
- `test/`의 일곱 스케치를 모두 `teensy:avr:teensy41`, `--warnings all`로 컴파일했습니다.
  새 통합 시험은 오류·경고 0개입니다. Loader 실행 방지를 위해 postbuild hook을 비웠습니다.
- `check_combined.ps1`: 71개 컴파일 타임 assertion + 핀맵/소스 검사 통과.
  최초 핀 이름 비교 검사는 생산 코드의 `PIN_PRESSURE_SENSOR`와 시험의 `PIN_PRESSURE` 별칭
  차이로 실패했으며, 검사에서 별칭을 대응시킨 뒤 재실행해 통과했습니다. 핀은 모두 A8입니다.
- 기존 `check_stage4.ps1`의 35개 assertion, `check_contact.ps1`의 21개 assertion과
  기존 MAX30102 로그 321개 표본 재생 검사도 통과했습니다. 새 실물 로그가 아닙니다.
- `git diff --check` 및 UTF-8/CRLF/행 끝 공백 검사 통과.
- PC용 `g++`가 없어 `teensy/test_saver_proto.cpp`의 네이티브 실행 시험은 수행하지 못했습니다.

전체 회귀 컴파일 중 기존 MAX30102 외부 SparkFun 라이브러리에서만 경고가 4개 나왔습니다.

| 외부 라이브러리 위치 | 경고 |
|---|---|
| `spo2_algorithm.h:83` | `an_x` 미사용 |
| `spo2_algorithm.h:84` | `an_y` 미사용 |
| `heartRate.cpp:108` | 비교식과 비트 `&` 사용 위치에 괄호 권고 |
| `MAX30105.cpp:292` | `originalContents` 미사용 |

주요 실행 명령은 다음과 같습니다. CLI는 Arduino IDE에 포함된 실행 파일을 사용했습니다.
`<임시 빌드 폴더>`는 저장소 밖의 실행별 새 디렉터리입니다. 업로드 명령은 실행하지 않았습니다.

```text
git add -- <FG 제외 관련 7개 명시 경로>
git commit -m "[teensy] 음압 FG 제외 및 벤치 안전 정지 보완"
arduino-cli compile --fqbn teensy:avr:teensy41 --warnings all --build-property "recipe.hooks.postbuild.1.pattern=" --build-path <임시 빌드 폴더> <스케치 폴더>
powershell.exe -NoProfile -ExecutionPolicy Bypass -File test/tests/check_combined.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File test/vacuum_motor_stage4_fg/tests/check_stage4.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File test/max30102_sensor_test/tests/check_contact.ps1 -LogPath <기존 사용자 로그>
git diff --check
```
