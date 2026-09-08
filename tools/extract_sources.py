#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""sources/original (또는 지정 디렉터리)의 원본에서 검색용 평문 텍스트를 뽑아
sources/extracted/ 아래 같은 상대경로 + .txt 로 저장한다.

사용:
    python3 tools/extract_sources.py [dir]   # 기본값: sources/original

지원 형식:
    .txt/.md/.csv  그대로 복사
    .zip           내부 *.txt 를 페이지 순서로 이어 붙임 (스캔 문서 컨테이너)
    .docx          python-docx 가 있으면
    .pdf           실제 PDF 이고 pdfplumber/PyPDF2 가 있으면

지원 라이브러리가 없거나 파일이 선언된 형식과 다르면 건너뛰고 이유를 출력한다.
먼저 tools/fix_extensions.py 로 실제 형식을 확인하십시오.

주의: 추출 결과는 원본을 검색 가능한 평문으로 만든다. public 저장소에서는
sources/extracted/ 를 추적하지 않는다(.gitignore). private 저장소에서만 추적하십시오.
"""
import os, sys

for _stream in (sys.stdout, sys.stderr):
    if hasattr(_stream, "reconfigure"):
        _stream.reconfigure(encoding="utf-8")

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

def read_docx(path):
    try:
        import docx
    except ImportError:
        return None, "python-docx 미설치"
    d = docx.Document(path)
    return "\n".join(p.text for p in d.paragraphs), None

def read_pdf(path):
    try:
        import pdfplumber
    except ImportError:
        try:
            from PyPDF2 import PdfReader
        except ImportError:
            return None, "pdfplumber/PyPDF2 미설치"
        try:
            reader = PdfReader(path)
        except Exception as e:
            return None, "PDF 파싱 실패: %s (fix_extensions.py로 실제 형식 확인)" % e
        return "\n".join((p.extract_text() or "") for p in reader.pages), None
    try:
        with pdfplumber.open(path) as pdf:
            return "\n".join((p.extract_text() or "") for p in pdf.pages), None
    except Exception as e:
        return None, "PDF 파싱 실패: %s (fix_extensions.py로 실제 형식 확인)" % e

def read_zip(path):
    """스캔 문서 컨테이너(.zip). 내부 *.txt 를 순서대로 이어 붙인다.

    docs/specs 의 문서들이 이 형태다(페이지 이미지 + 페이지별 텍스트).
    """
    import zipfile
    try:
        with zipfile.ZipFile(path) as z:
            names = [n for n in z.namelist() if n.lower().endswith(".txt")]
            if not names:
                imgs = [n for n in z.namelist()
                        if n.lower().endswith((".jpg", ".jpeg", ".png"))]
                return None, ("텍스트 없음 (이미지 %d장) — OCR 필요" % len(imgs)
                              if imgs else "텍스트 없음")

            def page_no(n):
                base = os.path.splitext(os.path.basename(n))[0]
                return (0, int(base)) if base.isdigit() else (1, 0)

            parts = []
            for n in sorted(names, key=page_no):
                parts.append("<!-- %s -->\n%s" % (n, z.read(n).decode("utf-8", "replace")))
            return "\n\n".join(parts), None
    except zipfile.BadZipFile as e:
        return None, "ZIP 파싱 실패: %s" % e

def read_text(path):
    try:
        with open(path, encoding="utf-8") as f:
            return f.read(), None
    except UnicodeDecodeError as e:
        return None, "UTF-8 디코딩 실패: %s" % e

def main():
    src = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "sources", "original")
    dst = os.path.join(ROOT, "sources", "extracted")
    if not os.path.isdir(src):
        print("원본 디렉터리 없음: %s" % src)
        return 2

    ok, skipped = 0, []
    for dirpath, _, filenames in os.walk(src):
        for fn in filenames:
            path = os.path.join(dirpath, fn)
            ext = os.path.splitext(fn)[1].lower()
            rel = os.path.relpath(path, src)

            if ext in (".txt", ".md", ".csv"):
                text, err = read_text(path)
            elif ext == ".docx":
                text, err = read_docx(path)
            elif ext == ".pdf":
                text, err = read_pdf(path)
            elif ext == ".zip":
                text, err = read_zip(path)
            else:
                text, err = None, "미지원 확장자 %s" % ext

            if err:
                skipped.append((rel, err))
                continue

            out_path = os.path.join(dst, rel + ".txt")
            os.makedirs(os.path.dirname(out_path), exist_ok=True)
            with open(out_path, "w", encoding="utf-8") as f:
                f.write(text or "")
            ok += 1
            print("  추출: %s" % rel)

    print("")
    print("완료: %d건 추출, %d건 건너뜀" % (ok, len(skipped)))
    for rel, err in skipped:
        print("  건너뜀: %s (%s)" % (rel, err))
    return 0

if __name__ == "__main__":
    sys.exit(main())
