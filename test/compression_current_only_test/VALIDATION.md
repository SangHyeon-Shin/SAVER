# 전류 지령 1초 코드 검증 — 2026-10-15

사용자가 요청한 최소 송신형입니다. 새 폴더 `test/compression_current_only_test/`만 추가했습니다.
기존 PID·단독 듀티 시험·메인 펌웨어·protocol은 이번 작업에서 수정하지 않았습니다.
중간에 작성한 ARM/LIMIT형 보조 코드는 최종본에서 제거했습니다. 업로드 파일은 `.ino` 하나입니다.

## 소프트웨어 검사 (`tested`)

- Teensy 4.1 / Arduino CLI / `--warnings all`: 컴파일·링크 통과, 오류 0·경고 0.
- 실제 `.ino`를 포함한 PC 모의 USB/UART/시간 시험: **63 checks, 0 failures**.
- 실제 스케치와 모의 헤더의 ARM C++14 문법 검사 및 전류 송신 전용 소스 검사 통과.
- CRC 독립 벡터, 1 A → signed 1000 mA 패킷, 부호/소수/잘못된 숫자, 정확한 1초 지령 종료,
  20ms 갱신, STOP·잘못된 명령·실행 중 재명령 정지, USB 단절/재연결 시 재실행 금지,
  송신 버퍼 부족 시 0 A 우선 재시도, 명령 길이/시간 제한, millis 순환, USB 출력 정체를 검사했습니다.
- clang-format `--dry-run --Werror`, 새 파일 후행 공백 검사, `tools/consistency_audit.py` 통과.

