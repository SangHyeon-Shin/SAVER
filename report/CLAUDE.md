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

## 그림 (figures/)

- `figures/*.svg`가 원본이고 `figures/*.png`는 변환 산출물이다. **docx는
  `rsvg-convert` 없이 SVG를 삽입하지 못하므로 qmd에서는 PNG를 참조한다.**
- SVG를 수정하면 PNG를 다시 만든다. Windows에서는 Edge 헤드리스가 시스템 한글
  폰트를 쓰므로 결과가 깨지지 않는다.

```bash
"/c/Program Files (x86)/Microsoft/Edge/Application/msedge.exe" \
  --headless --disable-gpu --force-device-scale-factor=3 --window-size=520,310 \
  --screenshot="<대상>.png" "<원본>.svg"
```

  변환 후 여백은 잘라낸다(PIL `ImageChops.difference` + `getbbox`).
  `svglib`+`reportlab` 경로는 한글이 두부 글자로 깨지므로 쓰지 않는다.
