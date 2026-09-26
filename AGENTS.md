# SAVER 저장소 규칙

이 파일은 **Codex(한건민)와 Claude Code(신상현)가 공유하는 단일 규칙 파일**입니다.
`CLAUDE.md` 는 이 파일을 참조만 합니다. 규칙을 두 벌로 나누지 마세요.

---

## 프로젝트

SAVER (Smart Auto-positioning Vacuum Emergency Resuscitator) — 자동 심폐소생술 장치.
대한기계학회 제16회 전국학생설계경진대회 출품작.

**연구용 시제품입니다.** 마네킹·벤치 시험 전용이며, 임상 검증된 의료 소프트웨어가 아닙니다.
그렇게 표현하는 문구를 코드나 문서에 쓰지 마세요.

## 작업 전 읽을 것

| 문서 | 언제 |
|---|---|
| 이 파일 | 항상 |
| `docs/PROJECT.md` | 프로젝트에 처음 합류할 때 |
| `docs/DECISIONS.md` | 기존 설계에 의문이 들 때. **뒤집기 전에 반드시** |
| `protocol/teensy_usb_protocol_v1.md` | 통신 관련 작업 전 |
| `docs/hardware_mapping.md` | 하드웨어 관련 작업 전 |
| `docs/document_index.md` | 명세 원본을 찾을 때 |

---

## 디렉터리 소유권

| 경로 | 소유 | 도구 |
|---|---|---|
| `teensy/` | 한건민 | Codex |
| `jetson/` | 신상현 | Claude Code |
| `ui/` | 신상현 | Claude Code |
| `protocol/` | **공동** | 합의 후에만 변경 |
| `docs/` | 공동 | 자유 |
| `docs/specs/` | 원본 보존 | **수정 금지.** 명세 원본이다 |

- **자기 소유가 아닌 디렉터리는 읽기만 하고 수정하지 않는다.**
  다른 영역에 문제가 보이면 고치지 말고 보고한다.
- `protocol/` 변경은 양쪽 담당자 합의 후에만 한다.
  변경하면 `jetson/tests/test_all.py` 와 `teensy/test_saver_proto.cpp` 를 모두 다시 돌린다.
- `ui/src/generated/` 는 자동 생성물이다. 직접 편집하지 않는다.

---

## 아키텍처 경계 — 절대 넘지 않는다

```
Jetson Nano  판단·통합·UI·기록·레이더·카메라
Teensy 4.1   실시간 제어·센서·안전 인터록
```

1. **Jetson 은 PWM 이나 실시간 제어 루프를 생성하지 않는다.**
   목표 BPM·깊이·힘 한계만 보내고, 모터 전압이나 듀티를 직접 지정하지 않는다.
2. **Jetson 의 명령은 요청이다.** Teensy 는 자체 인터록을 검사한 뒤에만 실행하고, 거부할 수 있다.
3. **소프트웨어 정지는 물리 비상정지를 대체하지 않는다.** E-stop 은 USB 와 무관한
   하드웨어 회로가 모터 출력을 끊고, Teensy 는 그 사실을 보고만 한다.
4. **Teensy 는 Jetson 이 죽어도 단독으로 안전 상태를 유지한다.**
5. 고속 원시 데이터(100~500 Hz)는 Teensy 내부에만 둔다. Jetson 에는 20 Hz 요약값만 올린다.

---

## 의학·안전 규칙

- **의학적 임계값을 절대 임의로 만들지 않는다.**
  ROSC 기준, 압박 깊이·속도 한계, 타이밍 규칙, 안전 한계가 여기 해당한다.
- 명세에 없는 값은 **설정 플레이스홀더(0 또는 null)로 두고 문서화**한다.
  값이 비어 있으면 그 값을 쓰는 기능을 활성화하지 않는다.
  (예: 목표 깊이가 0 이면 Teensy 가 `START_CPR` 을 거부한다. 이것은 정상 동작이다.)
- `jetson/config.sim.json` 은 **시뮬레이션 전용**이며 의학적 근거가 없다.
  실기 구동에 쓰지 않는다. 확정값은 별도 `config.json` 으로 만든다.
- 미확정 항목은 `protocol/teensy_usb_protocol_v1.md` §11 에 있다. 담당자 확인 전에는 채우지 않는다.
- 장치 연결 해제, 손상된 메시지, 오래된 telemetry, 상태 불일치가 발생하면 **안전 상태로 전이**한다.
- **단위시험이나 개발 중 기동 시 CPR 이 자동으로 시작되면 안 된다.**

---

## 상태 이름

- 운용 상태는 `protocol/state_machine_v2.json` 의 **S1~S13 만** 사용한다.
  `BOOT`, `SELF_TEST`, `PATIENT_ALIGNMENT` 같은 새 이름을 만들지 않는다.
  이 이름들은 UI·명세서·중간보고서·한성윤 문서 전체에서 쓰인다.
- Teensy 내부 상태는 별개 계층이다:
  `IDLE / ARMED / HOMED / VACUUM_HOLD / CPR_READY / CPR_RUNNING / CPR_PAUSED / FAULT / ESTOP`
  **여기에 S1~S13 을 쓰지 않는다.**
- 상태머신은 `protocol/state_machine_v2.json` 이 유일한 기준이다.
  Python(`jetson/saver/state_machine.py`)과 UI(`ui/src/stateMachine.js`)가 같은 파일을 읽는다.
  전이 규칙을 코드에 중복 정의하지 않는다.

---

## USB 프로토콜

