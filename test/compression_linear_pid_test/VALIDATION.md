# lm_current_v3 검증 기록 — 2026-09-27

## 변경 범위

이 폴더 내부만 변경했습니다. LM 원본과 다른 시험/운용 펌웨어/프로토콜은 수정하지 않았습니다.
커밋·푸시·업로드·시리얼 포트 접속은 하지 않았습니다.

| 파일 | 작업 |
|---|---|
| `compression_linear_pid_test.ino` | LM 전류 PID 통합, ZERO/CAL 및 설정 명령, 전류 로그. 기존 안전/통신/시간 검사 유지 |
| `current_control.h` | 전류 PID·필터·RAM 두 점 보정 로직 추가 |
| `bench_config.h`, `bench_guard.h` | 전류 단위 설정과 검증 잠금으로 전환. 미설치 보호장치 플래그 유지 |
| `tests/test_current_control.cpp`, `tests/check_current_control.ps1` | 새 전류/보정 검사 |
| `tests/test_bench_guard.cpp`, `tests/check_bench.ps1` | 현재 제어 방식·잠금의 회귀 검사 갱신 |
| `README.md` | 현재 명령/차이/미검증 조건 설명 |
| `README_commissioning_v2.md` | 이전 README를 과거 설명임을 표시해 보관 |
| `VALIDATION.md` | 이 기록 |

LM 원본 SHA256:
`72796228CEFE9F725FB5F4CF9C1284601D56A5D91AFDB013F8C784C5749F9133`.
기존 `vesc_link.h`, `depth_control.h`, `calibration_capture.h`는 변경하지 않았습니다.

## 실행과 결과

