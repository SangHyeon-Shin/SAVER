# 한건민 전달 — Teensy 4.1 ↔ Jetson Nano 통신

## 핀 충돌 정리 (2026-10-02)

- 음압 모터 2 RUN **34→32**, EN **35→37**. PPG 보호 핀과 음압+압력 시험도 함께 수정했습니다.
- WT901C-TTL은 **Serial8 RX=34 / TX=35** 예약입니다. 센서 TX→34, 센서 RX←35로 교차 연결합니다.
  메인에서는 아직 수신/baud/방향 계산을 구현하지 않았고 두 핀을 입력 high-Z로만 둡니다.
- NAU7802는 독립 **Wire1 SDA=17 / SCL=16**, MAX30102는 기존 **Wire SDA=18 / SCL=19**입니다.
  반력 기록 시험의 초기화·읽기·재연결은 Wire1로 옮겼지만 메인 로드셀 취득은 미구현입니다.
- 주행 모터 1 엔코더 A/B **16/17→11/12**. 기본 외부 SPI는 사용할 수 없지만 내장 microSD SDIO는 별개입니다.
- 사용자 확인: 모터 Hall은 VESC에만 연결합니다. Teensy **39**와 **23/A9**는 미연결 예비,
  **13**은 미배정입니다. 원시 로드셀 브리지를 A9에 직결하지 않습니다.
- 새 펌웨어와 옛 배선을 혼용하지 마세요. USB·모터 전원을 모두 끄고 재배선 후 검증해야 합니다.
  핀 선언/컴파일은 입력 전압·실물 센서 동작·모터 정지 성능 검증이 아닙니다.

핀 회귀 검사: `powershell.exe -NoProfile -ExecutionPolicy Bypass -File teensy/tests/check_pinmap.ps1`.

## 리니어 영구 보정 추가 (2026-09-30)

**설치 전에는 먼저 리니어 센서를 ZERO/CAL로 보정하고, 설치 후에는 영점·실측 거리·방향을 다시
확인하세요.** 고정 위치/기구 연결/기준점이 바뀌면 재보정합니다. 설치 후 하강이 양수인지도 확인해야 합니다.

- `test/linear_sensor_test/linear_sensor_test.ino`의 `build=calibration_eeprom_v3`에서 보정하면
  성공한 CAL 값을 EEPROM 4096~4135에 자동 저장합니다. `saved=1` 응답을 기다립니다.
- 메인 `saver_teensy.ino`는 부팅 시 같은 형식/CRC/범위를 검사해 **읽기만** 합니다. EEPROM 쓰기,
  새 JSON 명령/이벤트/필드, 모터 구동 기능을 추가하지 않았습니다.
- 현재 위치는 `g_s.linear_position_mm`, 유효 여부는 `linear_position_ok`, 두 보정점 사이 여부는
  `linear_within_cal_span`에 취득합니다. 없거나 손상된 저장값/잘못된 ADC/rail이면 유효 플래그를
  false로 하고 마지막 숫자는 덮어쓰지 않습니다. 부팅 위치를 자동 영점으로 삼지 않습니다.
- **`cp_depth_mm`는 명세 §6의 직전 사이클 최대 압박 깊이입니다.** 현재 위치를 이 필드로
  대신 송신하지 않으며 사이클 경계/원점/최대 깊이 산출이 구현되기 전에는 생략합니다.
  새 실시간 위치 필드가 필요하면 신상현과 프로토콜 합의가 선행되어야 합니다.
- ADC 12bit/평균 16회와 A16 입력 설정을 보정 스케치와 맞췄습니다. 평균 설정은 다른 ADC
  입력에도 적용되므로 전체 제어 루프의 실측 시간은 재검증해야 합니다.
- 공유 원본은 `src/saver_linear/`입니다. 메인 스케치 업로드용 폴더에도 `src/` 전체를 포함합니다.
  시험 스케치의 배포 복사본과 동일한지 자동검사합니다. EEPROM 지우기/복구/다른 스케치 덮어쓰기는
  보정을 없앨 수 있습니다. 다른 Teensy로 교체해도 다시 보정해야 합니다.
- 사용법·재부팅 확인·실패 처리는 [리니어 시험 안내](../test/linear_sensor_test/README.md)를 봅니다.
  기존 `test/compression_linear_pid_test`에는 이번 변경을 적용하지 않았습니다.

검사: 저장소 루트에서 `powershell.exe -NoProfile -ExecutionPolicy Bypass -File
test/linear_sensor_test/tests/check_linear.ps1`. 저장·손상·쓰기 실패·메인 복원 경로를 모의/컴파일타임에
검사합니다. 실물 EEPROM 보존, ADC 정확도, 단선 검출, 제어 주기는 별도 실측 대상입니다.

## 들어있는 것

```
protocol/
  teensy_usb_protocol_v1.md   ★ 명세서 — 이게 기준입니다
  examples/
    commands.jsonl            Jetson → Teensy 예제 (일부러 불량 줄 5개 포함)
    telemetry.jsonl           Teensy → Jetson 예제
teensy/
  pinmap.h                    ★ Teensy 4.1 외곽 핀 0~41 통합 배정
  saver_proto.h               ★ JSON 파서/생성기 — 그대로 쓰세요
  saver_teensy.ino            통신 골격 — [구현] 표시된 곳만 채우면 됩니다
  test_saver_proto.cpp        파서 단위시험 (PC에서 실행, 26개 통과 확인됨)
```

