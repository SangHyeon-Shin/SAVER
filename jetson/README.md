# jetson/ — Jetson Nano

담당: 신상현 (Claude Code). 규칙은 저장소 루트의 `AGENTS.md` 를 따릅니다.
젯슨 실기에서 Claude Code 로 작업할 때는 `AGENT_BRIEF.md`(젯슨 전용 사실, 명령, 사람 확인이 필요한 일)를 먼저 읽힙니다.

하드웨어 없이 전체를 개발·시험할 수 있습니다. 가짜 Teensy가 **가상 시리얼 포트(pty)** 를
만들기 때문에, 코드는 실제 장비와 완전히 같은 경로로 접속합니다. "시뮬레이션용 분기"가
코드에 생기지 않는 것이 이 방식의 핵심입니다.

```
React UI ──ws://:8765── bridge.py ──USB Serial── Teensy 4.1
                          │                      (또는 fake_teensy.py)
                          ├── state_machine.py  ← protocol/state_machine_v2.json
                          └── logger.py         → logs/*.csv (W7 규격)
```

## 젯슨 실기 설치

젯팩 4.6 을 새로 깐 젯슨에서는 `bash setup/setup_jetson.sh` 한 번으로 패키지, 권한(Teensy udev), 브리지 서비스,
냉각 팬 자동 제어(`saver-fan`), 키오스크, Claude Code, 자동 시험을 설정합니다. 자세한 내용은 `setup/README.md`.

## MR60BHA2 레이더 (D-50)

`saver/radar.py` 가 레이더 원시 프레임을 읽어 호흡수, 심박수, 거리를 받는다. 브리지에 `--radar PORT` 를 주면 켜지고
(설치 스크립트 기본 `/dev/saver-radar`), 1초마다 UI 로 보내고 기록한다.

| 값 | 쓰는 곳 |
|---|---|
| 호흡수, 심박수 | 일시정지(S8), 회복 신호 확인(S9) 화면. 장치가 멈춤을 보고한 뒤에만 수치. 개발 패널(D 키)은 언제나. 판단에는 쓰지 않음 |
| 거리 → 가슴 두께(EAPD) | `EAPD = radar_floor_mm − 거리`. 압박점 탐색이 D 를 계산해 Teensy 에 주행 거리로 요청 |

- `radar_floor_mm`(탐색 자세에서 레이더 면의 바닥 위 높이, mm)은 실측해 `jetson/config.json` 에 넣는다. 비어 있으면
  EAPD 를 만들지 않아 탐색이 "가슴 두께를 재지 못했습니다"로 시작하지 않는다(대체 상수 없음).
- 하드웨어 없이: `python3 simulators/fake_radar.py --port /tmp/ttyRADAR` 뒤 `python3 -m saver.bridge --radar /tmp/ttyRADAR --config config.sim.json`.
  `--text` 를 주면 프레임 대신 글자를 보내 키트 기본 펌웨어 상황을 흉내 낸다.

### 연결

| 연결 | 포트 | 준비 |
|---|---|---|
| XIAO ESP32C6 키트를 USB 로 | `/dev/saver-radar`(udev 고정 이름, 실제 `/dev/ttyACM*`) | 키트 기본 펌웨어(ESPHome)는 원시 프레임을 USB 로 보내지 않는다. 전달 펌웨어를 한 번 올린다(아래) |
| 레이더 UART 를 40핀에 직접 | `/dev/ttyTHS1`(8번 TX, 10번 RX, 3.3 V) | 설치 때 `--radar-port /dev/ttyTHS1` |

### 전달 펌웨어 올리기 (XIAO ESP32C6, 한 번)

`radar_fw/mr60bha2_passthrough/` 는 레이더 UART(115200)를 USB 로 그대로 넘긴다. 값을 해석하거나 바꾸지 않는다
(Seeed-mmWave-library `passthrough_mode` 와 같은 동작).

0. (지금 펌웨어가 필요할 수 있으면) 백업: PC 에서 `python -m pip install esptool` 뒤
   `python -m esptool --chip esp32c6 --port <포트> read_flash 0 0x400000 xiao_backup.bin`(XIAO ESP32C6 플래시 4 MB 전체).
   되돌릴 때는 이 파일을 아래와 같은 방법으로 `0x0` 에 올린다.
