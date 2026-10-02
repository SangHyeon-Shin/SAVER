# 리니어 센서 단독 읽기·두 점 보정 시험 — Teensy 4.1

Arduino IDE에서 `linear_sensor_test.ino`를 엽니다. 같은 폴더의 `linear_reading.h`, `linear_calibration.h`,
`linear_calibration_store.h`가 필요합니다. 보드 **Teensy 4.1**, USB Type **Serial**.
추가 설치 없이 Teensyduino 기본 EEPROM 라이브러리를 사용합니다.
연구용 벤치 시험이며 모터 구동·위치 제어·압박 동작은 포함하지 않습니다.

> **설치 전 알림:** 리니어 센서를 설치하기 전에 ZERO/CAL로 보정하세요. 설치 후에는
> 기준점·실측 거리·방향을 다시 확인하세요. 센서와 기구의 결합/기준점이 바뀌었다면
> 기존 보정을 그대로 신뢰하지 말고 재보정해야 합니다. EEPROM 저장은 정확도 인증이 아닙니다.

## 배선 전에 확인

**모든 모터의 전원과 제어 배선을 분리한 상태에서 사용하세요.** 이 스케치는 다른 장치의
EN/PWM을 초기화하거나 UART 정지 명령을 보내지 않습니다. 업로드만으로 연결된 모터가
정지한다고 보장할 수 없습니다.

| 신호 | 연결 | 조건 |
|---|---|---|
| 센서 아날로그 OUT | Teensy **A16 / 물리 40번** | 전처리 후 모든 상태에서 0~3.3 V 이내 |
| 센서 신호 GND | Teensy GND | 비절연 아날로그 신호의 공통 기준 |
| 센서 전원 | **3.3 V** | 사용자 확인: YwRobot 75mm / 10kΩ / 0~VCC 슬라이드 모듈 |

**디지털 16번과 A16(40번)은 다릅니다.** 저장소 `teensy/pinmap.h` 배정을 따릅니다.
실물의 VCC/GND/OUT 라벨을 확인합니다. 선 색이나 단자 순서는 추측하지 않습니다.
사용자가 알려준 명목 75mm를 곧바로 ADC 전 범위에 대응시키지 않고 두 위치를 실측합니다.

