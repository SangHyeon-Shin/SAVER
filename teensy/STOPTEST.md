# 기구 분리 모터 정지 시험 — 2026-10-23

현재 teensy.ino / saver_teensy.ino 펌웨어의 로컬 시험 경로다.
**왕복·압박 시연 코드가 아니다. 벨트/커플링을 분리해 압박 기구를 움직일 수 없는
모터만 고정하여 시험한다.** 비상정지는 손이 닿는 곳에 두고 음압 모터는 끈다.
UNCOUPLED는 작업자의 분리 확인 선언이지, 펌웨어가 분리 상태를 검출한 결과가 아니다.

## 업로드와 첫 펄스

1. 모든 모터 전원을 끄고 모터와 압박 기구의 기계적 연결을 분리한다.
   회전축의 느슨한 부품을 제거하고 모터를 고정한다. 사람·마네킹에 접촉하지 않는다.
2. Arduino IDE에서 같은 폴더의 teensy.ino를 열어 **프로젝트 전체**를 업로드한다.
   Teensy 4.1 / USB Type Serial. src/를 포함한 폴더를 그대로 유지한다.
   현재 INFO의 fw는 manual-bench-20261023-motion-plan이다.
3. 리니어 센서 3.3 V / GND / SIG→A16(물리 40), VESC UART RX28/TX29와
   신호 GND를 기존처럼 연결한다. UART는 Teensy RX←VESC TX, Teensy TX→VESC RX다.
   전원 배선 변경은 전원을 끄고 한다. VESC/블로워의 보조 +5 V 출력을 USB 5 V에 묶지 않는다.
4. 분리·고정을 확인한 뒤 VESC 전원을 켜고 시리얼 모니터 115200 / Newline에서
   아래를 **한 줄씩** 보낸다. 부팅·포트 재연결만으로는 회전하지 않는다.

       STOP
       VAC OFF
       PPG OFF
       STREAM OFF
       CP STOPTEST UNCOUPLED

1초 이상 기다린 뒤:

    CP STOPTEST STATUS
    CP STOPTEST ARM
    CP STOPTEST PULSE

- **+1 A 지령, 최대 1000 ms 한 번**이다. 이전 사용자 PULSE 1 방향 확인 자극을
  재사용했으며, 안전성 검증된 운전 정격이나 압박 설정이 아니다.
  음전류 하강/양전류 상승의 설치 기구 왕복과 혼동하지 않는다.
- 출력이 작아 모터가 회전하지 않으면 정지 시험 성공으로 세지 않는다. 임의로 전류를 높이지 않는다.
- ARM은 10초 안에 한 번만 사용한다. 다음 펄스에는 다시 ARM이 필요하다.
  반복 횟수·전류·시간 인자는 받지 않는다.
- 정상 펄스 종료 시 STOPTEST_ZERO,elapsed_ms=1000... 뒤
  STOPTEST_PULSE_DONE이 나온다. 0 전류는 **토크 해제 요청**이며 즉시 제동이나 축 고정이 아니다.
- 이 모드에는 ZERO/CAL, 로드셀·압력센서가 필요 없다. 위치/힘 제어는 동작하지 않는다.
  VESC 신선도·고장·전압·전류·FET 온도, 입력 설정, ADC 레일·제어 deadline 감시는 유지한다.
  부팅 후 NAU7802 미연결 오류가 있어도 시험 진입은 가능하다.

## 네 가지 시험 — 각각 새 ARM/PULSE

먼저 1초 후 자연 종료와 자동 재시작 없음부터 확인한다. 이후 한 항목씩 시험한다.
회전/출력이 남거나 예상과 다르면 **물리 비상정지로 전원을 차단하고 다음 시험을 하지 않는다.**

| 항목 | 조작 | 남길 결과 |
|---|---|---|
| STOP | PULSE 직후 준비해 둔 STOP을 보낸다 | STOPTEST_ZERO의 elapsed_ms가 1000 미만인지, 실제 축이 더 구동되지 않는지 |
| 리니어 단선 | 먼저 모터 전원 OFF에서 쉽게 분리할 **SIG만** 준비한다. 다음 펄스 중 SIG를 분리하며 VCC/GND나 인접 단자는 건드리지 않는다 | LINEAR_ADC_RAIL_OR_INVALID, 0 전류 요청, 실제 출력 해제 |
| UART 응답 상실 | 먼저 모터 전원 OFF에서 Teensy RX28←VESC TX 신호만 쉽게 분리하도록 준비한다. 다음 펄스 중 이 신호만 분리한다. TX29→VESC RX와 신호 GND는 유지한다 | 마지막 정상 응답 후 300 ms 신선도 만료로 정지 요청, 실제 출력 해제 |
| USB | 먼저 모니터를 닫아 **Teensy가 켜진 상태의 DTR 단절**을 시험하고 다시 연다. 그다음 분리 모터에서만 USB 케이블 분리를 별도로 시험한다 | 재연결 후 자동 재시작 없음. USB가 유일한 Teensy 전원이면 정지 명령 송신은 불가능하므로 VESC 자체 timeout으로 출력이 풀리는지 물리적으로 관찰 |

