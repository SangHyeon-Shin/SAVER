# 리니어 EEPROM 보정·메인 복원 검증 (2026-10-01)

## 변경 범위

- `linear_sensor_test.ino`: CAL 성공 자동 저장, 부팅 복원, ZERO/CAL_RESET 무효화,
  저장 상태 표시와 저장 실패 시 위치 환산 비활성화.
- `linear_calibration_store.h`: 버전/프로필/CRC-32/값 범위 검사, 완료 표식 마지막 쓰기,
  전체 읽기 검증. EEPROM 4096~4135의 40바이트만 사용.
- `linear_reading.h`, `linear_calibration.h`: 실제 계산은 유지하고 공유 원본을
  `teensy/src/saver_linear/`에 배치. Arduino 빌드용 동일 복사본을 시험 폴더에 유지.
- `teensy/src/saver_linear/linear_sensor_reader.h`: 메인의 읽기 전용 소비자.
- `teensy/saver_teensy.ino`: 부팅 시 복원, ADC 평균 16회, 현재 리니어 위치 취득.
  `cp_depth_mm`는 현재 위치가 아닌 직전 사이클 최대값이므로 계속 미측정으로 생략.
- `tests/test_linear_storage.cpp`, `tests/check_linear.ps1`: 모의 저장/복원 검사 및 공유 파일 일치 검사.
- 이 안내, `README.md`, `teensy/README.md`: 사용법, 설치 전 보정·설치 후 재확인 알림.
- `protocol/`, Jetson/UI, 압박 PID 시험, 다른 센서·모터 시험 코드는 수정하지 않음.
  원래 있던 `test/README.md`, `test/bench_logic.h`, `test/test.ino`, `test/tests/check_combined.ps1`
  변경은 그대로 보존했으며 이번 작업에 포함하지 않음.

## 확인한 것

- ARM g++의 **87개 컴파일타임 assertion 통과**. 실제 포함되는 C++ constexpr 로직을 평가함.
  기존 56개와 저장/복원·메인 소비 31개. PC 실행형 시험이나 ADC/USB 실물 시험은 아님.
- 320개 단일 비트 손상, 잘못된 버전/프로필/핀/ADC 분해능, CRC가 맞아도 잘못된
  캡처 수·평균·거리·rail, 43개 바이트 쓰기 예산에서 중단을 모의하여 검사.
- 빈 저장소, 초기화 후 재부팅, 실패한 초기화의 실패 반환, 취소된 재보정의 옛 값 복원 방지,
  정/역방향과 소수 거리 복원, 읽기 중 쓰기 없음, 메인 부팅 시 자동 영점 없음 검사.
- A16 핀맵 일치, 시험 코드에 모터 출력 없음, 메인 EEPROM 읽기 전용, 현재 위치/사이클 깊이
  분리, 두 스케치의 공유 헤더 일치 소스 검사.
- `teensy/test_saver_proto.cpp`: ARM g++ C++11 문법 검사 통과. PC용 g++/clang++/cl이 PATH에 없어
  저장소 규칙의 PC 실행형 프로토콜 시험은 실행하지 못함.
- clang-format 적용 및 검사: 시험 스케치/새 C++ 파일/공유 헤더, 메인 신규 함수 범위.
  기존 메인 파일 전체를 재포맷하지 않음. `git diff --check`와 대상 파일 후행 공백 검사 수행.

## 컴파일 및 경고

Arduino CLI, Teensy 플랫폼 1.62.0, `teensy:avr:teensy41`, `--warnings all --clean`.
Teensy Loader postbuild hook을 비활성화하여 업로드하지 않음.