1. 설치 없이: Chrome 또는 Edge 에서 `https://espressif.github.io/esptool-js/` → Connect(키트 포트) → Flash Address `0x0` 에
   `radar_fw/mr60bha2_passthrough/bin/mr60bha2_passthrough_xiao_esp32c6.bin` → Program → USB 를 다시 꽂는다.
   연결이 안 되면 XIAO 의 BOOT 버튼을 누른 채 USB 를 꽂고 다시 Connect.
2. 또는 Arduino IDE: 스케치 머리말 순서(보드 XIAO_ESP32C6, USB CDC On Boot Enabled).
3. 확인: `saver-stop` 뒤 `saver-radar` 에서 "포트 열림", 프레임 수 증가, 호흡, 심박, 거리.
4. 되돌리기: Seeed 위키의 MR60BHA2 키트 안내대로 기본(ESPHome) 펌웨어를 다시 올린다.

빌드는 확인했고(`bin/BUILD_INFO.txt`), 키트에 올려 보지는 않았다.

10/15 실기(젯슨 Chromium 의 esptool-js 로 기록, MD5 일치): 기록 직후에는 `--dump` 받은 바이트가 0 이었고, ESP Tool 탭을 닫고
USB 를 다시 꽂은 뒤 정상 동작했다. `saver-radar --dump --seconds 10` 에서 23,069 바이트, 레이더 프레임 1,190개, 종류 radar.
레이더 모듈(칩 표기 Andar ADT6101P)은 켜질 때 글자 부팅 기록을 먼저 내고, 그 뒤 프레임을 1초에 약 190개 보낸다
(10초 덤프의 프레임 ID 0~1964). 거리 값은 40~57 cm 였다(자 대조 전).

| TYPE | 내용 | 처리 |
|---|---|---|
| 0A14, 0A15 | 호흡수, 심박수(float32) | 표시 |
| 0A16 | 거리(유효 표시 + float32, cm) | 표시, EAPD |
| 0F09 | 사람 감지 | 표시 |
| 0A13 | 위상 3개(float32) | 넘김 |
| 0A04 | 표적 정보(20 바이트) | 넘김 |
| 0A17, 0A29 | 뜻을 모름(8, 2 바이트) | 넘김 |
| 0100 | 글자(펌웨어 이름, `breath rate = …` 같은 디버그 글자) | 넘김 |

받으며 화면에 쓰던 이전 `--dump` 에서는 프레임 번호 775개가 빠졌다(화면 출력 때문에 읽기가 밀린 것으로 추정). 지금 `--dump` 는
다 받은 뒤 출력하고 프레임 종류별 개수와 번호 빠짐을 보인다. `saver-radar` 줄의 `빠짐` 도 같은 값이다.

진단 펌웨어 `radar_fw/mr60bha2_diag/`(빌드 `bin/mr60bha2_diag_xiao_esp32c6.bin`)는 전달 펌웨어가 다시 0 바이트를 낼 때 원인을
가르는 용도로 둔다. 레이더 자료는 전달하지 않고 1초마다 `SAVER-DIAG …` 글자 줄만 낸다. 읽는 법은 스케치 머리말에 있고,
실기에 올려 보지는 않았다.

### 현장 확인 (`saver-radar`)

`saver-stop` 뒤 `saver-radar`(또는 `python3 -m saver.radar /dev/saver-radar --floor 520`). 1초마다 포트 상태, 받은 바이트,
프레임 수와 값을 보이고, 원인을 연결마다 한 번씩 안내한다. 포트가 열리면 `포트 정보:` 줄에 실제 장치와 by-id 이름이 나온다.
이름에 `Espressif` 가 있으면 레이더 키트(XIAO), `Teensyduino` 면 Teensy 다.

| 보이는 것 | 뜻 |
|---|---|
| `포트 없음` | 그 경로에 장치가 없다. 지금 있는 직렬 포트 목록(by-id 이름 포함)을 함께 보인다 |
| `열기 실패` | 권한(dialout 그룹)이 없거나 다른 프로그램(브리지 등)이 쓰는 중 |
| `포트 열림 받음 0 B` | 장치는 있으나 자료가 없다. 레이더 전원, 배선, 펌웨어 확인 |
| `받음 N B 프레임 0 깨짐 0` | 받은 자료에 레이더 프레임 시작 바이트(0x01)가 없다. 레이더 프레임이 아니다. 아래 `종류` 로 원인을 가린다 |
| 프레임이 늘고 값이 보임 | 정상. 거리 단위(cm → mm)는 자로 잰 값과 대조한다 |
| `빠짐` 이 계속 늘어남 | 프레임 ID 가 건너뛴다(전송 중 손실). 브리지와 같이 읽고 있지 않은지(`saver-stop`), USB 선, 젯슨 부하를 확인 |

