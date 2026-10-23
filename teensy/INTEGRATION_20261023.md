# 2026-10-23 Git 통합 기록

## 보존 범위

- 기준: `origin/claude/saver-project-onboarding-xpmdhw`의 `f5762f1`.
  첫 rebase 이후 추가된 S1 점검/오류 안내 변경도 다시 rebase해 포함했다.
- 결과 브랜치: `codex/teensy-integration-20261023`.
- rebase 이전 변경은 `codex/teensy-v24-review-20261011`의 `cd31beb`에 보존했다.
- 기존 공유 브랜치를 강제 push하지 않는다. `main`과 팀 claude 브랜치는 수정하지 않는다.
- Jetson, UI, protocol 파일은 기준 브랜치 그대로 유지한다.
- 실제 시연 진입점은 `saver_teensy.ino` → `src/demo/demo_app.h`다.
  이 경로의 제어 소스는 rebase 이전 체크포인트와 동일하며, 중단한 구동 설정 변경을
  이번 Git 통합에서 다시 시작하지 않았다.
- 팀의 참고 통신 구현은 삭제하지 않고 `saver_v11_reference.h`에 옮겼다.
  `tests/host/host_test.cpp`는 이 참고 구현을 시험한다. Arduino의 진입점이 아니다.
- 팀 프로토콜 단위시험 전체를 보존하고 Arduino 빌드에서만 제외하도록 감쌌다.
  JSONL fixture 비교는 Windows checkout의 CRLF를 텍스트 모드로 읽는다.
- 문서 충돌은 양쪽 기록을 유지했다. 기존 전원 결정 D-33과 충돌하던 로컬 벤치 이동
  기록은 D-55로 번호를 바꿨다. 원격의 S1 점검 결정 D-54, 모터 선 색 정정과
  고장 엔코더 분리 주의도 유지했다.

## 재검증 범위

모두 소프트웨어 시험이다. 업로드, 시리얼 포트 연결, 실제 모터 동작은 수행하지 않았다.

- 실제 시연 펌웨어: `teensy/tests/demo/check_main.ps1`의 기본 설정 및 합성 fixture 시험.
- PPG 합성 재생: `teensy/tests/demo/check_ppg_diag_replay.ps1`.
- 핀 회귀: `teensy/tests/check_pinmap.ps1`.
- 프로토콜: `teensy/test_saver_proto.cpp`를 C++11로 빌드하고 실행.
- 참고 통신 구현: `teensy/tests/host/host_test.cpp`의 기본 및 가짜 구동 훅 빌드.
- Teensy 4.1 대상: Arduino CLI compile/link. 외부 SparkFun 라이브러리의 기존 경고는 남아 있다.
- 벤치 시험 묶음: `test/`의 전류, 리니어, 압력, 로드셀, 엔코더, 선택형/통합 시험 검사.
- UI: `npm run build:single`, `npm run smoke`.

결과(`tested`, PC 모의/컴파일에 한정): 시연 펌웨어 기본 476개·합성 fixture 575개,
프로토콜 118개, PPG 실제 라이브러리 합성 재생 14개, 참고 구현 호스트 기본 7장면·
가짜 훅 9장면, 최신 팀 UI 스모크 172개가 통과했다. 실제 Teensy 4.1 compile/link도 통과했다.
UI의 소프트웨어 모의 수치는 실제 시제품 측정 결과가 아니다.

## 이 PC의 검증 제약

- `jetson/tests/test_all.py`는 UTF-8 모드와 임시 의존성으로 실행했으나, Windows에서
  POSIX PTY 기반 가짜 Teensy 포트를 만들 수 없어 전체 완료하지 못했다.
  이 실패를 통과로 기록하지 않는다. Jetson/Linux에서 같은 시험을 다시 실행해야 한다.
- 전체 `tools/consistency_audit.py`는 원격에 이미 있는 압축 UI JS 한 줄을 지적한다.
  같은 줄의 파형 제목 `압박력`과 다른 용도의 숫자를 함께 검사하는 결과다.
  해당 산출물은 원격과 동일하게 보존했다. 이 통합 변경에서 새로 발생한 소스 차이는 아니다.
- 비공개 차단 용어는 커밋에 포함하지 않고 별도 목록으로 커밋 대상 검사를 실행한다.
