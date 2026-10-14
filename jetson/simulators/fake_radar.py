#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""fake_radar.py — 가짜 MR60BHA2 레이더 (하드웨어 없이 시험)

가상 시리얼 포트(pty)를 만들고 MR60BHA2 프레임(saver/radar.py 형식)을 보낸다.
값은 시험용이며 의학적 근거가 없다.

  python3 simulators/fake_radar.py --port /tmp/ttyRADAR
  python3 -m saver.bridge --radar /tmp/ttyRADAR --config config.sim.json

옵션
  --rr 14 --hr 72         호흡수, 심박수(회/분). 0 이면 측정 못 함(0)을 보낸다
  --distance-cm 29.4      레이더~가슴 거리(cm)
  --hz 1                  초당 보내는 묶음 수
  --garbage               프레임 사이에 깨진 바이트를 섞는다(다시 맞추기 시험)
  --no-person             사람 감지 0 을 보낸다
"""
import argparse
import os
import pty
import random
import signal
import struct
import sys
import termios
import time
import tty

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from saver.radar import (encode_frame, T_BREATH, T_HEART, T_DISTANCE,   # noqa: E402
                         T_PRESENCE, T_PHASE)


class FakeRadar(object):

    def __init__(self, rr=14.0, hr=72.0, distance_cm=29.4, person=True, garbage=False, seed=None):
        self.master, self.slave = pty.openpty()
        tty.setraw(self.master)
        tty.setraw(self.slave)
        attr = termios.tcgetattr(self.slave)
        attr[3] = attr[3] & ~termios.ECHO
        termios.tcsetattr(self.slave, termios.TCSANOW, attr)
        self.port = os.ttyname(self.slave)
        self.rng = random.Random(seed)
        self.rr, self.hr, self.distance_cm = rr, hr, distance_cm
        self.person, self.garbage = person, garbage
        self.fid = 0

    def _frame(self, ftype, data):
        self.fid = (self.fid + 1) & 0xFFFF
        return encode_frame(ftype, data, self.fid)

    def burst(self):
        j = self.rng.uniform
        out = [self._frame(T_PRESENCE, struct.pack("<H", 1 if self.person else 0))]
        if self.person:
            rr = self.rr + j(-0.5, 0.5) if self.rr else 0.0
            hr = self.hr + j(-1.5, 1.5) if self.hr else 0.0
            d = self.distance_cm + j(-0.3, 0.3)
            out.append(self._frame(T_PHASE, struct.pack("<fff", j(-1, 1), j(-1, 1), j(-1, 1))))
            out.append(self._frame(T_BREATH, struct.pack("<f", rr)))
            out.append(self._frame(T_HEART, struct.pack("<f", hr)))
            out.append(self._frame(T_DISTANCE, struct.pack("<If", 1, d)))
        if self.garbage:
            junk = bytes(self.rng.randrange(256) for _ in range(self.rng.randrange(1, 12)))
            out.insert(self.rng.randrange(len(out) + 1), b"\x01" + junk)   # 가짜 SOF 로 시작하는 쓰레기
        return b"".join(out)

    def run(self, hz=1.0, duration=None):
        t_end = time.monotonic() + duration if duration else None
        while t_end is None or time.monotonic() < t_end:
            os.write(self.master, self.burst())
            time.sleep(1.0 / hz)


def main():
    ap = argparse.ArgumentParser(description="가짜 MR60BHA2 레이더 (시험용, 의학적 근거 없음)")
    ap.add_argument("--port", default="/tmp/ttyRADAR", help="이 경로에 가상 포트 심볼릭 링크를 만든다")
    ap.add_argument("--rr", type=float, default=14.0)
    ap.add_argument("--hr", type=float, default=72.0)
    ap.add_argument("--distance-cm", type=float, default=29.4)
    ap.add_argument("--hz", type=float, default=1.0)
    ap.add_argument("--seed", type=int, default=None)
    ap.add_argument("--garbage", action="store_true")
    ap.add_argument("--no-person", action="store_true")
    ap.add_argument("--duration", type=float, default=None, help="초. 없으면 Ctrl+C 까지")
    ap.add_argument("--quiet", action="store_true")
    a = ap.parse_args()

    fake = FakeRadar(a.rr, a.hr, a.distance_cm, not a.no_person, a.garbage, a.seed)
    signal.signal(signal.SIGTERM, lambda *_: sys.exit(0))      # 종료 신호에도 심볼릭 링크를 지운다
    try:
        if os.path.islink(a.port) or os.path.exists(a.port):
            os.remove(a.port)
        os.symlink(fake.port, a.port)
    except OSError as e:
        print("[warn] 심볼릭 링크 실패: %s" % e)
    if not a.quiet:
        print("가짜 레이더: %s → %s, 호흡 %s, 심박 %s, 거리 %s cm, %.1f Hz"
              % (a.port, fake.port, a.rr, a.hr, a.distance_cm, a.hz))
    sys.stdout.flush()
    try:
        fake.run(a.hz, a.duration)
    except KeyboardInterrupt:
        pass
    finally:
        try:
            os.remove(a.port)
        except OSError:
            pass


if __name__ == "__main__":
    main()
