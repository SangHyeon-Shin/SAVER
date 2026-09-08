# cfd/ 작업 규칙

- ANSYS Fluent 프로젝트/journal/결과. 팬 커브 경계조건은 이미 확정된 전제이며
  임의로 바꾸지 않는다. 바꿀 경우 `docs/PROJECT_STATE.md`와 재검토 대상 보고서
  절을 함께 갱신한다.
- 진공 유지력 상한 ≈ 290 N (dead-head)은 이 디렉터리의 해석 결과다. 실측이
  아니므로 `simulated`로만 인용한다.
- 원본 프로젝트 파일(`.msh`, `.jou`, `.trn` 등)은 용량이 크다. `.gitignore` 보정
  규칙(`data/processed/*.csv`만 추적)을 확인하고 원자료 전체를 커밋하지 않는다.
