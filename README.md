# SAVER

**Smart Auto-positioning Vacuum Emergency Resuscitator** — 지능형 압박점 자동탐색 기반 음압 고정식 CPR 시스템

대한기계학회 제16회 전국학생설계경진대회 · 팀 Robotism (중앙대학교 기계공학부)

> ⚠️ 연구용 시제품입니다. **마네킹·벤치 시험 전용**이며 임상 검증된 의료기기가 아닙니다.

---

## 저장소 구성

문서와 코드는 2026-09-08부터 저장소를 나눠 관리합니다. 두 저장소 모두 팀 전용(private)입니다.

| 저장소 | 내용 |
|---|---|
| **`SAVER`** (여기) | `ui/` `jetson/` `teensy/` `test/` `protocol/` 코드, 개발 문서 `docs/`, 명세 원본 `docs/specs/` |
| **`SAVER-docs`** | 보고서 본문, 결선 발표자료·포스터, 특허 서류, 원자료, 해석 결과, 실험 데이터, 행정 문서 |

**새 보고서·특허·실험 자료는 이 저장소에 올리지 마십시오.** `SAVER-docs`로 갑니다.
`tools/patent_scan.py`가 pre-commit 훅으로 걸려 있어 미공개 항목이 섞이면 커밋이 중단됩니다.

이 저장소의 `analysis/` `cfd/` `data/` `mechanical/` `presentation/` `sources/`는
구조만 남은 빈 디렉터리입니다. 실제 내용은 `SAVER-docs`의 같은 경로에 있습니다.

---

## 구조

```
SAVER/
├── AGENTS.md          ★ 개발 규칙 (Codex · Claude Code 공통). 작업 전 필독
├── CLAUDE.md          AGENTS.md 참조용
│
├── protocol/          ★ 두 사람의 접점. 변경은 합의 후에만
│   ├── teensy_usb_protocol_v1.md    USB 통신 명세 (v1.1은 docs/ 초안에서 합의, 반영 대기)
│   ├── state_machine_v2.json        운용 상태머신 S0~S13와 확장 노드 X_ (단일 기준)
│   └── examples/                    예제 메시지 (불량 줄 포함)
│
├── jetson/            신상현 · Jetson Nano (판단·통합·UI·기록)
│   ├── saver/         브리지 · 상태머신 · 시리얼 · 로거
│   ├── simulators/    가짜 Teensy (가상 시리얼 포트)
│   └── tests/         자동시험 45항목
│
├── teensy/            한건민 · 실시간 제어·센서·안전
│   ├── saver_proto.h        JSON 파서/생성기 (외부 라이브러리 불필요)
│   ├── saver_teensy.ino     통신 골격. [구현] 부분을 채우면 됨
│   ├── test_saver_proto.cpp 파서 단위시험 (PC 에서 실행)
│   └── reference/           Git 이력에서 복원한 이전 작업 (ESP32-S3 기준)
├── test/              한건민 · 센서·모터 단품 시험 스케치
│
├── ui/                신상현 · 7인치 터치 UI (React + Vite)
│
├── docs/
│   ├── PROJECT_STATE.md   ★ "지금 무엇이 사실인가"의 단일 출처. 확정값·마감·검증 상태
│   ├── PROJECT.md         프로젝트 개요 · 팀 · 환경
│   ├── DECISIONS.md       ★ 기술 결정과 그 근거. 설계에 의문이 들면 여기부터
│   ├── hardware_mapping.md  핀·센서·전원 (Teensy 외부 5 V 전원 계획 포함)
│   ├── protocol_v1.1_draft.md  ★ 프로토콜 v1.1 합의본(7차, 10/4). protocol/ 반영 전 기준
│   ├── protocol_v1.1_worst_len.py  v1.1 메시지 최대 길이 계산
│   ├── protocol_review_한건민.md   프로토콜 검토 기록
│   ├── document_index.md  프로젝트 문서 색인
│   └── specs/             개발에 직접 필요한 명세 원본 15건 (수정·삭제 금지)
│
├── report/            Quarto 보고서 골격 (본문은 SAVER-docs)
├── tools/             patent_scan · consistency_audit · extract_sources · fix_extensions
└── release/           변환 산출물 (git 미추적)
```

