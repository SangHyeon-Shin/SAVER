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

프로토콜·상태머신·실제 시리얼 연결·고장 주입·자동 재연결까지 40개 항목을 검사합니다.
하드웨어가 하나도 없어도 전부 통과해야 합니다.

## 측정 구간 mark와 로그 분석

브리지가 실행 중일 때 WebSocket 또는 CLI로 측정 시작점을 CSV에 남길 수 있습니다.
CLI의 첫 단어가 `label`, 나머지가 측정 조건 `note`가 됩니다.

```bash
python3 -m saver.mark "vacuum_1 surface=mat voltage=24V load=0kg"
python3 -m saver.mark compression_1 --note "depth=50mm load=30kg"
```

CSV에는 `source=mark`, `mark_label`, `mark_note`가 기록됩니다. 분석기는 mark 행을
다음 측정 구간의 시작으로 사용하며, mark 이전 데이터는 `unmarked`로 따로 표시합니다.

```bash
python3 tools/analyze_log.py logs/saver_YYYYMMDD_HHMMSS.csv
python3 tools/analyze_log.py logs/saver_YYYYMMDD_HHMMSS.csv \
  --vacuum-target-kpa -22.5 --vacuum-tol-kpa 2.5
```

기본적으로 `jetson/config.json`의 `vacuum_target_kPa`와 `vacuum_tol_kPa`를 읽으며,
CLI 값이 있으면 CLI가 우선합니다. 목표값이 없으면 목표 도달 시간과 누설률을 추측하지
않습니다. 빈 셀과 `*_ok=false` 값은 통계에서 제외하고 0으로 바꾸지 않습니다.

가상 시리얼 PTY가 없는 PC에서도 `fake_teensy.py`의 동일한 텔레메트리 생성기로
mark 포함 예제 로그를 만들 수 있습니다.

```bash
python3 simulators/fake_teensy.py --write-log logs --duration 4 --seed 7
```

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

## UI 연동 (다음 단계)

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
   `T_comm` 값은 미확정입니다.
