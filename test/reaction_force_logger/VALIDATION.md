# 검증 기록 — 2026-09-27

## 변경 범위

`test/reaction_force_logger/`에만 9개 파일을 추가했습니다.

- Arduino 스케치 1개, 보조 헤더 3개
- 로직 시험 C++ 및 검사 PowerShell 각 1개
- CSV 검사 도구 1개
- README와 이 검증 기록

기존 수정 파일 `test/README.md`, `test/bench_logic.h`, `test/test.ino`,
`test/tests/check_combined.ps1`과 기존 미추적 폴더는 보존했습니다.
`teensy/`, `jetson/`, `protocol/`, `docs/`, 기존 모터 시험 파일을 변경하지 않았습니다.
Git 커밋/푸시, 펌웨어 업로드, 시리얼 포트 열기, SD 카드 접근은 하지 않았습니다.

## 실행한 주요 명령

PowerShell, 저장소 루트:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File test/reaction_force_logger/tests/check_logger.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File test/reaction_force_logger/tools/check_recording.ps1 -SelfTest
& 'C:/Program Files/Arduino IDE/resources/app/lib/backend/resources/arduino-cli.exe' compile --fqbn teensy:avr:teensy41 --warnings all --build-property 'recipe.hooks.postbuild.1.pattern=' --build-path 'C:/Users/Administrator/AppData/Local/Temp/saver-reaction-20260927' test/reaction_force_logger
& 'C:/Users/Administrator/AppData/Local/Arduino15/packages/teensy/tools/teensy-compile/15.2.1/arm/bin/arm-none-eabi-g++.exe' -std=c++11 -Wall -Wextra -fsyntax-only teensy/test_saver_proto.cpp
git diff --check
git status --short
```

`clang-format.exe -i` 적용 후 `--dry-run --Werror`로 `.ino`/`.h`/시험 `.cpp`를 검사했습니다.
스타일은 `{BasedOnStyle: LLVM, IndentWidth: 2, ColumnLimit: 100, SortIncludes: false}`입니다.
새 폴더의 모든 파일에 대해 후행 공백 검사도 수행했습니다.

## 결과

| 검사 | 결과 / 범위 |
|---|---|
| `check_logger.ps1` | **58개 static_assert 통과**. 실제 C++ 로직을 ARM 컴파일러가 평가 |
| 원시값·보정·요약 | signed 24-bit, 실제 0/미측정 구별, 역방향 센서 보정, 영점 재설정, 포화, 보정 구간 겹침, 평균/최대/최소 |
| 버퍼·명령·시각 | 버퍼 wrap/넘침 시 행 단위 보존, 명령 길이/timeout/CRLF, micros wrap, UTC 날짜/윤일/범위 |
| NAU 상태기계 모의 I/O | NACK, short read, 전원/보정 timeout, AFE 오류, 설정 readback, 준비 전 읽기 금지, 동일한 새 값 수용, 재연결 |
| CSV 검사 도구 | **가상 기록 8개 실행 통과**. 정상 320Hz 시각, 미측정 0 위장, 종료행 없음, 중복 시각, 행 수 불일치, 누락, 포화, NaN |
| Teensy 4.1 컴파일 | **최종 오류 0, 경고 0**. Wire/SdFat 포함 실제 스케치 빌드 |
| 포맷/공백 | 통과 |
| 기존 `test_saver_proto.cpp` | ARM 컴파일러 문법 검사만 통과. PC 실행형 단위시험은 수행하지 못함 |

최종 빌드 메모리 출력:

```text
FLASH: code 74956, data 11448, headers 8824
RAM1: variables 80320, code 72008, padding 26296, free local 345664
RAM2: variables 12416, free malloc/new 511872
```

첫 제한 환경 컴파일 시 Arduino15 패키지 symlink/임시 폴더 접근 거부로 시작하지 못했습니다.
접근 허용 후 처음 빌드에서 UTC 문자열의 잠재적 버퍼 잘림 경고 `-Wformat-truncation` **1개**가
나왔습니다. UTC 버퍼를 32→48 bytes로 늘리고 재컴파일하여 경고를 해소했습니다.

`git diff --check`의 기존 네 변경 파일 LF→CRLF 안내는 이번 컴파일 경고가 아닙니다.
PC용 `g++/clang++/cl`은 PATH에서 확인되지 않았습니다. `python`은 실행 불가능한 WindowsApps
별칭이어서 사용하지 않았으며, CSV 검사 도구는 PowerShell만 사용합니다.

## 가정과 아직 검증하지 못한 것

- DYMH-103 0~100 kg 및 NAU 모듈 3.3 V는 **사용자 제공 정보**입니다.
- 내부 3.0 V LDO/CH1/gain128/CH2 PGA 커패시터 경로 초기화는 일반 NAU7802 회로를 가정합니다.
  실제 모듈 AVDD/REFP/E+ 연결, 로드셀 여기전압 적합성, 선 배치 확인이 먼저 필요합니다.
- 명목 320 SPS와 20 Hz 요약 설정을 구현했지만 실제 샘플 주기·무누락·SD 지연은 미측정입니다.
- I2C/USB/SD를 포함한 Arduino `loop()`의 종단간 실행은 하지 않았습니다. SD 오류 처리는
  컴파일/소스 확인이며 실제 SD 장애 주입 또는 모의 파일시스템 실행 시험이 아닙니다.
- 3초 배출 deadline은 loop 사이에서 검사합니다. 동기식 라이브러리 호출 자체를 강제로 선점하는
  timeout이 아닙니다. 모터 실시간 제어와 통합 전 지연 격리 및 최악 지연 검증이 필요합니다.
- 하중 보정값은 실측 전 비어 있습니다. 보정 성공 표시는 N 환산 가능 여부이며 정확도 인증이 아닙니다.
- 센서 단선이 유효 범위의 전압으로 보이는 경우를 완전히 검출하지 못합니다. 포화·I2C 실패
  검출만으로 힘 센서의 안전 무결성을 주장하지 않습니다.
- UTC는 수동 앵커이며 정밀 동기화되지 않았습니다. read_us는 ADC 변환 순간이 아닌 읽기 완료 시각입니다.
- 갑작스런 전원 단절의 파일/캐시 손실, 카드 종류별 호환성, 장시간 드리프트, 실제 힘/잡음,
  반력–변위 동기 측정, Jetson 프로토콜 연동, 모터/안전 입력 동작은 검증하지 않았습니다.

추후 실기 순서는 `README.md`의 **나중에 해야 할 실물 시험**을 따릅니다.

## 로드셀 단독 테스트 재확인 — 2026-10-11

사용자 요청에 따라 기존 스케치를 재사용하고 `README.md`에 SD카드 없는 빠른 테스트,
사용자 제공 그림의 EX/SG 선 색, Wire1 배선과 보정 순서를 추가했습니다.
스케치와 세 보조 헤더, 메인 펌웨어, 운용 프로토콜은 변경하지 않았습니다.

검증 수준은 `tested`(소프트웨어 검사)입니다. 아래 결과는 로드셀 실물·힘 정확도 검증이 아닙니다.

| 검사 | 결과 |
|---|---|
| `tests/check_logger.ps1` | static_assert 58개, 실제 드라이버 모의 I2C, 센서 전용·SD 소스 검사 통과 |
| `tools/check_recording.ps1 -SelfTest` | 합성 CSV fixture 8개 통과 |
| `teensy/tests/check_pinmap.ps1` | 핀 assertion 19개와 Wire1/메인 소스 일치 검사 통과 |
| Teensy 4.1 스케치 컴파일·링크 | 오류 0, 경고 0 |
| `teensy/test_saver_proto.cpp` | PC에서 실행, 통과 26 / 실패 0 |
| `tools/consistency_audit.py`, `git diff --check` | 통과. 기존 다른 작업 파일의 줄바꿈 안내는 보존 |

실행 명령(저장소 루트):

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File test/reaction_force_logger/tests/check_logger.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File test/reaction_force_logger/tools/check_recording.ps1 -SelfTest
powershell.exe -NoProfile -ExecutionPolicy Bypass -File teensy/tests/check_pinmap.ps1
& 'C:/Program Files/Arduino IDE/resources/app/lib/backend/resources/arduino-cli.exe' compile --fqbn teensy:avr:teensy41 --warnings all --build-property 'recipe.hooks.postbuild.1.pattern=' --build-path "$env:TEMP/saver-loadcell-test-20261011" test/reaction_force_logger
$taskDir = Join-Path $env:TEMP 'saver-v24-review-zig-0.13.0'
$env:ZIG_GLOBAL_CACHE_DIR = Join-Path $taskDir 'global-cache'
$env:ZIG_LOCAL_CACHE_DIR = Join-Path $taskDir 'local-cache'
& "$taskDir/zig-windows-x86_64-0.13.0/zig.exe" c++ -std=c++11 -Wall -Wextra -Werror teensy/test_saver_proto.cpp -o "$taskDir/test_saver_proto.exe"
& "$taskDir/test_saver_proto.exe"
& 'C:/Users/Administrator/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe' tools/consistency_audit.py
git diff --check
```

Arduino CLI는 제한 환경에서 설치된 패키지 symlink 접근이 거부되어 실패했고,
허용된 환경에서 같은 컴파일 명령을 다시 실행해 통과했습니다. Teensy Loader 후처리는
비워 두었으며 **펌웨어 업로드·시리얼 포트 열기·장치 구동은 하지 않았습니다.**

기존 내부 LDO 3.0 V와 CH1 배선 가정은 유지합니다. 사용자의 실제 NAU7802 모듈의
AVDD/REFP/E+ 회로, 여기전압 적합성, 하중 전달 방향, 풀업·배선 품질은 실물 확인 대상입니다.
ADC 응답만으로 로드셀 단선이나 기계적 설치 이상을 모두 검출하지 못합니다.