## 왜 이렇게 만들었는지

- **JSON Lines** (한 줄에 JSON 하나): 시리얼 모니터로 눈으로 읽으면서 디버깅할 수 있습니다.
  나중에 데이터량이 실제로 문제가 되면 그때 바이너리로 바꿉니다.
- **ArduinoJson 안 씁니다.** 프로토콜이 1단계 평면 구조라 `saver_proto.h` 하나로 충분하고,
  라이브러리 버전 문제가 생기지 않습니다. PC에서 g++로 시험도 됩니다.
- **텔레메트리는 20 Hz.** 100~500 Hz 제어 데이터는 Teensy 안에만 두세요.
  Jetson에는 표시·기록용 요약값만 올리면 됩니다.

## 시작하는 법

**1. 파서 먼저 확인** (PC, 아두이노 없이)

```bash
cd teensy
g++ -std=c++11 -Wall -o test test_saver_proto.cpp && ./test
```

`전체 통과 (통과 26 / 실패 0)` 이 나오면 정상입니다.

**2. Teensy에 골격 올리기**

`saver_teensy.ino`, `saver_proto.h`, `pinmap.h`, `src/`를 같은 스케치 폴더에 두고 업로드하세요.
핀에 의존하지 않는 USB 통신은 실제 모터 제어가 없어도 시험할 수 있습니다.

센서 취득 코드까지 컴파일할 때는 Arduino Library Manager에서
`SparkFun MAX3010x Pulse and Proximity Sensor Library` **1.1.2**를 설치하세요.
온도 센서는 DS18B20 대신 2선 NTC를 `A0/14`의 분압 ADC로 사용합니다. NTC의
`R25`, B값과 고정저항이 확정되기 전에는 온도값을 만들지 않고 필드를 생략합니다.

**3. 시리얼 모니터로 확인**

아두이노 시리얼 모니터를 열고 (줄 끝: **Newline**) 아래를 붙여넣으세요.

```
{"v":1,"t":"cmd","seq":1,"cmd":"PING"}
```

이렇게 돌아오면 통신 계층은 끝난 겁니다.

```
{"v":1,"t":"ack","seq":1,"ok":true,"state":"IDLE"}
```

`commands.jsonl` 을 한 줄씩 넣어보면 ARM → HOME → VACUUM_ON → START_CPR 흐름이 그대로 돕니다.

**4. `[구현]` 채우기**

`saver_teensy.ino` 안에 `[구현]` 으로 표시한 곳이 형님 영역입니다. 순서 추천:

1. `controlTick()` — 센서 읽어서 `g_s` 구조체 채우기 (모터는 아직 건드리지 말고)
2. `checkEstop()` — E-stop 핀 읽기
3. `motorsOff()` — 드라이버 Enable LOW
4. 원점 복귀 `HOME`
5. 음압 `VACUUM_ON`
6. 압박 PID (여기서 처음 모터가 실제로 돕니다)

1~3번만 되어도 8/2 통합시험은 가능합니다. **모터 전원은 분리한 채로 시작합니다.**

## 꼭 지켜야 할 것 4가지

1. **`sev` 3(위험) 오류는 모터를 먼저 끄고 나중에 보고합니다.** 순서를 바꾸면 안 됩니다.
2. **소프트웨어 정지는 물리 E-stop을 대체하지 않습니다.** E-stop은 USB와 무관한 하드웨어
   회로가 모터 출력을 끊고, Teensy는 그 사실을 보고만 합니다.
3. **측정이 안 되는 값은 0을 보내지 말고 필드를 통째로 빼세요.** 0을 보내면 Jetson이
   "정상적으로 0"으로 오해합니다.
4. **Teensy 상태 이름에 S1~S13을 쓰지 마세요.** 그건 Jetson 운용 상태입니다.
   Teensy는 `IDLE / ARMED / HOMED / VACUUM_HOLD / CPR_READY / CPR_RUNNING /
   CPR_PAUSED / FAULT / ESTOP` 을 씁니다.

## 형님이 정해줘야 하는 값 (7/31까지)

명세서 §11에 있는 것 중 형님 담당입니다. 제가 임의로 못 채웁니다.

| 값 | 위치 | 비고 |
|---|---|---|
| 과전류 임계 `I_LIMIT_A` | `saver_teensy.ino` 설정부 | BL60120-2430 정격 16 A 기준 |
| 목표 음압 `VAC_TARGET_KPA` | 동일 | 서정준과 함께 |
| 텔레메트리 주기 20 Hz 적정 여부 | — | 부하 보고 조정 |

`I_LIMIT_A = 0` 이면 과전류 보호가 꺼진 상태입니다. **실제 모터 구동 전에 반드시 채우세요.**

## 답 주셨으면 하는 것

명세서를 읽고 아래만 알려주시면 v1.1로 확정하겠습니다.

1. 명령 목록에서 빠진 것 / 필요 없는 것
2. 텔레메트리 필드에서 Teensy가 못 주는 것
3. 20 Hz가 부담되는지
4. 과전류·리미트 외에 즉시 정지해야 할 조건
5. 통신 두절 시 압박 지속 (§9) — 제어 관점에서 문제 없는지

---
신상현 · 2026-07-26