`docs/specs/`는 명세 원본 보존 영역이라 삭제·이력 변경을 금지합니다.
2026-09-08에 확장자를 실제 형식에 맞게 교정했으므로(`.pdf` → `.zip` 11건,
`.docx` → `.txt` 3건) `.zip`을 열면 페이지 이미지와 추출 텍스트가 들어 있습니다.
자세한 내용은 `docs/specs/README.md`를 보십시오.

---

## 담당

| 영역 | 담당 | 도구 |
|---|---|---|
| `jetson/` `ui/` | 신상현 | Claude Code, Codex |
| `teensy/` | 한건민 | Codex |
| `protocol/` | 공동 | 합의 후 변경 |

두 도구가 `AGENTS.md` 를 함께 읽습니다. Claude Code 는 `CLAUDE.md` 를 통해 참조합니다.

처음 합류했다면 `docs/PROJECT_STATE.md` → `docs/PROJECT.md` → `AGENTS.md` →
자기 담당 디렉터리의 README 순으로 읽으세요.

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
cd jetson && python3 tests/test_all.py                       # 45항목
cd teensy && g++ -std=c++11 -Wall -o test test_saver_proto.cpp && ./test   # 26항목
cd ui && npm run build:single && npm run smoke               # 24항목
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

### 제출 전 필수 점검

```bash
py -3 tools/consistency_audit.py            # 확정값이 문서 전반에 어긋나지 않는지
py -3 tools/patent_scan.py --all --report   # 미공개 항목 혼입 여부
```

과거 보고서에서 계수 오류 하나가 4개소로 전파된 이력이 있습니다. 건너뛰지 마십시오.

---

## 현재 상태

**확정값·마감·검증 상태는 `docs/PROJECT_STATE.md`가 단일 출처입니다.**
이 README를 포함해 다른 문서의 값이 어긋나면 `PROJECT_STATE.md`가 우선합니다.
값을 여기로 옮겨 적지 마십시오 — 그렇게 해서 어긋난 이력이 있습니다.

| 항목 | 상태 |
|---|---|
| 상태머신 S0~S13 + X_ | 구현 완료 (JSON 단일 기준). 탐색 후 고정 순서로 바꾸는 작업은 v1.1 묶음 |
| 프로토콜 v1.1 | **7차 합의본(10/4)** `docs/protocol_v1.1_draft.md`. `protocol/` 반영과 양쪽 구현 대기 |
| Jetson 브리지 | 구현 완료 · 자동시험 45/45(PC). 젯슨 실기는 40항목 판까지 확인 · 시작 seq 무작위(Jetson만 재시작될 때 중복 무시 방지) |
| 가짜 Teensy | 구현 완료 (고장 주입 7종) |
| 7인치 UI | 20화면 구현 완료 · 연결 중 생략·stale 텔레메트리는 "—"로 표시(모의값으로 채우지 않음) |
| Teensy 펌웨어 | 통신 골격과 리니어 영구 보정 읽기, 단품 시험 스케치(`test/`). v1.1 구현 대기(한건민) |
| Teensy 전원 | 벅 5 V → `VIN`, `VIN`–`VUSB` 패턴 절단, USB는 통신 전용 — **계획(10/4), 미검증** |
| 제어 보드 | Teensy 4.1 확정 (2026-08-03, PCB 미발주) |

미확정 항목은 `docs/PROJECT_STATE.md` §3에 있습니다. **추측으로 채우지 않습니다.**
값이 비어 있으면 Teensy 가 `START_CPR` 을 거부합니다. **정상 동작입니다.**

주장 수준(`implemented` / `tested` / `simulated` / `assumed` / `planned`)을
보고서·발표·커밋 메시지에서 반드시 구별합니다. 정의는 `PROJECT_STATE.md` §5,
항목별 값은 `docs/evidence.csv` 의 `level` 열입니다.

---

## 팀

| 이름 | 역할 |
|---|---|
| 신상현 | 팀장 · PM · SW/해석 |
| 서정준 | 부팀장 · 제작/설계 |
| 한건민 | 제어/회로 |
| 한성윤 | 의학/안전/행정 |
| 양다솜 교수 | 지도교수 |