주요 실행 명령(저장소 루트):

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File test/compression_current_only_test/tests/check_current_pulse.ps1
$taskDir = Join-Path $env:TEMP 'saver-v24-review-zig-0.13.0'
$env:ZIG_GLOBAL_CACHE_DIR = Join-Path $taskDir 'global-cache'
$env:ZIG_LOCAL_CACHE_DIR = Join-Path $taskDir 'local-cache'
& "$taskDir/zig-windows-x86_64-0.13.0/zig.exe" c++ -std=c++14 -Wall -Wextra -Werror -DARDUINO_TEENSY41 -I test/compression_current_only_test/tests/stubs test/compression_current_only_test/tests/test_current_pulse.cpp -o "$taskDir/test_current_pulse.exe"
& "$taskDir/test_current_pulse.exe"
& 'C:/Program Files/Arduino IDE/resources/app/lib/backend/resources/arduino-cli.exe' compile --fqbn teensy:avr:teensy41 --warnings all --build-property 'recipe.hooks.postbuild.1.pattern=' --build-path "$env:TEMP/saver-current-pulse-20261015" test/compression_current_only_test
& 'C:/Users/Administrator/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe' tools/consistency_audit.py
```

Arduino CLI는 설치된 패키지에 접근 가능한 환경에서 실행했고 Loader 후처리를 비웠습니다.
펌웨어 업로드·시리얼 포트 열기·전원 인가·실제 모터 구동은 하지 않았습니다.

## 미검증·가정

- 1초는 **Teensy의 비영 전류 지령 송신 구간**입니다. 실제 전류·회전·정지 시각을 보장하지 않습니다.
- 이 최소형은 VESC 응답/고장/단선을 수신 판정하지 않습니다. UART 단선·Teensy 정지 때는
  0 A가 전달되지 않을 수 있어 VESC 자체 timeout과 독립 물리 전원 차단이 필요합니다.
- 상/하 리미트·보조접점·위치/힘/속도 보호가 없습니다. 기구에서 분리한 고정 무부하 모터만 대상입니다.
- 15 A는 이전 LM 명령 문법에서 가져온 파싱 상한이며 실제 안전 전류가 아닙니다.
  전류는 매 PULSE 명령에서 사용자가 명시적으로 선택합니다. 기본 구동 전류는 없습니다.
- 방향·VESC 설정/버전·UART 배선·전기적 여유·회생·정지거리·발열·하드웨어 timeout은 실물 미검증입니다.

## 15 A 입력 상한 제거 — 2026-10-15

사용자의 이 폴더 상한 제거 요청을 **기존 15 A 전류 입력 상한 제거**로 해석해 반영했습니다.
1초 고정 펄스 시간은 변경하지 않았습니다. 앞 절의 15 A 문법 제한 설명은 이전 버전 기록입니다.

- `GRAMMAR_MAX_MILLI_A=15000`과 정수부 `15` 제한을 제거했습니다. 새 운전 전류 상한은
  임의로 정하지 않고, 기존 signed-int32 mA 패킷 표현 범위/넘침 검사만 유지했습니다.
  음수 최솟값 변환도 부호 있는 정수 넘침 없이 처리합니다. 이 표현 범위는 안전 전류나
  실물에서 달성 가능한 전류가 아닙니다. 모의 시험의 극단값을 실물에 보내면 안 됩니다.
- 시작 메시지는 `current_pulse_1s_v2_wire_range`와 `sketch_current_cap=NONE`으로 바꿨습니다.
  기본 전류·자동 시작은 추가하지 않았고, VESC 설정 명령은 송신하지 않습니다.
- 1초 종료, STOP, USB 단절/재연결, 잘못된 명령, 버퍼 부족 시 0 A 요청과 우선 재시도,
  실행 중 재명령 정지 및 피드백 미검증 표시는 유지했습니다.
- 작업 시작 시 이미 파일에 있던 `TX_MS=100`(100ms) 변경은 보존했습니다. 이 작업으로
  송신 주기를 변경하지 않았습니다. README와 모의 시험은 현재 100ms 설정에 맞췄습니다.
- 수정 범위는 이 폴더의 `.ino`, `README.md`, `tests/test_current_pulse.cpp`,
  `tests/check_current_pulse.ps1`, 이 기록입니다. 다른 시험/운용 코드나 protocol은 수정하지 않았습니다.

검증 수준: `tested` — 소프트웨어/모의 검사만 수행했습니다. 위 주요 명령을 재실행했습니다.

- `tests/check_current_pulse.ps1`: 실제 `.ino` + 모의 헤더의 ARM C++14 검사와 소스 보호 검사 통과.
- 위 Zig 명령으로 PC 모의 시험 빌드/실행: **80 checks, 0 failures**.
  이전 15 A 초과 양/음수와 mA 단위, signed-int32 양/음수 경계·초과·매우 긴 숫자·넘침,
  실제 송신 패킷의 부호 보존, 넓힌 입력에서도 1초 후 0 A 요청을 검사했습니다.
  극단 전류는 모두 mock 테스트 데이터이며 장치로 송신하지 않았습니다.
- 위 Arduino CLI 명령으로 Teensy 4.1 컴파일·링크: 오류 0, 경고 0.
  설치 도구 접근 권한을 사용했고 업로드 후처리는 비활성입니다.
- clang-format 검사, 파일별 후행 공백 검사, `git diff --check`, `tools/consistency_audit.py` 통과.
  이 폴더는 기존부터 Git untracked 상태여서 별도 파일 공백 검사도 수행했습니다.
- [공식 VESC 명령 처리](https://github.com/vedderb/bldc/blob/master/comm/commands.c)의
  `COMM_SET_CURRENT`가 signed-int32 값을 1000으로 나눠 A로 해석하는 점을 재확인했습니다.

실물 구동·업로드·시리얼 포트 접근은 하지 않았습니다. VESC 자체 전류/전압/회생/timeout
제한은 변경하거나 실측 검증하지 않았습니다. 현재 사용자의 기구 끝 근처 이동부에 연결해
시험하는 용도가 아니며, 기존과 같이 기구에서 분리해 고정한 무부하 모터 전용입니다.

## 수동 정지 RUN 추가 — 2026-10-16

사용자의 "시간을 늘리고 직접 멈추기" 요청을 기존 `PULSE`를 보존하고
별도의 `RUN signed_A` 수동 유지 명령을 추가하는 것으로 반영했습니다.
`RUN`에는 자동 작동시간 제한이 없고 `STOP` 또는 기존 오류/USB 단절 처리로 0 A를 요청합니다.
운전 중에는 시험대를 떠나지 않으며, 기구에서 분리한 고정 무부하 모터만 대상입니다.

- 부팅 표시는 `current_manual_stop_v3`입니다. 핀 28/29, UART 115200, 송신 갱신 100ms,
  전류 패킷·넘침 검사·기존 sketch 입력 상한 제거 상태는 보존했습니다.
- `PULSE`는 기존대로 1초 종료합니다. 부팅·재연결·정지·오류 복구 후 자동 시작하지 않습니다.
- `STATUS`에 `run_mode=MANUAL_STOP/PULSE_1S/STOPPED`를 추가했습니다.
  `remaining_ms`는 실행 중인 1초 PULSE에만 출력하며 수동 RUN에는 가짜 잔여시간을 보내지 않습니다.
- 실행 중 다른 RUN/PULSE는 정지시키고 거부합니다. 즉시 전류 변경·반전하지 않습니다.
- USB 단절, 잘못된 명령/길이 초과/부분 명령 시간 초과, UART 버퍼 부족 시 0 A 우선 요청과
  버퍼 복구 시 0 A 재시도를 유지합니다. 정지·회전·전류에 대한 VESC 피드백은 읽지 않습니다.
- VESC 자체 제한·통신 timeout·물리 전원 차단은 이 스케치와 별개로 검증해야 합니다.
  수동 RUN은 위치/힘/온도/전류 피드백 보호나 실제 정지를 추가한 기능이 아닙니다.

소프트웨어 검증 결과 (`tested` — 모의 USB/UART/시간 및 컴파일, 실물 시험 아님):

- `tests/check_current_pulse.ps1`: 실제 스케치·mock 헤더 ARM C++14 검사와 소스 보호 검사 통과.
- 기존 80개 및 새 수동 모드 시험을 포함한 네이티브 실행 **122 checks, 0 failures**.
  1초·30초·10분·1시간 이후의 수동 유지(mock 시각), STOP, 양·음 전류, PULSE 복귀,
  USB 단절/재연결, 재명령/반전 거부, 잘못된 입력, pending zero, UART/USB 정체,
  부분 명령 시간 초과·길이 초과, millis 순환, 수동 모드의 잔여시간 필드 생략을 검사했습니다.
  장시간 항목은 mock 시각을 이동한 것이며 실제 장시간 모터 운전이 아닙니다.
- `teensy/test_saver_proto.cpp`의 기존 네이티브 시험 **26/26 통과**. protocol은 수정하지 않았습니다.
- Teensy 4.1 Arduino CLI 컴파일·링크: 오류 0, 경고 0. 설치 패키지 접근 권한을 사용했고
  `recipe.hooks.postbuild.1.pattern=`으로 Loader 후처리를 비웠습니다. 업로드하지 않았습니다.
- clang-format 검사와 `tools/consistency_audit.py` 통과. 폴더가 untracked 상태이므로
  변경 파일에는 별도 UTF-8·후행 공백 검사도 수행합니다.

이번 실행 명령은 앞 절과 같은 컴파일러/검사 명령을 사용하되, 출력 실행 파일과
Arduino 빌드는 새 임시 경로에 만들었습니다. 네이티브 프로토콜 회귀 명령도 추가했습니다.

```text
powershell.exe -NoProfile -ExecutionPolicy Bypass -File test/compression_current_only_test/tests/check_current_pulse.ps1
zig c++ -std=c++14 -Wall -Wextra -Werror -DARDUINO_TEENSY41 -I test/compression_current_only_test/tests/stubs test/compression_current_only_test/tests/test_current_pulse.cpp -o <임시폴더>/test_current_manual.exe
<임시폴더>/test_current_manual.exe
zig c++ -std=c++11 -Wall teensy/test_saver_proto.cpp -o <임시폴더>/test_saver_proto.exe
<임시폴더>/test_saver_proto.exe
arduino-cli compile --fqbn teensy:avr:teensy41 --warnings all --build-property "recipe.hooks.postbuild.1.pattern=" --build-path <임시빌드폴더> test/compression_current_only_test
clang-format --dry-run --Werror <이번 수정 스케치와 시험 C++>
python tools/consistency_audit.py
git diff --check
```

수정 파일은 이 폴더의 `.ino`, `README.md`, `VALIDATION.md`,
`tests/test_current_pulse.cpp`, `tests/check_current_pulse.ps1` 다섯 개입니다.
메인 펌웨어·음압 시험·protocol·VESC 설정은 변경하지 않았습니다.
