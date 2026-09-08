#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""V3.0 특허 미공개 항목이 public 저장소로 커밋되는 것을 차단한다.

사용:
    python3 tools/patent_scan.py              # 스테이징된 파일 검사 (pre-commit)
    python3 tools/patent_scan.py --all        # 추적 중인 전체 파일 검사
    python3 tools/patent_scan.py --all --report  # 차단하지 않고 목록만 출력

저장소가 private이면 tools/patent_scan.cfg 에 PRIVATE=1 을 두어 비활성화한다.
"""
import os, re, subprocess, sys

# Windows 기본 콘솔 코드페이지(cp949)에서 한글/특수문자 출력 시 죽는 것을 방지한다.
for _stream in (sys.stdout, sys.stderr):
    if hasattr(_stream, "reconfigure"):
        _stream.reconfigure(encoding="utf-8")

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

# V3.0 신규성 관련. 1건이라도 걸리면 커밋을 멈추고 사람이 판단한다.
BLOCK = [
    r"CPD", r"이중\s*루프", r"dual[- ]loop",
    r"적응\s*주파수", r"adaptive\s+frequency",
    r"발명설명서", r"특허명세서", r"청구항", r"claim\s*\d+\s*\.",
    r"10-2026-0050317", r"우선권",
    r"공지예외", r"신규성\s*상실",
]
# 이미 2026-08-03 에 공개된 항목. 차단하지 않고 경고만 한다.
WARN = [r"강성\s*매핑", r"stiffness\s*map", r"EAPD", r"검상\s*돌기", r"xiphoid"]

SKIP_DIRS = {".git", "node_modules", "dist", "dist-single", "__pycache__", ".venv"}

# 스캐너 자신만 제외한다. 패턴 목록을 담고 있어 검사하면 항상 걸린다.
#
# 내용 문서는 여기 넣지 않는다. 예전에 PROJECT_STATE.md·document_index.md·
# DECISIONS.md·AGENTS.md 가 들어 있었고, 그 탓에 PROJECT_STATE.md 의 출원
# 일정과 document_index.md 의 명세 문서 목록이 public 저장소에 올라간 채
# 한 번도 걸리지 않았다. 문서가 키워드를 정말 써야 한다면 SELF_EXEMPT 가
# 아니라 아래 ALLOW_MARKER 를 파일 안에 남긴다 — 그래야 왜 예외인지가
# 파일 자체에 보인다.
SELF_EXEMPT = {
    "tools/patent_scan.py",
    "tools/patent_scan.cfg",
    ".git/hooks/pre-commit",
}
# 파일 어디에든 이 문자열이 있으면 그 파일은 검사에서 제외된다.
ALLOW_MARKER = "patent-scan: allow"
TEXT_EXT = {".md",".txt",".py",".js",".jsx",".mjs",".cjs",".json",".yml",".yaml",
            ".qmd",".bib",".csv",".h",".cpp",".ino",".sh",".cfg",".toml"}

def is_private():
    """tools/patent_scan.cfg 에 PRIVATE=1 이 있으면 검사를 건너뛴다.

    PowerShell 의 `>` 는 파일을 UTF-16LE 로 쓴다. 설정 파일 하나 때문에
    커밋 훅 전체가 죽지 않도록 인코딩을 관대하게 처리한다.
    """
    cfg = os.path.join(HERE, "patent_scan.cfg")
    if not os.path.exists(cfg):
        return False
    try:
        raw = open(cfg, "rb").read()
    except (IOError, OSError):
        return False
    for enc in ("utf-8-sig", "utf-16", "utf-8", "cp949", "latin-1"):
        try:
            if "PRIVATE=1" in raw.decode(enc):
                return True
        except (UnicodeDecodeError, UnicodeError):
            continue
    return False

def files_to_check(all_files):
    if all_files:
        out = subprocess.run(["git","ls-files"], cwd=ROOT,
                             capture_output=True, text=True).stdout
    else:
        out = subprocess.run(["git","diff","--cached","--name-only","--diff-filter=ACM"],
                             cwd=ROOT, capture_output=True, text=True).stdout
    res = []
    for p in out.split("\n"):
        if not p.strip():
            continue
        if any(part in SKIP_DIRS for part in p.split("/")):
            continue
        if os.path.splitext(p)[1].lower() not in TEXT_EXT:
            continue
        if p in SELF_EXEMPT:
            continue
        res.append(p)
    return res

def scan(path):
    fp = os.path.join(ROOT, path)
    try:
        txt = open(fp, encoding="utf8", errors="ignore").read()
    except (IOError, OSError):
        return [], []
    if ALLOW_MARKER in txt:
        return [], []
    b = [p for p in BLOCK if re.search(p, txt, re.I)]
    w = [p for p in WARN if re.search(p, txt, re.I)]
    return b, w

def main():
    all_files = "--all" in sys.argv
    report_only = "--report" in sys.argv
    if is_private() and not report_only:
        print("patent_scan: 저장소가 private로 표시됨. 검사를 건너뜁니다.")
        return 0
    blocked, warned = [], []
    for p in files_to_check(all_files):
        b, w = scan(p)
        if b:
            blocked.append((p, b))
        if w:
            warned.append((p, w))
    for p, w in warned:
        print("  [경고] %s  이미 공개된 항목: %s" % (p, ", ".join(w)))
    if blocked:
        print("")
        print("=" * 62)
        print(" 커밋 중단 — V3.0 특허 미공개 항목이 감지되었습니다")
        print("=" * 62)
        for p, b in blocked:
            print("  [차단] %s" % p)
            for pat in b:
                print("         패턴: %s" % pat)
        print("")
        print(" 이 저장소는 public 이력이 있습니다 (2026-08-03 공개).")
        print(" 해당 내용은 private 저장소 SAVER-docs 로 옮기십시오.")
        print(" 의도한 커밋이면: git commit --no-verify")
        print("")
        return 0 if report_only else 1
    if not warned:
        print("patent_scan: 이상 없음")
    return 0

if __name__ == "__main__":
    sys.exit(main())