USB 전원 상실 또는 Teensy TX29→VESC RX 단절은 소프트웨어 0 지령이 VESC에 도달하지
못하는 시험이다. **이 경우 1초 펄스 제한도 보장되지 않는다.** VESC timeout 설정은 아직
읽지 못했으므로, 출력이 남으면 즉시 비상정지로 차단한다. 계속 회전하는 축에 손을 대지 않는다.
신호 GND는 시험 중 빼지 않으며 24 V·모터선·상선은 활선 상태에서 분리하지 않는다.

ADC 단선 시험 중에는 A16에 시험 전용 내부 pulldown을 건다.
이 바이어스는 포텐셔미터 값을 바꾸므로 정상 위치 보정값을 쓰지 않는다.
끊은 SIG가 실제로 레일까지 떨어지는지는 배선/RC/기판 상태에 따라 실물 확인이 필요하다.
그럴듯한 값으로 고정되는 고장·잘못된 스케일 등 모든 센서 고장을 검출하는 시험은 아니다.

## 고장 뒤와 종료

VESC 전원을 끄고 시험 신호를 복원한다. 분리 상태를 유지한 채 다시 전원을 켜고,
VESC 고장 해소·축 정지 후 1초 이상 기다린다:

    CP STOPTEST CLEAR
    CP STOPTEST STATUS
    CP STOPTEST ARM
    CP STOPTEST PULSE

CLEAR는 현재 고장 원인이 사라지고 신선한 ERPM=0이 250 ms 유지돼야 허용한다.
고장 초기화가 회전을 시작하지 않는다. 외부 비상정지 보조접점은 미연결이므로 펌웨어가
실제 E-stop 접점 상태나 전원 차단 성공을 인증하지 않는다.

USB 단절 후에는 시험 모드/분리 선언도 취소되므로 새 CP STOPTEST UNCOUPLED부터
진행한다. 고장 잠금이 남아 있으면 원인을 확인하고 기존 고장 해제 절차를 사용한다.

끝나면:

    STOP
    CP STOPTEST OFF

A16 입력 설정을 원래대로 복원한다. **시험 진입/종료 시 리니어 RAM 보정을 무효화**하므로
이후 원점 ZERO / 실제 거리 CAL을 다시 해야 한다. EEPROM 보정 영역은 쓰지 않는다.
펌웨어 업로드 자체로 로드셀 TARE/FORCE_CAL 등 RAM 설정도 초기화된다.
시험 결과만으로 기존 CP RUN/Jetson/통합 구동 잠금을 자동 해제하지 않는다.
다른 로컬 기능과 원격 제어 전환은 시험 모드를 끄기 전에는 거부한다.

## 무엇을 검증했는가

- implemented: 명시적 로컬 진입, 단발 펄스, 기존 UART/0 전류 정지 경로 공유,
  시험 한정 ADC 바이어스와 복원, 기존 정상 구동 설정 불변.
- simulated: 모의 USB/UART/ADC/I²C에서 단절·레일·시간 초과·송신 막힘·로그 막힘,
  재연결 및 자동 재시작 방지. 실제 MCU 무전원 시 모터 동작을 모사한 것은 아니다.
- planned: 이 문서의 실물 시험. 실제 정지 시간/거리·모터 자체 timeout·기구 연결 후
  폐루프 추종·힘 상한·설치 회로의 고장 대응은 별도 검증이다.

standstill_verified=0 및 hardware_test_passed=0은 의도된 표시다. UART 송신 큐 수락이나
PC 모의 시험을 실물 정지 통과로 바꾸어 기록하지 않는다. 각 시험의 전체 로그와
실제 회전/정지 관찰, USB 단절이 DTR인지 전원 상실인지 구분해서 전달한다.

## PC 검증 기록

2026-10-23, 업로드·실물 구동 없이 실행:

- teensy/tests/demo/check_main.ps1: 기본 설정 457개 + 합성 fixture 564개 통과.
- teensy/test_saver_proto.cpp: 네이티브 C++11 시험 26개 통과.
- teensy/tests/check_pinmap.ps1: 19개 assertion 및 소스 배정 검사 통과.
- teensy/tests/demo/check_ppg_diag_replay.ps1: 실제 Maxim 라이브러리 합성 입력 14개 통과.
- tools/consistency_audit.py: 확정값 불일치 없음.
- arduino-cli compile --fqbn teensy:avr:teensy41 --warnings all
  --build-property recipe.hooks.postbuild.1.pattern= teensy: 컴파일·링크 통과.
  외부 SparkFun 라이브러리의 기존 경고 4개는 남아 있다. 자체 수정 코드 경고는 없었다.

재실행은 저장소 루트에서 PowerShell -NoProfile -ExecutionPolicy Bypass -File 뒤에
해당 ps1 경로를 지정한다. 프로토콜 시험은 C++11 지원 네이티브 컴파일러로 빌드·실행한다.
