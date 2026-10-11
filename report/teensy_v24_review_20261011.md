# Teensy–Jetson 상태머신 v2.4 대조 결과

작성: 2026-10-11, 한건민 / Codex. 연구용 시제품의 코드 대조 및 PC 시험 기록이다.
실물 원점 복귀, 압박 정지, 입력 극성, 연결 안정성 시험은 수행하지 않았다.

## 대조 기준

- 펌웨어 기준: 로컬 `main`의 `6b223ab34a922628c6f61a970adcfa0334bf6191`.
- 검토 작업 브랜치: `codex/teensy-v24-review-20261011`.
- 상대 브랜치: `origin/claude/saver-project-onboarding-xpmdhw`, fetch 후 `fb158e4`.
- 상대 상태표: `protocol/state_machine_v2.json` v2.4, 해당 변경 커밋 `a3211a0`.
- 로컬 상태표는 v2.0이다. 상대 v2.4와 로컬 메인 상태표를 혼동하지 않는다.
- 상대 브랜치와 로컬 메인의 `teensy/` 차이는 fetch 시점에 없었다.

아래의 **맞음**은 확인한 코드 경로가 조건에 맞는다는 뜻이다.
명령 수락이나 상태 문자열만으로 실물 구동을 `tested`로 주장하지 않는다.
미구현 실기 기능은 `planned`로 남긴다.

## 항목별 결과

| 항목 | 판정 | 확인 결과와 제한 |
|---|---|---|
| Jetson S1 명령 순서와 상태별 분기 | 맞음 | 상대 `jetson/saver/init_seq.py`는 PING ACK → GET_STATUS ACK와 tel → IDLE이면 ARM ACK 뒤 HOME, ARMED이면 HOME, HOMED이면 생략한다. FAULT·ESTOP·그 밖은 INIT_ERROR이며 자동 DISARM하지 않는다. 코드 대조이며 Jetson 실기 시험은 아니다. |
| GET_STATUS 응답 형식 | 맞음 / 다름 | 메인은 ACK 뒤 tel을 보내고 `state`, `estop`, `faults`를 포함한다. 그러나 `faults`는 `g_fault` 한 개뿐이며 ESTOP 처리와 버전 오류 처리는 여기에 오류 코드를 기록하지 않는다. 완전한 활성 고장 목록으로 볼 수 없다. |
| 비상정지의 실물 의미 | 미검증 | `checkEstop()`은 HIGH를 활성으로 가정해 읽는다. 실물 접점 극성은 미검증이며, `loop()`에서 명령 처리가 이 검사보다 먼저다. ARM 직전 최신 입력을 다시 검사하는 경로가 없다. |
| IDLE에서 ARM 수락 | 맞음 / 미구현 | IDLE이면 ARMED로 바꾸고 ACK를 보낸다. 다른 상태는 NOT_IDLE 또는 ESTOP_ACTIVE로 거부한다. 리미트·전원·드라이버 등 전체 인터록 검사는 TODO다. 상태 조건의 수락을 안전 점검 통과로 해석하면 안 된다. |
| HOME ACK를 먼저 보내기 | 맞음 | ARMED에서 ACK를 먼저 보내며 ACK의 state는 ARMED다. 뒤에 별도 HOMING_DONE 메시지를 보낸다. |
| 실제 HOME 완료·실패·시간 초과 | 미구현 | ACK 직후 센서나 모터 확인 없이 HOMED와 HOMING_DONE을 만든다. 실제 비동기 원점 복귀, 실패 결과, 시간 초과 처리는 없다. Jetson은 이 메시지를 실제 완료로 오인할 수 있다. |
| PAUSE_CPR | 맞음 / 미구현 | CPR_RUNNING에서 motorsOff → CPR_PAUSED → ACK 순서다. motorsOff는 BTS 주행 PWM/EN만 끈다. 압박 VESC UART 정지는 TODO이며 Serial7도 초기화하지 않는다. 실물 압박 정지가 구현된 것으로 볼 수 없다. |
| 최초 START / 일시정지 뒤 RESUME | 맞음 / 미구현 | START_CPR은 CPR_READY, RESUME_CPR은 CPR_PAUSED를 요구한다. 상대 Jetson은 장치 상태에 따라 START를 RESUME로 고른다. Teensy RESUME는 상태만 검사하며 음압·압박점·목표·드라이버 등 인터록 재검사는 없다. |
| PAUSE 응답 대기 중 STOP 보내기 | 맞음 / 다름 | 상대 Jetson은 명령별 pending ACK를 관리하고 STOP을 PAUSE ACK 뒤에만 보내도록 묶지 않는다. 메인은 정상 명령 경로에서 motorsOff를 호출하지만 버전·seq·중복·ESTOP 검사가 그보다 앞선다. 언제든 식별 즉시 정지하는 예외 경로는 미구현이다. 압박 VESC 정지도 없다. |
| STOP_CPR 이후 상태 | 맞음 | CPR_RUNNING/CPR_PAUSED에서 HOMED로 바꾼 뒤 ACK한다. STOP만으로 FAULT로 바뀌는 코드는 아니다. 실제 헤드가 원점에 복귀했다는 증거도 아니다. |
| X_VENT, X_WHEELS, REALIGN_DONE | 맞음(상대 v2.4) / 다름(로컬 v2.0) | 상대 v2.4에는 X_VENT의 PAUSE_CPR·CPR_PAUSED 재개 조건·STOP → S11이 있다. X_WHEELS, WHEELS_DONE, REALIGN_DONE은 삭제됐다. 로컬 상태표에는 옛 항목이 남아 있다. Teensy는 UI 노드 자체를 처리하지 않는다. |

