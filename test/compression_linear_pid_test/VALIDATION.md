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
