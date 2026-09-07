# 한건민 전달 — Teensy 4.1 ↔ Jetson Nano 통신

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

`saver_teensy.ino`, `saver_proto.h`, `pinmap.h`를 같은 폴더에 두고 업로드하세요.
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
