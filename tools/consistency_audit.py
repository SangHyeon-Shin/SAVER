#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""확정값이 문서 전체에 일관되게 반영되었는지 검사한다.

EAPD 계수 오류가 4개소로 전파된 전례가 있다. 제출 전 반드시 실행한다.

사용:
    python3 tools/consistency_audit.py
    python3 tools/consistency_audit.py --json
"""
import json, os, re, subprocess, sys

for _stream in (sys.stdout, sys.stderr):
    if hasattr(_stream, "reconfigure"):
        _stream.reconfigure(encoding="utf-8")

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
STATE = os.path.join(ROOT, "docs", "PROJECT_STATE.md")

SKIP_DIRS = {".git", "node_modules", "dist", "dist-single", "__pycache__",
             ".venv", "docs/specs", "sources/original", "data/raw"}
TEXT_EXT = {".md",".qmd",".txt",".py",".js",".jsx",".json",".h",".cpp",".ino",".csv",".bib",".svg"}

# (이름, 정답값, 단위, 이 값을 나타내는 것으로 보이는 숫자를 찾는 정규식)
def load_canonical():
    """PROJECT_STATE.md §2 표에서 확정값을 읽는다."""
    if not os.path.exists(STATE):
        print("PROJECT_STATE.md 없음"); sys.exit(2)
    txt = open(STATE, encoding="utf8").read()
    sec = txt.split("## 2.")[1].split("## 3.")[0] if "## 2." in txt else txt
    vals = {}
    for line in sec.split("\n"):
        if not line.strip().startswith("|"):
            continue
        cells = [c.strip() for c in line.strip().strip("|").split("|")]
        if len(cells) < 2 or cells[0] in ("항목", "---"):
            continue
        m = re.search(r"[-−]?\d+(?:\.\d+)?", cells[1])
        if m:
            vals[cells[0].strip("`")] = (m.group(0).replace("−", "-"), cells[1])
    return vals

# 검사할 값: 라벨 → (정답, 오답으로 자주 나타나는 값들)
CHECKS = [
    ("진공 목표압력",  "-22.5", ["-20.0", "-20", "-25", "-22", "-30"], r"(?:VAC_TARGET|목표\s*압력|흡착\s*압력)"),
    ("BPM",            "110",   ["100", "102", "120"],                 r"(?:BPM|압박\s*속도)"),
    ("볼스크류 리드",  "10",    ["5", "8", "12"],                      r"(?:리드|lead)"),
    ("감속비",         "1.5",   ["1.0", "2.0", "2.5"],                 r"(?:감속비|gear\s*ratio)"),
    ("최대 압박력",    "555.1", ["500", "550", "555", "600"],          r"(?:압박력|compression\s*force)"),
    ("유지력 상한",    "290",   ["300", "280", "250"],                 r"(?:유지력|holding\s*force)"),
    ("전류 제한",      "12.0",  ["10.0", "15.0", "16.0"],              r"(?:I_LIMIT|전류\s*제한)"),
    ("온도 정지",      "70.0",  ["60", "65", "80"],                    r"(?:TEMP_STOP|온도\s*정지)"),
]

def walk():
    out = subprocess.run(["git","ls-files"], cwd=ROOT, capture_output=True, text=True).stdout
    for p in out.split("\n"):
        if not p.strip():
            continue
        if any(p.startswith(d) for d in SKIP_DIRS):
            continue
        if any(part in SKIP_DIRS for part in p.split("/")):
            continue
        if os.path.splitext(p)[1].lower() not in TEXT_EXT:
            continue
        yield p

def main():
    as_json = "--json" in sys.argv
    canon = load_canonical()
    findings = []
    for rel in walk():
        fp = os.path.join(ROOT, rel)
        try:
            lines = open(fp, encoding="utf8", errors="ignore").read().split("\n")
        except (IOError, OSError):
            continue
        for i, line in enumerate(lines, 1):
            for label, good, bads, ctx in CHECKS:
                if not re.search(ctx, line, re.I):
                    continue
                nums = re.findall(r"[-−]?\d+(?:\.\d+)?", line)
                nums = [n.replace("−", "-") for n in nums]
                if good in nums or any(n.rstrip("0").rstrip(".") == good.rstrip("0").rstrip(".") for n in nums):
                    continue
                hit = [n for n in nums if n in bads]
                if hit:
                    findings.append({"file": rel, "line": i, "label": label,
                                     "expected": good, "found": hit,
                                     "text": line.strip()[:110]})
    if as_json:
        print(json.dumps(findings, ensure_ascii=False, indent=2))
        return 0
    print("확정값 출처: docs/PROJECT_STATE.md  (%d개 파싱)" % len(canon))
    print("검사 대상 항목: %d" % len(CHECKS))
    print("")
    if not findings:
        print("불일치 없음.")
        return 0
    print("=" * 62)
    print(" 불일치 %d건" % len(findings))
    print("=" * 62)
    cur = None
    for f in findings:
        if f["file"] != cur:
            cur = f["file"]
            print("\n%s" % cur)
        print("  L%-5d [%s] 기대 %s / 발견 %s" %
              (f["line"], f["label"], f["expected"], ", ".join(f["found"])))
        print("         %s" % f["text"])
    print("")
    print("각 건이 오류인지 의도된 다른 값인지 사람이 판단하십시오.")
    print("의도된 값이면 CHECKS 의 오답 목록에서 제거하십시오.")
    return 1

if __name__ == "__main__":
    sys.exit(main())