Teensy ADC는 0~3.3 V 입력이며 5 V tolerant가 아닙니다.
[PJRC 공식 사양](https://www.pjrc.com/store/teensy41.html)을 참고하세요.
0~5 V/0~10 V 출력은 전처리 없이 직결하지 않습니다. 4~20 mA 출력 역시 이 전압입력
배선에 바로 연결할 수 없습니다. 압력센서용 4.7 kΩ/10 kΩ 분압을 자동으로 재사용하지 않습니다.
전압 변환식을 바꿔도 실제 과전압을 막지는 못합니다.

## 표시되는 값

12-bit ADC, 하드웨어 평균 16회, 기본 100 ms(초당 10줄)입니다.
`adc_V = adc_raw × ADC_REFERENCE_V / 4095`, 공칭 기준은 3.3 V입니다.
`ADC_REFERENCE_V`는 ADC 기준전압이며 센서의 공급전압이 아닙니다. 정밀 보정은 별도 필요합니다.

다음은 설명용 형식 예시이며 실측 결과가 아닙니다.

```text
DATA,ms=1200,raw_ok=1,position_ok=0,adc_raw=2048,adc_V=1.6504,adc_rail=0
```

- `raw_ok=1`: 읽은 ADC 코드가 범위 내임. 센서 연결·정확도 정상 판정이 아닙니다.
- `adc_raw`: 0~4095 원시값. 정상적으로 측정한 0은 그대로 0으로 표시합니다.
- `adc_V`: **Teensy A16 핀 전압의 추정값**. 분압이 있으면 센서 원출력 전압과 다릅니다.
- `adc_rail=1`: 원시값이 0~1 또는 4094~4095. 끝단/배선/포화 확인 표시일 뿐 고장 확정이 아닙니다.
- `position_ok=0`: 위치 환산 미설정. `position_mm`는 가짜 0을 넣지 않고 생략합니다.
- `position_ok=1`: 수집한 두 점으로 환산 가능하다는 뜻이며 정확도·센서 존재 인증이 아닙니다.
  `position_mm`는 수동 ZERO에 대한 상대 위치입니다. ADC 증가/감소 방향을 자동 반영합니다.
- `within_cal_span=0`: 보정한 두 점 바깥의 **외삽값**. 음수를 0으로 자르거나 범위 안으로 숨기지 않습니다.
- 위치용 ADC는 0~5 / 4090~4095를 거부합니다 (`position_raw_usable=0`). 기존 `adc_rail` 표시의
  0~1 / 4094~4095보다 여유를 둔 검사이며, 아날로그 단선 검출을 보장하지 않습니다.
- 실패/범위 밖 ADC 결과는 `raw_ok=0`과 오류만 표시하고 측정 숫자는 생략합니다.
- `STATUS`의 `sensor_presence_known=0`: **아날로그 값만으로 미연결을 확정할 수 없음**.
  떠 있는 입력에도 숫자가 나옵니다. ADC 값이 안정적이라는 사실만으로 정상 연결을 판단하지 마세요.
- `dropped_logs`: USB 송신 공간 부족 등으로 버린 출력 수. 느린 모니터를 기다리지 않습니다.

## 사용법

시리얼 모니터: 115200, 줄바꿈 New Line 또는 Both NL & CR.
열면 안내 후 자동으로 원시값/전압을 표시합니다. 모터 자동 구동 기능은 없습니다.

| 명령 | 동작 |
|---|---|
| `READ` | 새 ADC 값을 한 번 읽어 표시 |
| `START` | 주기적 읽기/출력 재개 |
| `STOP` | 주기적 출력 및 진행 중 보정 수집 취소. **모터 정지 명령이 아님** |
| `STATUS` | 출력 설정·센서 존재 미확인·누락 로그 수 |
| `HELP` | 시작 안내 다시 표시 |
| `ZERO` | 기존 EEPROM 보정 무효화 후 현재 위치 50개 평균으로 상대 0mm 설정 |
| `CAL 50` | ZERO에서 **실측 50mm 이동한 위치**를 수집해 비율 적용·EEPROM 자동 저장. 모터 이동 명령 아님 |
| `CAL_SHOW` | ZERO/CAL 통계·거리·mm/count와 `saved`, `storage_state` 표시 |
| `CAL_RESET` | RAM 보정/진행 중 수집/EEPROM 저장값 무효화. 재부팅해도 원시값만 표시 |
| `CHECK 25` | 현재 위치를 실측 25mm와 비교. `error_mm = 표시값 - 실측값`, 보정값은 변경하지 않음 |

대소문자와 앞뒤 공백은 허용합니다. 줄바꿈 LF 전까지 공백·CR을 포함하여 31자 이내이며,
첫 글자 후 1초 이내 줄바꿈이 필요합니다. 인자는 부호 없는 십진수이며 NaN/Inf/남는 인자를 거부합니다.
깨진/너무 긴/시간 초과 줄은 줄바꿈까지 버립니다. USB 재연결 시 부분 명령을 폐기하고
안내를 다시 표시하며, `STOP`으로 중단한 출력은 `START` 전까지 다시 시작하지 않습니다.
보드 리셋 시 읽기 자동 시작 기본값으로 돌아가고, 유효한 EEPROM 보정이 있으면 복원합니다.
전원이 유지된 채 시리얼 모니터만 닫았다 열면 완료된 보정은 유지하지만 진행 중 수집은 취소합니다.
배선·센서 설치·시작점을 바꿨다면 CAL_RESET 후 다시 측정합니다.

출력 간격은 `.ino` 위쪽 **사용자가 수정하는 부분**의 `SAMPLE_PERIOD_MS`를 바꿉니다.
이 코드는 Jetson JSON 프로토콜이 아닌 PC 시리얼 모니터용 텍스트입니다.

## 오늘 보정하고 저장하는 순서 (`build=calibration_eeprom_v3`)

1. 모터 전원·제어선을 분리하고, 센서만 연결합니다. 기구에 붙어 있다면 이동부를 지지해 낙하/끼임을 막습니다.
2. 새 스케치를 업로드하고 시리얼 모니터를 엽니다. `build=calibration_eeprom_v3`를 확인합니다.
   이전 RAM 전용 버전에서 한 보정은 업로드를 넘어 자동 이전되지 않으므로 한 번 다시 보정합니다.
3. 센서 물리 끝점에서 여유를 둔 곳을 기준으로 잡고 `ZERO`. 약 0.5초간 고정해 `OK,ZERO_SET`을 기다립니다.
4. 같은 방향으로 **자로 실제 50mm 이동한 경우에만** `CAL 50`. 다시 약 0.5초 고정하고
   **`OK,CAL_APPLIED,calibrated=1,saved=1`**을 기다립니다. 이 응답 전에 전원을 끄지 마세요.
   충분한 여유가 없으면 더 작은 실측 거리로 합니다.
   입력 허용 거리는 5~75mm지만 물리 끝점/ADC rail을 보정점으로 사용하지 않습니다.
5. 시작점에서 실측 25mm 위치에 놓고 `CHECK 25`. 다른 중간 위치도 확인합니다.
6. 시작점으로 돌아와 `CHECK 0`. **이때 ZERO를 다시 입력하지 않습니다**. 반복 왕복 후에도 같은
   위치에서 얼마나 달라지는지 비교합니다. 모터 없이 수동 이동만 합니다.
7. `CAL_SHOW`의 `saved=1,storage_state=SAVED`와 CHECK 결과를 복사해 보관합니다.
8. 전원을 껐다 켜서 `CAL_STORAGE,state=LOADED,saved=1,calibrated=1`을 확인합니다.
   `CAL_SHOW`의 두 점/거리가 같은지, 같은 물리 위치의 `CHECK` 결과가 재현되는지 확인합니다.
   이 확인만 하려고 ZERO를 다시 입력하지 마세요. ZERO는 기존 저장값을 무효화합니다.
9. 같은 Teensy에 메인 `teensy/saver_teensy.ino`를 업로드하면 동일 저장 형식을 읽어
   내부 `linear_position_mm` 환산에 사용합니다. EEPROM을 지우는 초기화/복구나 다른 스케치의
   덮어쓰기는 피하세요. 업로드 후 보존 여부도 실물에서 확인해야 합니다.
   `test/compression_linear_pid_test`에는 이번 기능을 적용하지 않았습니다.

```text
ZERO
CAL 50
CHECK 25
CHECK 0
CAL_SHOW
```

위 명령 사이에는 실제로 센서를 해당 위치로 옮기고 응답을 기다려야 합니다. 한꺼번에 붙여넣지 마세요.
`CAL 50`의 50은 센서 정격이나 원하는 이동량이 아니라 **실제로 이동한 거리**입니다.
작은 거리로 CAL했다면 그 구간 안에서 중간점을 검증합니다.

```text
position_mm = (adc_raw - zero_raw_mean) * distance_mm / (span_raw_mean - zero_raw_mean)
```

두 보정점은 10ms 간격으로 50개를 수집하고 평균/min/max를 저장합니다. 느린 실행을 따라잡으려고
같은 샘플을 재사용하지 않습니다. 2초 초과/rail/범위 밖 ADC는 실패하고 위치 비율은 무효 상태로
남습니다. 두 평균 차이가 30코드 미만이거나 raw 수집 범위가 겹쳐도 보정을 거부합니다.
이는 최소 수집 검사일 뿐 안정성·정확도 인증이 아닙니다. 임의 합격 오차는 정하지 않았으며 CHECK의
오차와 반복 편차를 실측해 판단합니다. START/STOP의 출력 간격과 보정 수집 간격은 독립입니다.
미연결·중간 전압에 고정된 고장은 아날로그 값만으로 확인할 수 없습니다.

## 영구 저장의 범위와 실패 처리

- [PJRC EEPROM 문서](https://www.pjrc.com/teensy/td_libs_EEPROM.html)의 Teensy 4.1 내장 비휘발성
  저장 기능을 사용합니다. **논리 주소 4096~4135, 40바이트**를 두 스케치가 공유합니다.
  다른 EEPROM 사용자와 주소가 충돌하지 않도록 유지해야 합니다. SD 카드는 필요하지 않습니다.
- 보정이 없는 첫 부팅은 `EMPTY`, 형식/CRC가 잘못된 기록은 `INVALID`입니다. 둘 다
  보정되지 않은 상태로 두며 위치 숫자를 만들지 않습니다. 성공한 복원은 `LOADED`입니다.
- 저장 형식은 버전·센서 프로필·A16(40)·ADC 12bit·두 점의 통계·실측 거리·CRC-32를 담습니다.
  평균은 0.001 ADC 코드 단위, 거리는 0.000001mm 단위로 직렬화합니다. 이는 저장 해상도이며
  센서의 측정 정확도를 뜻하지 않습니다. 저장 후 같은 복원값을 RAM에도 적용합니다.
- ZERO 또는 유효한 새 CAL 수집 **시작 전** 기존 저장값을 무효화합니다. 중단/실패/USB 단절 후
  재부팅해도 오래된 보정이 되살아나지 않게 하기 위함입니다. ZERO만 완료한 상태는 저장하지 않습니다.
- 성공한 CAL 때만 내용과 CRC를 쓰고, 마지막에 완료 표식을 씁니다. 전체 읽기 검증 후 `saved=1`을
  보고합니다. 매 ADC 읽기/출력/USB 재연결에서는 쓰지 않아 불필요한 쓰기를 피합니다.
- `EEPROM_SAVE_VERIFY_FAILED`: 저장 확인 실패. 위치 환산도 비활성화하므로 전원/저장 상태를 점검하고
  다시 보정합니다. `EEPROM_CLEAR_FAILED,...old_record_may_remain=1`이면 이전 값이 남았을 수 있으므로
  재부팅만 해서 사용하지 말고 CAL_RESET 성공과 재보정을 확인합니다.
- 쓰기 도중 실제 전원 차단/플래시 소거 중 갈색전압(brownout)의 결과는 모의시험만으로 보장할 수 없습니다.
  정상 전원에서 응답 완료를 기다리세요. 저장 완료가 센서 설치 상태/선형성/안전성을 검증하지는 않습니다.
- 메인의 `cp_depth_mm`는 **직전 사이클 최대 압박 깊이**입니다. 현재 리니어 위치와 다르므로
  이번에는 이 텔레메트리 필드를 활성화하지 않았습니다. 프로토콜 변경/모터 구동도 하지 않았습니다.

### 공유 코드 유지

원본은 `teensy/src/saver_linear/`입니다. Arduino가 빌드 폴더로 스케치를 복사하므로
이 시험 폴더에 동일한 헤더 3개를 배포 복사본으로 둡니다. 원본을 수정하면 복사본도 동기화해야 하며,
`tests/check_linear.ps1`이 줄바꿈을 제외한 내용 일치를 검사합니다. 메인에는 `src/` 폴더도 함께 필요합니다.

## 소프트웨어 검사

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File test/linear_sensor_test/tests/check_linear.ps1
```

실제로 포함되는 헤더의 전압 계산, 잘못된 ADC 결과, rail 표시, millis rollover,
명령 timeout/손상/overflow/복구를 컴파일 타임에 검사합니다. 핀맵 일치와 모터 출력이
없는지도 소스 검사합니다. 실제 ADC/USB 동작, 센서 단선 검출, 위치 정확도는 시험하지 않습니다.
업로드나 실물 시험을 하지 않은 상태에서 하드웨어가 정상 동작한다고 주장하지 않습니다.

### 두 점 보정 추가 검증 (2026-09-30)

- 변경: `linear_sensor_test.ino`, 신규 `linear_calibration.h`, `tests/test_linear_reading.cpp`,
  `tests/check_linear.ps1`, 이 README. 기존 `linear_reading.h`와 다른 폴더는 변경하지 않았습니다.
- `powershell.exe -NoProfile -ExecutionPolicy Bypass -File test/linear_sensor_test/tests/check_linear.ps1`:
  **컴파일타임 assertion 56개**, A16 핀맵·모터 출력 금지 소스 검사 통과.
  정/역방향 두 점 환산, 중간점, 잘못된 숫자·rail·겹친 수집점·짧은 ADC 차이,
  평균/min/max·중복 샘플 억제·취소/타임아웃·millis 순환을 검사했습니다.
- 설치된 ARM g++ `-std=c++14 -Wall -Wextra -Werror -fsyntax-only`가 실제 constexpr 로직을
  평가한 결과이며 PC 실행형/ADC·USB 종단간 실물 시험은 아닙니다.
- 아래 명령으로 Teensy 4.1 컴파일 성공: **오류 0·경고 0**. Teensy Loader 후처리는 비활성화했습니다.

```powershell
& 'C:/Program Files/Arduino IDE/resources/app/lib/backend/resources/arduino-cli.exe' compile --fqbn teensy:avr:teensy41 --warnings all --build-property 'recipe.hooks.postbuild.1.pattern=' --build-path 'C:/Users/Administrator/AppData/Local/Temp/saver-linear-cal-20260930' test/linear_sensor_test
```

- 변경 C++/스케치 clang-format 적용 및 `--dry-run --Werror`, 폴더 후행 공백 검사, `git diff --check` 통과.
  기존 타 파일 4개의 LF→CRLF 안내는 컴파일 경고가 아닙니다.
- PC용 g++가 없어 `teensy/test_saver_proto.cpp` 실행형 시험은 못 했습니다. ARM g++의
  `-std=c++11 -Wall -Wextra -fsyntax-only` 문법 검사만 통과했습니다.
- 센서 모델/75mm 범위는 사용자 제공 정보입니다. 실제 전압·기계 방향·이동량·노이즈·선형성·
  보정 정확도·단선/USB 재연결 반응은 실물 미검증입니다. 반복 CHECK 오차로 확인해야 합니다.
- 업로드·시리얼 포트 접속·실물 시험·커밋·푸시는 하지 않았습니다. 다른 PID 스케치에 자동 반영하지 않습니다.

### 작성 시 검증 결과 (2026-09-10)

- 신규 컴파일 타임 검사 24개, A16 핀맵/모터 출력 금지 검사 통과.
- Arduino CLI 1.5.1, Teensy 플랫폼 1.62.0, Teensy 4.1: **컴파일 오류 0개 / 경고 0개**.
  최초 제한 환경에서는 Arduino15 패키지 접근 권한으로 실패했고, 권한 승인 후 같은
  설치 패키지로 성공했습니다. Teensy Loader 실행 hook을 꺼서 컴파일만 했습니다.
- 기존 통합 71개, 음압 stage4 35개, MAX30102 21개, 압박 단독 47개 컴파일 타임 검사 통과.
- 포맷, UTF-8, 끝 공백 검사 통과. 기존 파일 수정 없음.
- `teensy/test_saver_proto.cpp`는 ARM 컴파일러 문법 검사만 수행했습니다.
  PC 실행형 시험은 PC용 C++ 컴파일러가 없어 실행하지 못했습니다.
- 센서 모델/공급전압/출력범위, 실제 A16 전압과 단선, USB 재연결 실동작, 위치 정확도는
  미검증입니다. 업로드·하드웨어 동작·커밋·푸시는 하지 않았습니다.

실행 명령 요약 (도구는 설치된 절대 경로로 호출):

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File test/linear_sensor_test/tests/check_linear.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File test/tests/check_combined.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File test/vacuum_motor_stage4_fg/tests/check_stage4.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File test/max30102_sensor_test/tests/check_contact.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File test/compression_motor_single_test/tests/check_vesc_bench.ps1
arm-none-eabi-g++ -std=c++11 -Wall -Wextra -fsyntax-only teensy/test_saver_proto.cpp
arduino-cli compile --fqbn teensy:avr:teensy41 --warnings all --build-property 'recipe.hooks.postbuild.1.pattern=' --build-path 'C:\Users\Administrator\AppData\Local\Temp\saver-linear-60e87b0ca348404fb8ee609fef02adf5' test/linear_sensor_test
git diff --check
git status --short
```

새 `.ino/.h/.cpp`에는 clang-format (LLVM, 들여쓰기 2, 열 제한 100, include 정렬 끔)을
적용하고 `--dry-run --Werror`로 재검사했습니다. 검사 코드는 실제 포함되는 헤더를
사용하지만 실제 ADC/USB 런타임 동작을 대신 검증하지는 않습니다.