다섯 스크립트를 각각 다음과 같이 실행했습니다.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File test/compression_linear_pid_test/tests/check_depth_control.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File test/compression_linear_pid_test/tests/check_vesc_link.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File test/compression_linear_pid_test/tests/check_bench.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File test/compression_linear_pid_test/tests/check_calibration_capture.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File test/compression_linear_pid_test/tests/check_current_control.ps1
```

- 각각 71 + 36 + 54 + 34 + 39 = **234개 컴파일타임 assertion 통과**, 핀맵/제어·통신 분리 소스 검사 통과.
- 설치된 Teensy ARM g++의 `-std=c++14 -Wall -Wextra -Werror -fsyntax-only`로 실제 헤더를 평가했습니다.
  단순 문법 검사뿐 아니라 constexpr/static_assert 평가지만, **PC 실행형/실물 종단간 시험은 아닙니다**.
- 새 검사: 정·역 ADC 보정, 재보정 실패 시 이전 비율 무효화, rail/NaN/짧은 span 거부,
  signed 전류/CRC, 양방향 포화/anti-windup, 필터, 측정 미분, 지연/잘못된 dt와 재시작 이력 초기화.
- 기존 검사: 명령 수치/범위, ARM 만료·중복 RUN·fault·clear, CRC/손상/오래된 프레임 등.

컴파일 실행 명령:

```powershell
& 'C:/Program Files/Arduino IDE/resources/app/lib/backend/resources/arduino-cli.exe' compile --fqbn teensy:avr:teensy41 --warnings all --build-property 'recipe.hooks.postbuild.1.pattern=' --build-path 'C:/Users/Administrator/AppData/Local/Temp/saver-current-pid-20260927' test/compression_linear_pid_test
```

- Teensy platform 1.62.0, 컴파일 성공. **오류 0, 컴파일 경고 0**.
- Teensy Loader 후처리를 꺼서 컴파일만 했습니다. Arduino15 설치 툴체인 접근 권한으로 실행했습니다.
- 변경 C++/스케치에 Arduino IDE의 clang-format 적용 및 `--dry-run --Werror` 검사.
- `git diff --check` 및 이 폴더 전체 후행 공백 검사. 이 폴더는 현재 Git untracked라 별도 공백 검사도 실시.
  다른 네 파일의 LF→CRLF Git 안내는 기존 변경에 대한 것으로, 컴파일 경고가 아닙니다.
- `Get-Command g++`에서 PC용 컴파일러를 찾지 못했습니다. 저장소의 PC 실행형
  `teensy/test_saver_proto.cpp`는 실행하지 못했고 ARM g++ `-std=c++11 -Wall -Wextra -fsyntax-only`
  문법 검사만 통과했습니다. 이 결과를 실행형 시험 통과로 보지 않습니다.

## 가정과 미검증

- LM 사용자 보고를 알고리즘/기본값의 기준으로 삼았지 안전성 검증으로 삼지 않았습니다.
- LM의 게인·전류/전압/FET 온도 값은 출처를 표시해 이식했고 검증 완료 플래그는 false입니다.
  전원 정격, 회생, VESC 실제 설정/펌웨어 호환성은 실물 확인이 필요합니다.
- 상·하단 리미트와 별도 물리 E-stop은 미설치로 재확인되어 잠금 유지.
- 합쳐진 스케치의 실제 루프/USB/UART 상호작용, ADC 잡음/선형성/보정 정확도, 실제 전류·방향,
  깊이·CPM·추종 오차·정지거리·회생·발열·비상정지/통신 단절 반응은 시험하지 않았습니다.
- CAL의 거리와 작업 원점은 사용자가 실제 측정해야 하며 코드는 자동 홈/센서 존재 검증을 하지 않습니다.
- 0A 요청은 물리 정지·하중 유지가 아닙니다. 추가 능동 복귀는 기본 비활성입니다.
- 다른 폴더의 기존 미커밋 변경이나 과거 시험 실패는 이번 작업으로 해결했다고 주장하지 않습니다.

## USB 콘솔 출력 속도 조정 — 2026-10-15

시리얼 모니터 스크롤이 너무 빠르다는 사용자 보고에 따라 자동 상태 묶음만
50ms → 1000ms로 변경했습니다. 한 묶음의 여러 줄 출력은 유지합니다.
`STATUS` 명령과 오류·완료 이벤트의 기존 전송 경로는 그대로입니다.
명시적 `CHECK`는 별도 `CHECK_REPORT_MS=50`을 사용해 항목 출력이 느려지지 않게 했습니다.
PID/ADC 200Hz, 전류 지령 50Hz, VESC 조회 20Hz와 모든 구동 잠금·한계는 변경하지 않았습니다.
`delay()`를 추가하지 않았으며 운용 JSON 프로토콜이나 다른 스케치는 수정하지 않았습니다.

검증 수준: `tested` — 소프트웨어 검사만 수행했습니다.

- 위 다섯 `check_*.ps1` 재실행: 기존 234개 static_assert 통과.
- `check_bench.ps1`에 콘솔/조회/CHECK 주기의 독립성 소스 검사를 추가해 통과.
- Teensy 4.1 컴파일·링크: 오류 0, 경고 0. 위 Arduino CLI 명령의 build-path만
  `$env:TEMP/saver-compression-linear-review-20261015`로 변경해 실행.
- 위 스타일의 `clang-format --dry-run --Werror`, `git diff --check` 통과.
- 실제 Python 실행 파일로 `tools/consistency_audit.py` 실행: 불일치 없음.

수정 파일은 `.ino`, `README.md`, `tests/check_bench.ps1`, 이 기록뿐입니다.
펌웨어 업로드·시리얼 포트 접근·모터 구동은 하지 않았습니다.
실제 콘솔 표시와 제어 루프 지연·모터 동작은 미검증이며, 장치에는 재업로드 후 반영됩니다.

## 리미트·보조접점 미사용 벤치 프로필 — 2026-10-15

사용자 답변: 상·하 리미트는 설치하지 않음, USB/Teensy와 독립된 모터 전원 차단 비상정지는
설치됨, Teensy 6번 보조접점은 미연결. 전류 부호에 따른 실제 회전 방향은 아직 확인 못 함.
최신 요청은 축 끝 정면 기준 압박 반시계(CCW), 복귀 시계(CW)입니다.

- `LIMIT_SWITCHES_REQUIRED=false`, `LIMIT_SWITCHES_INSTALLED=false`로 분리했습니다.
  없다는 사실을 설치/검증 완료로 위장하지 않고 `LIMIT_SWITCHES_OPTIONAL_BENCH`로 표시합니다.
- 물리 차단의 존재는 `PHYSICAL_ESTOP_POWER_CUT_USER_CONFIRMED=true`라는 **사용자 보고
  (`assumed`)**로만 반영했습니다. `ESTOP_AUX_INSTALLED=false`이므로 버튼 상태를 알 수 없고,
  `estop_state_observable=0`을 표시합니다. 누락된 접점의 raw 상태는 출력하지 않습니다.
- 미사용 3/4/6번을 상태 판단에서 읽지 않습니다. 나중에 설치한 접점은 극성/검증 조건을
  통과해야 읽고 감시합니다. 물리 전원 차단 요구는 유지하며 소프트웨어가 이를 대체하지 않습니다.
- 압박 CCW는 요청 표시일 뿐 실제 방향 검증이 아닙니다. `CURRENT_SIGN_FOR_INCREASING_MM=0`,
  `MOTOR_DIRECTION_VERIFIED=false`는 유지합니다. 부호·이동/추종 한계·VESC 설정 등 미확인 항목이
  남아 있어 **실제 RUN 허가는 아직 나지 않습니다**. 모든 잠금 해제 요청으로 확대하지 않았습니다.
- 정지/USB 단절/통신 오류·기존 전류/위치 보호, 1초 상태 출력은 유지했습니다.
  새 빌드는 `lm_current_v3_bench_no_limits`입니다. 운용 펌웨어·protocol은 변경하지 않았습니다.

검증 수준: `tested`(소프트웨어만).

- 위 다섯 `check_*.ps1`: **247개 static_assert 통과**(71+36+67+34+39).
  필수/선택 리미트, 설치된 접점의 미확인 극성, 보조접점 검증 필요, CCW 요청과 미확인 부호의
  구별을 추가 검사했습니다. 소스 검사는 미설치 입력을 읽기 전에 설치 여부를 확인하는지 검사합니다.
- 위 Arduino CLI 명령으로 Teensy 4.1 컴파일·링크, `--warnings all`: 오류 0, 경고 0.
  build-path는 `$env:TEMP/saver-compression-linear-review-20261015`입니다.
- 위 스타일의 clang-format 검사 및 `git diff --check`, `tools/consistency_audit.py` 통과.
- PC용 Zig 0.13.0 `c++ -std=c++11 -Wall -Wextra -Werror teensy/test_saver_proto.cpp` 빌드 후
  실행: 통과 26 / 실패 0. 임시 실행 파일/캐시는 `%TEMP%/saver-v24-review-zig-0.13.0`에 있습니다.

실제 전원 차단, 해제 후 재시작, VESC timeout, 반시계 전류 부호, 소프트 이동 한계, 수직축
낙하·오버트래블은 미검증입니다. 보조접점이 없어 순간 전원 차단/재인가를 반드시 감지할 수
없습니다. 업로드·시리얼 포트 열기·실제 모터 구동은 하지 않았습니다.

## 사용자 관찰에 따른 압박 방향 부호 반영 — 2026-10-15

사용자가 별도 `compression_current_only_test`의 `PULSE 1`(+1 A 지령)에서 시계 회전을
관찰했다고 보고했습니다. 압박 방향 요청은 반시계입니다. 이 사용자 보고를 근거로
`CURRENT_SIGN_FOR_INCREASING_MM=-1`, `MOTOR_DIRECTION_VERIFIED=true`로 변경했습니다.
위 선행 기록의 방향 미확인 상태는 이 절에서 갱신합니다.

- 이 플래그는 **사용자가 확인한 전류 부호/회전 방향 대응**만 나타냅니다. Codex가 실물을
  측정하거나 리니어 피드백·설치 기구·실제 왕복 추종을 검증했다는 뜻이 아닙니다.
- PID의 논리 양수 출력은 VESC 음수 전류, 논리 음수 출력은 양수 전류로 변환됩니다.
  감속 토크와 실제 이동 방향은 순간적으로 다를 수 있습니다.
- 리니어 ZERO/CAL은 압박 이동을 양수 mm로 정의해야 합니다. 추가 MOTORFLIP은 하지 않습니다.
- 진단에 `configured_sign=-1,source=user_report`를 표시하고 빌드는
  `lm_current_v3_bench_no_limits_ccw`로 구분했습니다.
- 이동/추종 한계·최대 실행시간·VESC 및 센서 검증 등의 기존 미확인 값은 그대로입니다.
  방향 수정만으로 실제 RUN 허가가 나는 것은 아닙니다. 별도 1초 시험 코드도 변경하지 않았습니다.

검증 (`tested`, 소프트웨어만): 위 다섯 `check_*.ps1` 재실행 **249개 static_assert 통과**
(71+36+69+34+39). 정지 이력에서 양수/음수 위치 오차에 대한 실제 전류 PID 계산과 설정 부호의
곱을 검사하는 두 assertion을 추가했습니다. 스케치가 같은 곱셈 경로를 사용하는 소스 검사도 유지합니다.
위 Arduino CLI 명령(build-path `saver-compression-linear-review-20261015`)의 Teensy 4.1
컴파일·링크는 오류 0·경고 0입니다. clang-format 검사, `git diff --check`,
`tools/consistency_audit.py`도 통과했습니다.

이번 수정은 이 폴더의 설정·스케치 진단·README·검사·검증 기록에만 적용했습니다.
프로토콜·메인 펌웨어·다른 시험 코드는 수정하지 않았고 업로드나 추가 실물 구동은 하지 않았습니다.

## 사용자 지정 최대 구동시간 10분 반영 — 2026-10-15

사용자가 최대 구동시간을 10분으로 지정해 `MAX_RUN_MS`와 공통 시간 검사 상한
`ABSOLUTE_RUN_MS`를 모두 `600000`ms로 변경했습니다. 기존 별도 30초 상한도 함께
갱신해 설정 검사와 요청 시간 검사가 모순되지 않게 했습니다. 운용 펌웨어의 통신 두절
`T_comm`이나 별도 1초 전류 펄스 코드는 변경하지 않았습니다.

- 이 시간은 사용자 지정 벤치 상한(`assumed`)이며 발열·수명·기계 안전성이 실측 검증된
  10분 연속 운전 정격이 아닙니다. 미확인 센서·이동 범위·전류·VESC·최종 구동 허용은
  여전히 잠겨 있어, 이 변경만으로 ARM/RUN이 허가되지 않습니다.
- 한 RUN은 요청 횟수가 먼저 끝나면 종료하며 최대 20왕복 제한도 유지합니다.
  `TARGET 5 10` + `RUN 3`의 목표 시간은 18초입니다. 선택적 능동 복귀 시간도 총 상한에
  포함하며, PREVIEW의 공통 시간 검사 상한도 10분으로 변경됩니다.
- 실시간 제어의 기존 경과 시간 검사와 `RUN_TIME_LIMIT` 오류를 유지했습니다.
  `check_bench.ps1`에서 해당 검사 소스가 남아 있는지도 검사합니다.
- 수정 파일: `bench_config.h`, `bench_guard.h`, `tests/test_bench_guard.cpp`,
  `tests/check_bench.ps1`, `README.md`, 이 기록. 기존 다른 변경은 보존했습니다.

검증 수준: `tested`(소프트웨어만).

- 위 다섯 `check_*.ps1` 재실행: **256개 static_assert 통과**(71+36+76+34+39).
  추가 검사는 CHECK 시간 항목 통과, 요청 3왕복 허용 시간 계산, 10분 경계 일치,
  설정 한계보다 1ms 긴 요청·10분 초과 궤적·허용 상한 초과 설정·정수 오버플로 거부입니다.
- 위 Arduino CLI 명령(build-path `saver-compression-linear-review-20261015`)의 Teensy 4.1
  컴파일·링크: 오류 0, 경고 0. 기본 샌드박스의 Arduino15 심볼릭 링크 접근이 거부돼
  설치 도구 접근 권한으로 컴파일만 재실행했습니다. 업로드 후처리는 비활성입니다.
- 위 스타일의 `clang-format --dry-run --Werror`, `git diff --check` 및
  `tools/consistency_audit.py` 통과. Git의 기존 LF/CRLF 안내는 컴파일 경고가 아닙니다.

업로드·시리얼 포트 접근·모터 구동은 하지 않았습니다. 실제 10분 연속 운전과 정지 시점,
루프 지연, 발열·회생·센서 신뢰성·기계 이동 여유는 미검증입니다.

## 원점 기준 압박 방향 상한 50mm 반영 — 2026-10-15

사용자가 허용 이동 범위를 원점에서 50mm로 지정해 압박 방향 상한 `SOFT_MAX_MM=50`을
반영했습니다. 사용자 지정 값이며 실물 측정/정지 여유 검증 결과로 기록하지 않습니다
(`assumed`). 원점 반대쪽의 실제 여유는 알려지지 않아 `SOFT_MIN_MM=0`을 미설정으로
유지했습니다. 기존 검사는 하한이 원점보다 작고 원점과 목표가 경계 안쪽에 있어야 하므로
`SOFT_TRAVEL_NOT_SET`은 아직 실패합니다. 임의의 음수 여유나 새 위치 허용 오차를 만들거나
원점/상한 경계 검사를 완화하지 않았습니다. 따라서 목표 50mm는 현재 경계와 같아 거부합니다.

- 수정 파일: `bench_config.h`, `tests/test_bench_guard.cpp`, `README.md`, 이 기록.
- `tested`(소프트웨어만): 위 다섯 `check_*.ps1` 재실행, **257개 static_assert 통과**
  (71+36+77+34+39). 상한 50mm 반영과 하한 미설정 차단 유지를 함께 검사했습니다.
- 위 Arduino CLI 명령(build-path `saver-compression-linear-review-20261015`)으로 Teensy 4.1
  컴파일·링크: 오류 0, 경고 0. 설치 도구 접근 권한을 사용했고 업로드 후처리는 비활성입니다.
- 위 스타일의 clang-format 검사, `git diff --check`, `tools/consistency_audit.py` 통과.
- 업로드·시리얼 포트 접근·모터 구동은 하지 않았습니다. 하한, 보정 정확도, 센서 잡음,
  실제 기계 여유·정지거리 및 나머지 미확인 구동 조건은 검증하지 않았습니다.