프레임이 0 이면 안내 줄의 `종류`(앞 256 바이트로 판별)를 본다.

| 종류 | 뜻 | 할 일 |
|---|---|---|
| `esphome` | 키트 기본 펌웨어(ESPHome)의 기록 글자 | 전달 펌웨어를 올린다(위) |
| `seeed_example` | Seeed 예제 펌웨어가 값을 글자로 내보냄 | 전달 펌웨어로 바꾼다 |
| `csv` | 값을 쉼표로 나눈 글자 줄(예: `74.00, 6.00, 10.00, NORMAL, LOW, 45.92`). XIAO 에 다른 펌웨어(아두이노 스케치 등)가 있다 | 전달 펌웨어로 바꾼다. 지금 펌웨어가 필요하면 먼저 백업 |
| `teensy` | SAVER Teensy 의 JSON 줄. 레이더가 아닌 Teensy 포트를 열었다 | `/dev/saver-radar`(이름에 Espressif)를 연다 |
| `boot` | XIAO(ESP32) 부팅 기록 | 몇 초 뒤에도 같으면 재부팅 반복을 의심한다 |
| `text` | 그 밖의 글자 | 펌웨어와 포트를 확인한다 |
| `binary_nosof`, `binary` | 이진 자료지만 레이더 프레임이 아님 | 통신 속도(`--baud`, 기본 115200)와 장치를 확인한다 |

받은 바이트를 그대로 보려면 `saver-radar --dump`(5초, `--seconds 10` 으로 늘림). 16진수와 글자 줄 뒤에
`합계 N 바이트, 0x01 N개, 레이더 프레임 N개. 종류: …` 를 보인다. 전달 펌웨어가 제대로 돌면 레이더 프레임 수가 0 보다
크고 `종류: radar` 다.

## 노트북 대체 실행 (젯슨이 안 될 때)

브리지와 UI 는 노트북에서도 돕니다. 레이더, 카메라, 키오스크, `saver-*` 명령은 젯슨 전용입니다.

```bash
cd SAVER/jetson
python -m pip install websockets pyserial     # Python 3.7~3.14 (시험: 리눅스 3.11, 3.13, 3.14)
python -m saver.bridge --serve ui_dist        # Teensy 는 VID/PID 로 자동 탐색. 지정하려면 --serial COM5
# 브라우저에서 http://127.0.0.1:8080
```

- **Windows 실행은 아직 확인하지 않았습니다(10/12).** 한국어 Windows 기본 인코딩(cp949)으로 상태머신 JSON 을 읽다가
  멈추는 문제와 Python 3.14 에서 시작하자마자 멈추는 문제는 고쳤고, 리눅스에서 같은 조건을 만든 시험으로 막아 두었습니다.
- 가짜 Teensy(`simulators/fake_teensy.py`)는 pty 를 써서 Windows 에서 돌지 않습니다. Windows 에서는 실물 Teensy 로만 봅니다.
- 안전 규칙은 젯슨과 같습니다. 브리지는 S1 에서 `ARM`, `HOME` 을 보내므로 처음 연결은 모터와 블로워 전원을 끈 상태에서
  하고, `config.sim.json` 은 쓰지 않습니다.

## 실행

**터미널 1 — 가짜 Teensy**

```bash
python3 simulators/fake_teensy.py --port /tmp/ttyFAKE
```

실행 중 키보드로 고장을 주입할 수 있습니다.

| 입력 | 재현되는 상황 |
|---|---|
| `oc` | 모터 과전류 `3-CU-021` → 안전정지 |
| `estop` | 비상정지 |
| `leak` | 음압 누설 `2-VU-005` |
| `vloss` | 압박 중 흡착 상실 `3-VU-006` |
| `drop` | 송신 중단 = USB 통신 두절 |
| `slow` | 텔레메트리 1 Hz 저하 |
| `ok` | 정상 복귀 |

**터미널 2 — 브리지**

```bash
pip3 install websockets pyserial
python3 -m saver.bridge --serial /tmp/ttyFAKE --config config.sim.json
```