- 리니어 시험: 오류 0, 경고 0.
- 메인: 오류 0, 외부 SparkFun MAX3010x 라이브러리 경고 4개. 새 보정 코드 경고 없음.
  - `spo2_algorithm.h:83`: 미사용 `an_x` (`-Wunused-variable`).
  - `spo2_algorithm.h:84`: 미사용 `an_y` (`-Wunused-variable`).
  - `heartRate.cpp:108`: 비교식 사이의 `&`에 괄호 권고 (`-Wparentheses`).
  - `MAX30105.cpp:292`: 미사용 `originalContents` (`-Wunused-variable`).
  외부 설치 라이브러리는 수정하지 않음.
- 처음에는 제한 환경에서 Arduino15 접근 거부로 빌드가 실패. 승인된 컴파일로 재시도함.
  공유 헤더를 외부 상대 경로로 포함한 첫 시도도 Arduino 임시 스케치 복사 때문에 실패하여,
  동일한 배포 복사본과 일치 검사 방식으로 수정한 후 컴파일함.
- Git의 LF→CRLF 안내는 컴파일 경고가 아님.

## 실행 명령

저장소 루트에서 실행. 도구 경로는 설치 환경에 맞춰 절대 경로로 호출함.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File test/linear_sensor_test/tests/check_linear.ps1
arm-none-eabi-g++ -std=c++11 -Wall -Wextra -fsyntax-only teensy/test_saver_proto.cpp
clang-format -i --style='{BasedOnStyle: LLVM, IndentWidth: 2, ColumnLimit: 100, SortIncludes: false}' <변경 C++ 파일>
clang-format --dry-run --Werror --style='{BasedOnStyle: LLVM, IndentWidth: 2, ColumnLimit: 100, SortIncludes: false}' <동일 파일>
arduino-cli compile --fqbn teensy:avr:teensy41 --warnings all --clean --build-property 'recipe.hooks.postbuild.1.pattern=' --build-path 'C:/Users/Administrator/AppData/Local/Temp/saver-linear-persist-20260930' test/linear_sensor_test
arduino-cli compile --fqbn teensy:avr:teensy41 --warnings all --clean --build-property 'recipe.hooks.postbuild.1.pattern=' --build-path 'C:/Users/Administrator/AppData/Local/Temp/saver-main-persist-build-20260930' 'C:/Users/Administrator/AppData/Local/Temp/saver-eeprom-main-ef47f0fb042b474cb491b10071978d10/saver_teensy'
git diff --check
git status --short
```

메인은 Arduino 폴더명 규칙에 맞게 임시 `saver_teensy/`를 `New-Item`으로 만들고
`saver_teensy.ino`, `saver_proto.h`, `pinmap.h`, `src/`를 `Copy-Item`으로 복사하여 빌드함.
기존 파일 삭제나 저장소 폴더 이동은 하지 않음.

## 가정과 실물 미검증

- 사용자 제공 센서: YwRobot 75mm 10kΩ, 3.3V 공급, A16. 공유 주소에 다른 프로그램이
  기록하지 않고 같은 센서/설치 기준을 유지한다는 조건이 필요함. 이를 자동 감지하지 못함.
- 이전 로그의 보정 수치를 펌웨어에 하드코딩하지 않음. 새 시험 코드를 업로드하고 다시
  ZERO/CAL하여 `saved=1`을 받아야 함. 기존 RAM 보정은 업로드 시 자동 이전되지 않음.
- 업로드, 시리얼 포트 접속, 실제 EEPROM 기록/전원 재인가/스케치 교체 후 보존 시험은 하지 않음.
  물리 플래시의 쓰기 중 전압 저하/전원 차단 결과는 바이트 모의시험으로 보장하지 않음.
- ADC 전압, 센서 단선 검출, 선형성·반복성·거리 정확도, 설치 후 하강 방향, 메인 500Hz 주기,
  모터·안전장치·Jetson 종단간 통신은 미검증. 저장 성공만으로 이를 정상이라고 주장하지 않음.
- 메인의 다른 `[구현]`과 기존 안전/통신 과제는 이번 작업으로 완료되지 않음.
- 커밋·푸시하지 않음.
