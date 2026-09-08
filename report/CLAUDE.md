# report/ 작업 규칙

- Quarto book. `_quarto.yml`이 chapter 순서(`index.qmd` → `04_conclusion.qmd`)를 정의한다.
  새 절은 여기에 등록하지 않으면 렌더링에 포함되지 않는다.
- 본문에 나오는 모든 수치는 `docs/PROJECT_STATE.md` §2 확정값과 일치해야 한다.
  렌더 전 `python3 tools/consistency_audit.py`를 실행한다.
- 주장에는 반드시 근거 수준을 붙인다 (`docs/evidence.csv`의 `level`). 특히
  `simulated`(CFD, 계산)를 `tested`(실측)로 서술하지 않는다.
- 진공 유지력 상한 290 N은 CFD dead-head 가정이다. 본문에서 "설계 한계"로
  명시하고 실측치처럼 쓰지 않는다.
- `render.sh`는 Quarto → pandoc → (둘 다 없으면) 순수 파이썬 폴백 순서로 시도한다.
  제출본은 반드시 Quarto로 최소 1회 렌더링해 `@fig-`/`@tbl-` 교차참조 번호를 확인한다.
- `templates/ksme-reference.docx`가 있으면 `_quarto.yml`의 `reference-doc`으로 자동 적용된다.