**터미널 3 — UI**

```bash
cd ../ui && npm install && npm run dev
```

## 시험

```bash
python3 tests/test_all.py
```

프로토콜·상태머신·실제 시리얼 연결·고장 주입·자동 재연결·Jetson 재시작(Teensy 전원 유지)·정적 UI 서빙(Python 3.6 경로 포함)·브리지 기동·UTF-8 아닌 기본 인코딩까지 검사합니다.
하드웨어가 하나도 없어도 전부 통과해야 합니다.

## 설정값 — 중요

`saver/bridge.py`의 `CONFIG`는 기본이 **0**입니다. 0인 값은 Teensy로 보내지 않고,
목표 깊이가 없으면 Teensy가 `START_CPR`을 거부합니다. **이게 정상 동작입니다.**

| 값 | 상태 | 담당 |
|---|---|---|
| `target_bpm` | 확정 110 | — |
| `target_depth_mm` | **미확정** | 한성윤 (가이드라인 근거) |
| `force_max_N` | **미확정** | 한성윤 / 서정준 |
| `vacuum_target_kPa` | **미확정** | 서정준 + 한건민 |

`config.sim.json`은 **시뮬레이션 전용**이며 의학적 근거가 없습니다. 불러오면 경고가 출력됩니다.
확정값이 나오면 `config.json`을 따로 만들어 쓰고, 실기에는 절대 `config.sim.json`을 쓰지 마세요.

## UI 연동

브리지는 상태머신의 **주인**입니다. UI는 화면입니다.

```jsonc
UI → {"type":"intent","name":"CPR_START"}
UI ← {"type":"state","node":"S7","state":"S7","alarm":"normal","allowed":[...]}
UI ← {"type":"telemetry","cp_depth_mm":48.76,...,"stale":false}
UI ← {"type":"error","code":"3-CU-021","sev":3,"desc":"모터 과전류"}
UI ← {"type":"rejected","name":"CPR_START","reason":"device_not_ready","mcu":"VACUUM_HOLD"}
```

**구현 완료** (`ui/src/bridge.js`, `ui/src/App.jsx`).

- 브리지 연결 시 → `intent` 만 보내고 받은 `state` 로 화면 결정
- 미연결 시 → 로컬 전이 (오프라인 시연 백업)
- `rejected` 수신 시 "장치 준비 중" 안내 표시

실제로 UI 가 장치보다 빨리 진행하는 경우가 시험에서 확인됐습니다
(흡착 완료 전 [재확인] 연타 등). `requires_mcu` 가 이를 막습니다.

## 알려진 설계 결정

1. **장치 선행조건 검사** — `state_machine_v2.json`의 `requires_mcu`. UI가 앞서가면 전이를 거부합니다.
2. **압박점 확정은 장치 주도** — `SCAN_RESULT` 수신 즉시 `SET_CP_POINT`를 보냅니다.
   UI의 [확인] 버튼 속도와 무관하게 장치가 `CPR_READY`에 올라갑니다.
3. **sev 3 오류 → 안전정지** — 전이표에 `STOP`이 없는 상태에서는 `force("S11")`로 강제 이동하고
   그 사유를 로그에 남깁니다.
4. **통신 두절** — Teensy가 `T_comm`까지 압박을 유지합니다(명세 §9). 브리지는 경고만 띄웁니다.
   `T_comm`은 한성윤 회신(10/1, 25 s)을 v1.1 구현 때 반영합니다. Teensy를 외부 5 V로 켜면
   USB를 뽑아도 Teensy가 꺼지지 않으므로 이 동작이 실기에서 실제로 작동합니다.
5. **시작 seq 무작위** — Teensy는 최근 8개 seq를 기억하고 같은 seq는 실행하지 않습니다.
   Jetson만 재시작되면 1부터 다시 세는 seq가 겹쳐 새 명령이 무시될 수 있어, 시작값을
   1~65535 중 무작위로 둡니다(`protocol.initial_seq`). v1.1에서는 `GET_INFO`의 `last_seq`로 이어 갑니다.
6. **실측 표시 규칙** — UI는 연결 중 받은 숫자만 보여 주고, 생략되거나 오래된 필드는 "—"로 둡니다.
   오프라인 시연용 모의값이나 이전 값으로 채우지 않습니다(`ui/src/vitalsView.js`).
