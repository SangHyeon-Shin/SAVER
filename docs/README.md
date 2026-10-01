# docs

## 먼저 읽을 것

| 문서 | 내용 |
|---|---|
| `../AGENTS.md` | **개발 규칙.** Codex·Claude Code 공통. 작업 전 필독 |
| `PROJECT.md` | 프로젝트 개요·팀·일정·환경. 새로 합류하면 여기부터 |
| `DECISIONS.md` | **왜 이렇게 되어 있는가.** 같은 논의를 반복하지 않기 위한 기록 |

## 참조

| 문서 | 내용 |
|---|---|
| `hardware_mapping.md` | 핀·센서·전원. 실기 확인 후 채움 |
| `document_index.md` | 프로젝트 문서 85건 색인. `specs/` 에 15건 포함 |
| `specs/` | 개발에 직접 필요한 명세 원본 (요구사항·연동표·알고리즘·근거추적표 등) |
| `../protocol/teensy_usb_protocol_v1.md` | USB 통신 명세 |
| `../teensy/reference/` | Git 이력에서 복원한 이전 작업 (ESP32 기준, 참고용) |

## 하드웨어 통합 주간(8/1~8/7)에 채울 것

| 파일 | 시점 | 내용 |
|---|---|---|
| `jetson_environment.md` | 8/1 | JetPack·Python 버전, 장치 인식 결과 |
| `hardware_mapping.md` | 8/2 | 핀 배치 확정, 결선도 |
| `measurements.md` | 8/5 | **측정값 vs 예측값 비교표** ← 최종보고서 핵심 |
| `fault_injection.md` | 8/6 | 고장 주입 시험 결과 |

`measurements.md` 가 가장 중요합니다. 현재 보고서의 값이 전부 설계값이라,
실측 데이터가 최종보고서와 논문을 동시에 뒷받침합니다.
