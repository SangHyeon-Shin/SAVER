# 선택형 통합판 소프트웨어 검증 — 2026-10-23

## 결과와 범위

**implemented / tested(software)**: 새 통합 스케치를 PC에서 mock USB/UART/ADC/I²C로 실행하고
Teensy 4.1 대상으로 컴파일했습니다. 업로드, 포트 연결, 실제 센서 읽기, 모터 구동은 하지 않았습니다.
실물 배선·정지거리·전류·힘·산소포화도 정확도를 검증한 결과가 아닙니다.

| 시험 | 결과 |
|---|---|
| 실제 스케치, 배포 기본 설정 | 65개 검사 통과. 미확인 압박 설정에서는 RUN 불가 |
| 실제 스케치, synthetic 설정 fixture | 89개 검사 통과. 시험 전용 비영점 설정으로 PID/정지 경로 실행 |
| Teensy USB 프로토콜 회귀 | 26/26 통과; 프로토콜 파일 변경 없음 |
| Teensy 4.1 Arduino CLI 컴파일 | 성공. 통합 작성부 경고 없음, 외부 SparkFun 라이브러리 경고 4개 |
| 기존 압박 설정/제어/코덱/보정 회귀 | bench 77, current 39, depth 71, VESC 36, capture 34개 compile-time assertion 통과 |
| 기존 로드셀 회귀 | 58개 compile-time assertion + transport mock/source 검사 통과 |
| 기존 PPG 회귀 | contact 21, diagnostics 32, synthetic signal 19개 compile-time assertion 통과 |
| 기존 음압 회귀 | 90개 compile-time assertion 및 실제 스케치 mock harness 구문 검사 통과 |
| 공유 헤더 동결본 | 11개 모두 원본과 내용 일치(CRLF/마지막 개행만 정규화) |
| 확정값 대조 | `tools/consistency_audit.py` 불일치 없음 |

외부 경고는 SparkFun MAX3010x 1.1.2의 `spo2_algorithm.h` 미사용 정적 배열 2개,
`heartRate.cpp`의 비교·비트연산 괄호, `MAX30105.cpp` 미사용 지역변수입니다.
외부 코드를 바꾸거나 경고를 숨기지 않았습니다. HR/SpO₂의 실제 알고리즘은 펌웨어 빌드에 연결했고,
PC I/O 통합 mock은 생체 값을 만들어 내지 않도록 알고리즘 결과를 무효로 고정합니다.
신호 품질 순수 C++ 회귀는 별도 synthetic 파형 검사입니다. 실측 RAW 재생은 `LogPath`가 필요한
별도 조사 스크립트이며 이번 요청에 새 RAW 파형이 없어 실행 완료로 계산하지 않았습니다.

## 통합 실행 검사의 주요 항목

- 부팅/USB 재연결 NONE, 모터 자동 시작 없음, 비선택 I²C 취득 없음.
- 모드 전환에서 모터 정지/ARM 해제, 음압 모드에서 VESC 조회·비영점 요청 없음.
- 1/2번 독립 및 동시 음압 시작, SET 변경/0 정지, 잘못된 모드/명령의 정지.
- ERR LOW 래치/250 ms 안정 후 명시 clear, ADC rail 정지, 구버전 시간 인자 거부.
- 부분 명령 timeout/늦은 suffix 폐기, USB 재연결 때 대기 명령 폐기.
- 리니어 ZERO/CAL, NAU7802 초기화/320샘플 TARE·질량 보정, 잘못된 재보정의 이전 스케일 무효화.
- synthetic 설정에서만 압박 PID 비영점 요청, 스크립트 위치 입력을 이용한 왕복 궤적 종료.
- VESC 응답 상실·제어 deadline·리니어 rail·로드셀 I²C 실패 시 0 A 요청 및 재시작 차단.
- PPG 시작/shutdown 확인, PPG shutdown 미확인 시 모터 모드 진입 차단, 미탐지 PPG의 로컬 실패.
- UART 송신 막힘의 정지 우선 재시도, USB 로그 포화 중에도 오류 감시, 부분 송신 시 줄 비혼합.

위치 궤적 입력은 모터 plant나 생체역학 해석이 아닙니다. 물리 PID 안정성/추종/방향을 입증하지 않습니다.
시험 fixture는 `tests/fixture_config.h`에서만 활성화하며 Arduino 빌드에서는 포함하지 않습니다.
실기 설정은 `src/bench_config.h`에 남은 미설정/미검증 값을 실측 후 반영해야 합니다.

## 실행 명령

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File test/selectable_system_bench_test/tests/check_integration.ps1

# 설치된 Teensy ARM 컴파일러를 사용하는 기존 회귀
powershell -NoProfile -ExecutionPolicy Bypass -File test/compression_linear_pid_test/tests/check_bench.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File test/compression_linear_pid_test/tests/check_current_control.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File test/compression_linear_pid_test/tests/check_depth_control.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File test/compression_linear_pid_test/tests/check_vesc_link.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File test/compression_linear_pid_test/tests/check_calibration_capture.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File test/reaction_force_logger/tests/check_logger.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File test/max30102_sensor_test/tests/check_contact.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File test/tests/check_combined.ps1

# 아래 명령의 실행 파일 경로는 로컬 설치 경로 사용
arm-none-eabi-g++ -std=c++14 -Wall -Wextra -Werror -fsyntax-only test/max30102_sensor_test/tests/test_signal.cpp
zig c++ -std=c++11 -Wall teensy/test_saver_proto.cpp -o <TEMP>/protocol.exe
<TEMP>/protocol.exe
python tools/consistency_audit.py

arduino-cli compile --fqbn teensy:avr:teensy41 --warnings all --build-property 'recipe.hooks.postbuild.1.pattern=' --build-path <TEMP>/unique-build test/selectable_system_bench_test
```

설치된 CLI/Teensy 패키지를 읽기 위해 Windows 샌드박스 권한 승격을 사용했습니다.
컴파일 전용이며 업로드/시리얼 통신/출력 조작을 하지 않았습니다.

포맷은 새 제어·I/O·시험 소스에만 clang-format (LLVM, 2 spaces, column 100,
SortIncludes=false)을 적용했습니다. `src/`는 동결 복사본을 유지했고 기존 파일은 수정하지 않았습니다.

## 동결 헤더 출처

| 통합판 `src/` | 원본 파일 |
|---|---|
| bench_logic.h, pressure_conversion.h | `test/` 같은 이름 |
| bench_config.h, bench_guard.h, current_control.h, depth_control.h, vesc_link.h | `test/compression_linear_pid_test/` 같은 이름 |
| logger_logic.h, nau7802_reader.h | `test/reaction_force_logger/` 같은 이름 |
| optical_window.h, ppg_signal.h | `test/max30102_sensor_test/` 같은 이름 |

**assumed / planned**: 사용자의 LM 실물 동작 보고와 기존 배선을 전제로 합니다.
이 통합판으로 실측한 결과가 아닙니다. 음압 고출력 시 자동 꺼짐은 미해결입니다.
센서 전원·ERR/RUN 전압, VESC 설정/timeout, 기계적 가동 범위, 하중 측정, 정지거리,
모터와 센서 간 간섭/EMI 시험은 실물에서 별도로 필요합니다.