근거: `teensy/saver_teensy.ino`의 `sendTel()`, `motorsOff()`, `checkEstop()`,
`handleCommand()`, `loop()`; 상대 브랜치의 `init_seq.py`, `teensy_serial.py`,
`protocol.py`, `state_machine_v2.json`.

## ACK와 정지 확인

PAUSE는 코드상 정지 출력 호출과 CPR_PAUSED 전이가 ACK보다 먼저다.
STOP은 정지 출력 호출과 해당 상태 전이가 ACK보다 먼저다.
주기 tel은 기본 50 ms 간격이며 GET_STATUS는 ACK 뒤 tel을 즉시 요청한다.
현재 Serial.println 직접 송신으로 인해 실제 송신 지연은 별도 측정 대상이다.

현재 ACK나 CPR_PAUSED는 **압박 모터가 실제로 멈췄다는 증거가 아니다**.
VESC 정지 출력과 실제 정지 확인이 없기 때문이다.
상대 브리지의 `pause_stop_verified=false` 차단은 실물 시험 전까지 유지한다.

또한 중복 seq에는 원래 거부 ACK를 저장해 재송신하지 않고 성공 ACK를 새로 만든다.
ARM 거부 뒤 같은 seq 재전송을 수락으로 오인할 수 있으므로 최초 ACK 유실 시험이 필요하다.

## S13의 [대기로] 제안

`BACK_TO_IDLE`의 정상 목적지를 S2 대신 **S1로 변경하는 데 동의**한다.
DISARM 뒤에는 장치가 IDLE이므로 새 GET_STATUS를 바탕으로 ARM과 HOME을 다시 해야 한다.

다만 기존 S13 redirect는 유지해야 한다. 음압이 해제되지 않았거나 해제 상태를 모르면
S12에서 해제 확인을 기다리고, 확인 후에 S1로 돌아간다. 잔류 음압 상태에서 초기화를
강제로 통과시키거나 ARM/HOME을 보내지 않는다.

