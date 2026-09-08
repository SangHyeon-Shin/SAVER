#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""파일 확장자와 실제 형식이 어긋난 항목을 진단하고(선택적으로) 교정한다.

사용:
    python3 tools/fix_extensions.py <dir>            # 진단만
    python3 tools/fix_extensions.py <dir> --apply     # 교정 (파일명 변경)

docs/specs 처럼 "원본 보존" 정책이 있는 디렉터리에서 --apply를 쓰면 파일 내용은
그대로 두고 확장자만 실제 형식에 맞게 바꾼다. 내용을 바꾸지 않으므로 정책과
충돌하지 않지만, 공유 저장소에 반영하기 전에는 팀과 확인하십시오.
"""
import os, sys, zipfile

for _stream in (sys.stdout, sys.stderr):
    if hasattr(_stream, "reconfigure"):
        _stream.reconfigure(encoding="utf-8")

SKIP_DIRS = {".git", "node_modules", "__pycache__", ".venv"}

def sniff(path):
    try:
        with open(path, "rb") as f:
            head = f.read(8)
    except (IOError, OSError):
        return None

    if head.startswith(b"%PDF"):
        return "pdf"

    if head.startswith(b"PK\x03\x04"):
        try:
            with zipfile.ZipFile(path) as z:
                names = set(z.namelist())
        except zipfile.BadZipFile:
            return "zip"
        if "word/document.xml" in names:
            return "docx"
        if any(n.startswith("xl/") for n in names):
            return "xlsx"
        if any(n.startswith("ppt/") for n in names):
            return "pptx"
        if "[Content_Types].xml" in names:
            return "zip"  # openxml 계열이지만 word/xl/ppt 어디에도 속하지 않음
        return "zip"

    try:
        with open(path, "rb") as f:
            data = f.read(4096)
        # 4096바이트 경계에서 멀티바이트 UTF-8 문자가 잘렸을 수 있으니
        # 끝에서부터 최대 3바이트를 줄여가며 재시도한다.
        for trim in range(0, 4):
            chunk = data[:len(data) - trim] if trim else data
            try:
                chunk.decode("utf-8")
                break
            except UnicodeDecodeError:
                continue
        else:
            raise UnicodeDecodeError("utf-8", data, 0, 1, "truncated")
        if b"\x00" not in data:
            return "txt"
    except (UnicodeDecodeError, IOError, OSError):
        pass
    return None

EXPECT = {
    "pdf": {"pdf"},
    "docx": {"docx"},
    "xlsx": {"xlsx"},
    "pptx": {"pptx"},
    "zip": {"zip"},
    "txt": {"txt", "md", "csv"},
}

def walk(root):
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [d for d in dirnames if d not in SKIP_DIRS]
        for fn in filenames:
            yield os.path.join(dirpath, fn)

def main():
    if len(sys.argv) < 2:
        print("사용: python3 tools/fix_extensions.py <dir> [--apply]")
        return 2
    root = sys.argv[1]
    apply = "--apply" in sys.argv[2:]

    mismatches = []
    for path in walk(root):
        ext = os.path.splitext(path)[1].lstrip(".").lower()
        detected = sniff(path)
        if detected is None:
            continue
        if ext in EXPECT.get(detected, set()):
            continue
        if detected == "txt" and ext in EXPECT["txt"]:
            continue
        mismatches.append((path, ext or "(없음)", detected))

    if not mismatches:
        print("확장자 불일치 없음: %s" % root)
        return 0

    print("=" * 62)
    print(" 확장자 불일치 %d건" % len(mismatches))
    print("=" * 62)
    for path, ext, detected in mismatches:
        print("  %-70s  .%s 선언 -> 실제 %s" % (path, ext, detected))

    if not apply:
        print("")
        print("--apply 로 실행하면 파일명을 실제 형식에 맞게 바꿉니다 (내용은 변경하지 않음).")
        return 1

    print("")
    for path, ext, detected in mismatches:
        base, _ = os.path.splitext(path)
        new_path = "%s.%s" % (base, detected)
        if os.path.exists(new_path):
            print("  건너뜀 (대상 이미 존재): %s" % new_path)
            continue
        os.rename(path, new_path)
        print("  변경: %s -> %s" % (path, new_path))
    return 0

if __name__ == "__main__":
    sys.exit(main())