- `protocol/teensy_usb_protocol_v1.md` 를 따른다.
- 시제품 단계에서는 버전이 붙은 JSON Lines 를 쓴다.
- 모든 명령에 `seq` 가 있어야 하고, 수락·거부 모두 ACK 를 반환한다.
- 중복 `seq` 는 재실행하지 않고 ACK 만 다시 보낸다.
- 물리 단위는 필드명에 반드시 포함한다 (`cp_depth_mm`, `vac_kPa`).
- **측정 불가 필드는 0 을 보내지 말고 생략한다.** 0 은 "정상적으로 0" 으로 오해된다.
- 프로토콜을 바꾸면 문서와 시험을 함께 고친다.

---

## 코딩 규칙

**공통**
- 외부 I/O 는 타임아웃·재연결·구조화된 오류 처리를 갖춘다.
- 전역 가변 상태를 쓰지 않는다.
- 경과 시간 계산은 단조 시계(`millis()`, `time.monotonic()`)를 쓴다.
- 저장 기록의 타임스탬프는 UTC ISO 8601.
- 상태 전이·명령·ACK·고장·재연결을 로그로 남긴다.
- 로그 실패가 제어를 막으면 안 된다.

**Jetson (Python)**
- **Python 3.6 호환** (JetPack 4.6.x / Ubuntu 18.04). f-string, dataclass, walrus 금지.
- `websockets==9.1` 고정. 최신 버전은 3.6 에 설치되지 않는다.
- 하드웨어 접근은 인터페이스 뒤로 격리하고, 모든 구현에 모의(mock) 구현을 둔다.
- 타입 힌트는 주석 스타일(`# type:`) 또는 생략. 3.6 에서 동작해야 한다.

**Teensy (C++)**
- 통신 계층과 제어 계층을 섞지 않는다. 제어 루프 안에서 `Serial` 을 길게 쓰지 않는다.
- `sev` 3(위험) 오류는 **모터를 먼저 끄고 나중에 보고**한다. 순서를 바꾸지 않는다.
- 외부 JSON 라이브러리를 추가하지 않는다. `saver_proto.h` 로 충분하다.

**UI (React)**
- 브라우저 저장소(localStorage 등)를 쓰지 않는다.
- 브리지에 연결되면 UI 는 `intent` 만 보내고 받은 `state` 를 그린다.
  로컬 전이는 오프라인 시연 전용이다.

---

## 시험

- 작업을 끝내기 전에 해당 영역의 시험을 **모두 돌린다.**

```bash
# Jetson  (하드웨어 없이 전부 통과해야 함)
cd jetson && python3 tests/test_all.py

# Teensy  (PC 에서, 아두이노 없이)
cd teensy && g++ -std=c++11 -Wall -o test test_saver_proto.cpp && ./test

# UI      (브라우저 없이 렌더링·터치 흐름 검증)
cd ui && npm run build:single && npm run smoke
```

- 상태머신 전이는 표 기반으로 만들고 단위시험한다.
- 시험은 **장치 분리, 손상된 패킷, 중복 명령, 오래된 telemetry, E-stop, ACK 누락**을 다뤄야 한다.
- 하드웨어가 필요한 시험은 그렇다고 명시하고, 기본 시험 묶음에 넣지 않는다.

---

## 작업 완료 보고

완료를 알리기 전에:

1. 포맷과 시험을 실행한다.
2. 수정한 파일을 요약한다.
3. 실행한 명령을 보고한다.
4. **가정한 것과 검증하지 못한 하드웨어 동작을 명시한다.**
5. 실물 시험 없이 하드웨어가 동작한다고 주장하지 않는다.

---

## 커밋

```
[jetson] 브리지 재연결 백오프 상한 추가
[teensy] 과전류 인터록 구현
[protocol] 통신 두절 시 동작 §9 수정   ← 상대에게 먼저 알릴 것
[ui] 겉옷 화면 이미지 표시 수정
```

`protocol/` 을 건드린 커밋은 반드시 상대에게 알린다.

---

## 문서·보고서 작업 규칙 (2026-09-07 추가)

- **`docs/PROJECT_STATE.md`가 확정값의 단일 출처다.** 이 저장소 어디에서든 수치를
  인용하기 전에 먼저 이 파일을 읽는다. 오래된 문서·초안의 확정 표현보다 이 파일이
  우선한다.
- **주장 수준을 구분하지 않고 서술 금지.** `implemented` / `tested` / `simulated` /
  `assumed` / `planned` 5단계를 반드시 명시한다 (`docs/evidence.csv`의 `level` 열
  참조). 특히 `simulated`(해석)와 `tested`(실측)를 혼동하지 않는다.
- **수치를 보고서·발표자료·커밋 메시지에 옮겨 적을 때는 `tools/consistency_audit.py`를
  실행해 확정값과 어긋나지 않는지 확인한다.** EAPD 계수 오류가 과거 4개소로 전파된
  전례가 있다.
- **커밋 전 `tools/patent_scan.py`가 pre-commit 훅으로 실행된다.** 비공개 차단
  용어 목록(`tools/patent_scan_terms.txt`, 저장소에 올리지 않음)에 걸리는 파일은 차단된다.
  목록은 팀장에게 받아 같은 위치에 둔다.
  의도된 예외는 훅을 건너뛰지 말고 `patent-scan: allow` 마커를 파일에 남기거나
  사람이 직접 검토한 뒤 커밋한다.
- **`docs/specs/`는 원본 보존 영역이다.** 2026-08-03에 공개되었으므로 수정·삭제
  금지. 확장자 불일치는 이 폴더를 직접 고치지 말고 `tools/fix_extensions.py`로
  진단 후 처리한다.
- 새 문서·보고서 초안은 `report/`, 원자료는 `sources/`, 실험 원본은 `data/raw/`에
  둔다. 저장소 분리 원칙은 `SPLIT_PLAN.md`를 따른다.
