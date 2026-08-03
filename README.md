# SAVER

**Smart Auto-positioning Vacuum Emergency Resuscitator** — 지능형 압박점 자동탐색 기반 음압 고정식 CPR 시스템

대한기계학회 제16회 전국학생설계경진대회 · 팀 Robotism (중앙대학교 기계공학부)

> ⚠️ 연구용 시제품입니다. **마네킹·벤치 시험 전용**이며 임상 검증된 의료기기가 아닙니다.

---

## 구조

```
SAVER/
├── AGENTS.md          ★ 개발 규칙 (Codex · Claude Code 공통). 작업 전 필독
├── CLAUDE.md          AGENTS.md 참조용
│
├── protocol/          ★ 두 사람의 접점. 변경은 합의 후에만
│   ├── teensy_usb_protocol_v1.md    USB 통신 명세
│   ├── state_machine_v2.json        운용 상태머신 S1~S13 (단일 기준)
│   └── examples/                    예제 메시지 (불량 줄 포함)
│
├── jetson/            신상현 · Jetson Nano (판단·통합·UI·기록)
│   ├── saver/         브리지 · 상태머신 · 시리얼 · 로거
│   ├── simulators/    가짜 Teensy (가상 시리얼 포트)
│   └── tests/         자동시험 40항목
│
├── teensy/            한건민 · 실시간 제어·센서·안전
│   ├── saver_proto.h        JSON 파서/생성기 (외부 라이브러리 불필요)
│   ├── saver_teensy.ino     통신 골격. [구현] 부분을 채우면 됨
│   ├── test_saver_proto.cpp 파서 단위시험 (PC 에서 실행)
│   └── reference/           Git 이력에서 복원한 이전 작업 (ESP32-S3 기준)
│
├── ui/                신상현 · 7인치 터치 UI (React + Vite)
└── docs/
    ├── PROJECT.md     프로젝트 개요 · 팀 · 일정 · 환경
    ├── DECISIONS.md   ★ 기술 결정과 그 근거. 설계에 의문이 들면 여기부터
    ├── hardware_mapping.md
    ├── document_index.md   프로젝트 문서 85건 색인
    └── specs/         개발에 직접 필요한 명세 원본 15건
```

---

## 담당

| 영역 | 담당 | 도구 |
|---|---|---|
| `jetson/` `ui/` | 신상현 | Claude Code |
| `teensy/` | 한건민 | Codex |
| `protocol/` | 공동 | 합의 후 변경 |

두 도구가 `AGENTS.md` 를 함께 읽습니다. Claude Code 는 `CLAUDE.md` 를 통해 참조합니다.

처음 합류했다면 `docs/PROJECT.md` → `AGENTS.md` → 자기 담당 디렉터리의 README 순으로 읽으세요.

---

## 아키텍처

```
                      MR60BHA2 레이더 ──┐
                      Raspberry Pi Cam ─┤
                                        ▼
7" 터치 UI ──WebSocket:8765── Jetson Nano ──USB Serial── Teensy 4.1
 (React)                     (판단·상태·기록)            (실시간 제어)
                                                             │
                                          압박 모터 · 음압 · PPG · 로드셀
                                          엔코더 · 리미트 · 비상정지
```

**경계는 타협 대상이 아닙니다.**
Jetson 은 PWM 을 만들지 않고, Teensy 는 Jetson 없이도 안전 상태를 유지합니다.
자세한 내용은 `AGENTS.md` 와 `protocol/teensy_usb_protocol_v1.md` §1.

---

## 시작하기

### 하드웨어 없이 전체 돌려보기

```bash
# 터미널 1 — 가짜 Teensy (가상 시리얼 포트 생성)
cd jetson
python3 simulators/fake_teensy.py --port /tmp/ttyFAKE

# 터미널 2 — 브리지
cd jetson
python3 -m saver.bridge --serial /tmp/ttyFAKE --config config.sim.json

# 터미널 3 — UI
cd ui
npm install && npm run dev
```

가짜 Teensy 터미널에서 `oc`, `estop`, `drop` 등을 입력하면 고장을 주입할 수 있습니다.

### 시험

```bash
cd jetson && python3 tests/test_all.py                       # 40항목
cd teensy && g++ -std=c++11 -o test test_saver_proto.cpp && ./test   # 26항목
cd ui && npm run build:single && npm run smoke               # 18항목
```

**셋 다 하드웨어 없이 통과해야 합니다.**

### 젯슨 실기 배포

```bash
# PC 에서
cd ui && npm run build
scp -r dist ../jetson <계정>@<젯슨IP>:~/SAVER/

# 젯슨에서 (Python 3.6)
python3 -m pip install --user 'websockets==9.1' pyserial
cd ~/SAVER/jetson
python3 -m saver.bridge --serial /dev/ttyACM0 --serve ../dist --http-port 8080
```

---

## 현재 상태

| 항목 | 상태 |
|---|---|
| USB 프로토콜 v1.0a | 초안. **7/31 한건민 검토 후 확정** |
| 상태머신 S1~S13 | 구현 완료 (JSON 단일 기준) |
| Jetson 브리지 | 구현 완료 · 젯슨 실기 40/40 통과 |
| 가짜 Teensy | 구현 완료 (고장 주입 7종) |
| Teensy 펌웨어 | 통신 골격만. `[구현]` 부분 대기 |
| 제어 보드 | **Teensy 4.1 확정** (2026-08-03, PCB 미발주) |
| 7인치 UI | 20화면 구현 완료 |
| 레이더 · 카메라 | 미착수 (8/5 예정) |

### 확정이 필요한 값

`protocol/teensy_usb_protocol_v1.md` §11 참조. **추측으로 채우지 않습니다.**

| 값 | 담당 | 비고 |
|---|---|---|
| 목표 압박 깊이·속도 | 한성윤 | 가이드라인 근거 필요 |
| 통신 두절 시 압박 지속 시간 `T_comm` | 한성윤 | 의학 판단 |
| 과전류 임계 | 한건민 | 정격 16 A |
| 목표 음압 · 누설 판정 | 서정준 + 한건민 | 이전 코드에 **-30 kPa ±0.5** 존재. 확정값인지 확인 |
| 구동계(바퀴) 명령 필요 여부 | 신상현 + 한건민 | 프로토콜에 명령 없음 |
| 냉각팬 제어 주체 | 한건민 | Teensy 자율 vs Jetson 명령 |
| PCB 회로도 | 한건민 | Teensy 4.1 기준 재작성 (이력 회로도는 ESP32) |

값이 비어 있으면 Teensy 가 `START_CPR` 을 거부합니다. **정상 동작입니다.**

---

## 팀

| 이름 | 역할 |
|---|---|
| 신상현 | 팀장 · PM · SW/해석 |
| 서정준 | 부팀장 · 제작/설계 |
| 한건민 | 제어/회로 |
| 한성윤 | 의학/안전/행정 |
| 양다솜 교수 | 지도교수 |
