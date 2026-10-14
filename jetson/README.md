# jetson/ — Jetson Nano

담당: 신상현 (Claude Code). 규칙은 저장소 루트의 `AGENTS.md` 를 따릅니다.

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

`saver/radar.py` 가 레이더 UART 를 읽어 호흡수, 심박수, 거리를 받는다. 브리지에 `--radar /dev/ttyTHS1` 을 주면 켜지고
(설치 스크립트 기본), 1초마다 UI 로 보내고 기록한다.

| 값 | 쓰는 곳 |
|---|---|
| 호흡수, 심박수 | 일시정지(S8), 회복 신호 확인(S9) 화면. 장치가 멈춤을 보고한 뒤에만 수치. 개발 패널(D 키)은 언제나. 판단에는 쓰지 않음 |
| 거리 → 가슴 두께(EAPD) | `EAPD = radar_floor_mm − 거리`. 압박점 탐색이 D 를 계산해 Teensy 에 주행 거리로 요청 |

- `radar_floor_mm`(탐색 자세에서 레이더 면의 바닥 위 높이, mm)은 실측해 `jetson/config.json` 에 넣는다. 비어 있으면
  EAPD 를 만들지 않아 탐색이 "가슴 두께를 재지 못했습니다"로 시작하지 않는다(대체 상수 없음).
- 현장 확인: `saver-stop` 뒤 `saver-radar`(또는 `python3 -m saver.radar /dev/ttyTHS1 --floor 520`)로 1초마다 값을 본다.
  거리 단위(cm → mm)는 자로 잰 값과 대조한다.
- 하드웨어 없이: `python3 simulators/fake_radar.py --port /tmp/ttyRADAR` 뒤 `python3 -m saver.bridge --radar /tmp/ttyRADAR --config config.sim.json`.

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
