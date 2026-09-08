#!/usr/bin/env bash
# report/ 변환 리허설. quarto -> pandoc -> 순수 파이썬 폴백 순으로 시도한다.
# 제출본은 반드시 quarto로 최소 1회 렌더링하십시오 (교차참조 번호는 폴백에서 해결되지 않습니다).
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT="$HERE/../release/_build"
mkdir -p "$OUT"

QMDS=(index.qmd 01_intro.qmd 02_design.qmd 03_verification.qmd 04_conclusion.qmd 90_appendix.qmd)

if command -v quarto >/dev/null 2>&1; then
  echo "[render] quarto 렌더링"
  (cd "$HERE" && quarto render)
  exit 0
fi

if command -v pandoc >/dev/null 2>&1; then
  echo "[render] quarto 없음 -> pandoc 대체 변환 (교차참조 자동 번호화 안 됨)"
  : > "$OUT/_merged.md"
  for f in "${QMDS[@]}"; do cat "$HERE/$f" >> "$OUT/_merged.md"; echo "" >> "$OUT/_merged.md"; done
  pandoc "$OUT/_merged.md" --from=markdown --to=docx -o "$OUT/report.docx"
  echo "[render] 완료: $OUT/report.docx (pandoc)"
  exit 0
fi

echo "[render] quarto·pandoc 모두 없음 -> 순수 파이썬 폴백 (변환 리허설 전용, 제출 불가)"
PYBIN="$(command -v python3 || command -v python || command -v py)"
"$PYBIN" - "$HERE" "$OUT/report.docx" "${QMDS[@]}" <<'PYEOF'
import sys, re
from docx import Document

for _stream in (sys.stdout, sys.stderr):
    if hasattr(_stream, "reconfigure"):
        _stream.reconfigure(encoding="utf-8")

here, out = sys.argv[1], sys.argv[2]
qmds = sys.argv[3:]

doc = Document()
body_chars = 0
table_count = 0

def strip_yaml(text):
    if text.startswith("---"):
        end = text.find("\n---", 3)
        if end != -1:
            return text[end+4:]
    return text

def flush_table(doc, rows):
    global table_count
    if len(rows) < 2:
        return
    header = rows[0]
    data_rows = [r for r in rows[1:] if not re.match(r'^[\s|:-]+$', r)]
    cols = [c.strip() for c in header.strip().strip('|').split('|')]
    t = doc.add_table(rows=1, cols=len(cols))
    for i, c in enumerate(cols):
        t.rows[0].cells[i].text = c
    for r in data_rows:
        cells = [c.strip() for c in r.strip().strip('|').split('|')]
        row = t.add_row()
        for i, c in enumerate(cells[:len(cols)]):
            row.cells[i].text = c
    table_count += 1

for fname in qmds:
    with open(f"{here}/{fname}", encoding="utf-8") as fh:
        text = strip_yaml(fh.read())
    lines = text.split("\n")
    i = 0
    while i < len(lines):
        line = lines[i]
        stripped = line.strip()
        if stripped.startswith("!["):
            doc.add_paragraph(f"[그림 자리표시자: {stripped}]")
            i += 1
            continue
        if stripped.startswith("|"):
            table_lines = []
            while i < len(lines) and lines[i].strip().startswith("|"):
                table_lines.append(lines[i])
                i += 1
            flush_table(doc, table_lines)
            continue
        m = re.match(r'^(#{1,3})\s+(.*)', stripped)
        if m:
            level = len(m.group(1))
            heading_text = re.sub(r'\{.*?\}', '', m.group(2)).strip()
            doc.add_heading(heading_text, level=level)
            body_chars += len(heading_text)
        elif stripped:
            # 폴백 렌더러는 실제 인용/교차참조 서식을 해결하지 않는다 — 자리표시자로만 남긴다.
            clean = re.sub(r'\[@([\w-]+)\]', r'(참조:\1)', stripped)
            clean = re.sub(r'@([\w-]+)', r'(참조:\1)', clean)
            doc.add_paragraph(clean)
            body_chars += len(clean)
        i += 1

doc.save(out)
print(f"[render] 완료(파이썬 폴백): {out}")
print(f"[render] 본문 문자수: {body_chars}, 표 개수: {table_count}")
PYEOF
