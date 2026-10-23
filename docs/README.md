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
| `PROJECT_STATE.md` | **지금 무엇이 사실인가.** 확정값·마감·검증 상태의 단일 출처 |
| `hardware_mapping.md` | 핀·센서·전원. Teensy 외부 5 V 전원 계획(10/4, 미검증) 포함 |
| `protocol_v1.1_draft.md` | 프로토콜 v1.1 합의 경과(8차, 10/4). **10/23 `protocol/teensy_usb_protocol_v1.md` v1.1 부록 A로 반영**, 규범은 그쪽 |
| `protocol_v1.1_worst_len.py` | v1.1 메시지 최대 길이 계산(본문 511 byte 한도) |
| `protocol_review_한건민.md` | 프로토콜 검토 기록 |
| `document_index.md` | 프로젝트 문서 85건 색인. `specs/` 에 15건 포함 |
| `specs/` | 개발에 직접 필요한 명세 원본 (요구사항·연동표·알고리즘·근거추적표 등). 수정·삭제 금지 |
| `../protocol/teensy_usb_protocol_v1.md` | USB 통신 명세 v1.0 |
| `../teensy/reference/` | Git 이력에서 복원한 이전 작업 (ESP32 기준, 참고용) |

## 실측 자료를 둘 곳

시험 원본(T1~T4, 벤치 측정, 고장 주입 로그)은 이 저장소가 아니라 `SAVER-docs`의 `data/raw/`에 둡니다.
결과를 정리한 값은 `PROJECT_STATE.md` §4에 주장 수준(`tested` 등)과 함께 옮깁니다.
