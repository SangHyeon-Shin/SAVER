# cfd/ 작업 규칙

- ANSYS Fluent 프로젝트/journal/결과. 팬 커브 경계조건은 이미 확정된 전제이며
  임의로 바꾸지 않는다. 바꿀 경우 `docs/PROJECT_STATE.md`와 재검토 대상 보고서
  절을 함께 갱신한다.
- 누설 해석 결과·요약은 `SAVER-docs/cfd/` (`SAVER_CFD_results_final.csv`, `leak_cfd_summary.md`).
  옛 "290 N" 유지력은 1:2 축척 추정값으로 폐기됐다(D-24). 명목 유지력 3600 N(−20 kPa)은
  계산값이며 `simulated`로만 인용한다.
- 원본 프로젝트 파일(`.msh`, `.jou`, `.trn` 등)은 용량이 크다. `.gitignore` 보정
  규칙(`data/processed/*.csv`만 추적)을 확인하고 원자료 전체를 커밋하지 않는다.