현재 Teensy에는 v1.1의 rearm_lock, vac_released, 해제 확인 및 DISARM 음압 유지 상태
처리가 없다. DISARM은 FAULT에서도 IDLE로 바꾸므로 기존 고장을 우회할 수 있다.
따라서 위 합의는 protocol 변경 방향에 대한 동의이며 실기 동작 완료 보고가 아니다.
protocol 파일 수정과 양쪽 시험은 상대 담당자가 반영한 뒤 함께 대조한다.

## 이번 변경

- `teensy/reference/README.md`: 옛 "UI 화면 02(바퀴 내리기)" 대응 추정을 삭제하고
  v2.4에서 삭제된 단계와 X_MOVE/X_PIN 표시 이름을 설명했다.
- 이 검토 기록을 추가했다.
- 메인 펌웨어, protocol, Jetson, UI 동작 코드는 수정하지 않았다.
- 기존 사용자 미커밋 센서·시험 코드 변경은 이번 변경에 포함하지 않는다.

## 검사 결과

- `teensy/test_saver_proto.cpp`: **PC 네이티브 실행 26개 통과 / 실패 0**, 종료 코드 0.
  PC용 g++가 없어서 공식 배포의 휴대용 Zig 0.13.0 C++ 컴파일러를 임시 폴더에서
  사용했다. 배포 SHA-256을 검증했으며 `-std=c++11 -Wall -Wextra -Werror`로 컴파일했다.
  첫 빌드에서 Zig 외부 런타임 libunwind의 경고가 있었으며, 시험 소스는 통과했다.
- ARM GCC `test_saver_proto.cpp` C++11 문법 검사: 통과.
- `teensy/tests/check_pinmap.ps1`: 19개 컴파일타임 assertion와 소스 가드 통과.
- `test/linear_sensor_test/tests/check_linear.ps1`: 87개 컴파일타임 assertion와 공유 헤더,
  EEPROM 읽기 전용 경로 검사 통과.
- 상대 v2.4 JSON: 초기화 절차·재개 조건·삭제 항목·S13 redirect 등 14개 소스 대조 통과.
- 메인 Arduino CLI `teensy:avr:teensy41 --warnings all`: 컴파일·링크 통과.
  외부 SparkFun 라이브러리의 기존 경고 4개는 남아 있다.
- `tools/consistency_audit.py`: 불일치 없음.
- `git diff --check`: 통과.
- `tools/patent_scan.py`: 종료 코드 0. 비공개 차단 용어 목록이 없어 경고 검사만
  수행됐으며, 전체 차단 검사가 완료됐다고 주장하지 않는다.

위 결과는 PC/컴파일 검증이다. `test_saver_proto.cpp`는 파서·생성기·seq 기억 시험이며
HOME 동작, ARM 인터록, 실제 압박 정지나 재개, USB 실기 타이밍을 시험하지 않는다.
장치 포트를 열거나 펌웨어를 업로드하지 않았다.

주요 실행 명령(저장소 루트, Zig 실행 파일은 임시 배포 경로 사용):

```powershell
zig c++ -std=c++11 -Wall -Wextra -Werror teensy/test_saver_proto.cpp -o <임시폴더>/test_saver_proto.exe
& <임시폴더>/test_saver_proto.exe
powershell.exe -NoProfile -ExecutionPolicy Bypass -File teensy/tests/check_pinmap.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File test/linear_sensor_test/tests/check_linear.ps1
arduino-cli compile --fqbn teensy:avr:teensy41 --warnings all --build-property 'recipe.hooks.postbuild.1.pattern=' <임시복사본/saver_teensy>
& "$env:USERPROFILE/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe" tools/consistency_audit.py
git diff --check
```

## 연결 전 판단

현재 소스만으로 ARM/HOME 자동 전송 뒤 실기 초기화가 완료된다고 회신할 수 없다.
실제 모터 전력을 분리한 USB 메시지 시험과 실물 구동 시험을 구분해서 진행해야 한다.
원점 센서 기반 완료·실패 판정, VESC 정지 및 재개 인터록 구현·검증은 `planned`다.
