#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""실행 중인 SAVER 브리지에 측정 구간 표시를 전송한다.

사용 예:
  python3 -m saver.mark "trial_1 depth 50mm"
  python3 -m saver.mark trial_1 --note "load=30kg voltage=24V surface=mat"
"""
import argparse
import asyncio
import json
import sys


def split_mark(text, note=None):
    """한 인자 형식에서는 첫 단어를 label, 나머지를 note로 나눈다."""
    text = str(text or "").strip()
    if note is not None:
        return text, str(note).strip()
    label, sep, inferred_note = text.partition(" ")
    return label, inferred_note.strip() if sep else ""


def build_message(label, note=""):
    return {"type": "mark", "label": label, "note": note}


async def send_mark(url, label, note, timeout_s=3.0):
    try:
        import websockets
    except ImportError:
        raise RuntimeError("websockets 가 필요합니다: pip3 install 'websockets==9.1'")
    async with websockets.connect(url) as ws:
        await ws.send(json.dumps(build_message(label, note), ensure_ascii=False))
        while True:
            raw = await asyncio.wait_for(ws.recv(), timeout=timeout_s)
            msg = json.loads(raw)
            if msg.get("type") == "marked":
                return msg


def main(argv=None):
    ap = argparse.ArgumentParser(description="SAVER CSV 측정 구간 표시")
    ap.add_argument("mark", help="label 또는 'label note...' 문자열")
    ap.add_argument("--note", default=None, help="측정 조건 자유 문자열")
    ap.add_argument("--url", default="ws://127.0.0.1:8765",
                    help="브리지 WebSocket URL")
    a = ap.parse_args(argv)

    label, note = split_mark(a.mark, a.note)
    if not label:
        ap.error("label은 비어 있을 수 없습니다")
    try:
        result = asyncio.get_event_loop().run_until_complete(
            send_mark(a.url, label, note))
    except Exception as exc:
        print("mark 전송 실패: %s" % exc, file=sys.stderr)
        return 1
    if not result.get("ok"):
        print("mark 기록 실패: %s" % result.get("why", "UNKNOWN"), file=sys.stderr)
        return 1
    print("mark 기록: %s%s" %
          (result.get("label", label), " · " + note if note else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
