# -*- coding: utf-8 -*-
"""radar.py — MR60BHA2(Seeed 60 GHz 레이더) 판독: 호흡수, 심박수, 거리, 가슴 두께(EAPD)

연결
  레이더 UART(115200 8N1, 3.3 V) → 젯슨 40핀 UART /dev/ttyTHS1(8번 TX, 10번 RX) 또는 USB 직렬 변환기.
  브리지 --radar PORT 로 켠다. 주지 않으면 레이더 기능을 쓰지 않는다.
  레이더는 켜지면 스스로 프레임을 보낸다. 젯슨은 읽기만 하고 명령을 보내지 않는다.

프레임 (Seeed TinyFrame 형식. ESPHome seeed_mr60bha2 구성 요소와 같은 해석)
  SOF 0x01 | ID 2 | LEN 2 | TYPE 2 | 머리 체크섬 | DATA(LEN) | 데이터 체크섬     (ID, LEN, TYPE 은 big-endian)
  체크섬 = 바이트 XOR 의 비트 반전. 머리는 SOF~TYPE 7 바이트, 데이터는 DATA.
  TYPE 0x0A14 호흡수    DATA[0:4] float32 little-endian, 회/분
       0x0A15 심박수    DATA[0:4] float32 little-endian, 회/분
       0x0A16 거리      DATA[0:4] uint32 유효 표시, DATA[4:8] float32, cm (유효 표시 0 이면 무효)
       0x0F09 사람 감지 DATA[0:2] (0 이면 없음 → 호흡수, 심박수, 거리를 비운다)
       그 밖의 TYPE(위상 0x0A13, 점군 0x0A04 등)은 체크섬만 확인하고 넘긴다
  값 0 은 측정하지 못한 것이다. 0 을 화면과 기록에 쓰지 않고 비운다(프로토콜 v1 §4.3 과 같은 원칙).
  거리 단위 cm 는 ESPHome 구현 기준이다. 실물에서 자로 잰 거리와 대조해 확인한다(python3 -m saver.radar PORT).

가슴 두께 (EAPD)
  EAPD = radar_floor_mm − (최근 EAPD_WINDOW_S 초 안 유효 거리의 중앙값, EAPD_MIN_N 개 이상)
  radar_floor_mm 은 탐색 자세(바퀴 내림)에서 레이더 면의 바닥 위 높이다. 레이더가 가슴 바로 위에서
  바닥 쪽을 향한다고 가정한다(설치 위치 미확정). 비어 있으면 EAPD 를 만들지 않고, 탐색은
  EAPD_MISSING 으로 시작하지 않는다(대체 상수 없음, 프로토콜 v1.1 초안 §0.1-3).
  EAPD 로 압박점 거리 D 를 계산해 Teensy 에 주행 거리(DRIVE_STEP)로 요청하는 것은 search.py 다.

이 모듈의 값은 표시와 기록용이다. 호흡수, 심박수로 판단(회복 판정 등)을 하지 않는다.
"""
import collections
import math
import os
import struct
import threading
import time

try:
    import serial                       # pyserial
    HAVE_PYSERIAL = True
except ImportError:                     # 없으면 raw 파일 IO 로 대체 (pty 시험용)
    HAVE_PYSERIAL = False

SOF = 0x01
T_PHASE = 0x0A13
T_BREATH = 0x0A14
T_HEART = 0x0A15
T_DISTANCE = 0x0A16
T_PRESENCE = 0x0F09
T_CLOUD = 0x0A04

BAUD = 115200
MAX_LEN = 512           # 이보다 긴 LEN 은 깨진 머리로 본다
BUF_MAX = 4096          # 프레임을 못 찾은 채 쌓인 바이트 상한
FRESH_S = 5.0           # 값의 표시 유효 시간. 넘으면 비운다(자료 신선도, 의학 기준 아님)
RX_ALIVE_S = 3.0        # 이 시간 안에 프레임을 받았으면 '수신 중'
EAPD_WINDOW_S = 5.0     # EAPD 에 쓰는 거리 표본 구간
EAPD_MIN_N = 3          # EAPD 에 필요한 최소 표본 수(잡음 완화, 의학 기준 아님)

VALUE_KEYS = ("rr_bpm", "hr_bpm", "distance_mm")


def checksum(data):
    """바이트 XOR 의 비트 반전(ESPHome seeed_mr60bha2 calculate_checksum)."""
    c = 0
    for b in bytearray(data):
        c ^= b
    return (~c) & 0xFF


def encode_frame(ftype, data=b"", fid=0):
    """프레임 만들기(모의 레이더와 시험용)."""
    data = bytes(data)
    head = struct.pack(">BHHH", SOF, fid & 0xFFFF, len(data), ftype & 0xFFFF)
    return head + bytes([checksum(head)]) + data + bytes([checksum(data)])


def _num(v):
    return isinstance(v, (int, float)) and not isinstance(v, bool) and math.isfinite(v)


def _pos(v):
    """양의 유한수만 측정값으로 본다. 0, 음수, NaN 은 측정하지 못한 것이다."""
    return v if _num(v) and v > 0 else None


class FrameParser(object):
    """바이트를 받아 완성된 프레임 (TYPE, DATA, ID) 목록을 돌려준다. 깨진 바이트는 건너뛴다."""

    def __init__(self):
        self.buf = bytearray()
        self.frames = 0
        self.bad = 0            # 체크섬 실패, 길이 이상으로 버린 머리
        self.skipped = 0        # SOF 를 찾으며 버린 바이트

    def feed(self, data):
        self.buf.extend(data)
        out = []
        while True:
            i = self.buf.find(b"\x01")
            if i < 0:
                self.skipped += len(self.buf)
                del self.buf[:]
                break
            if i > 0:
                self.skipped += i
                del self.buf[:i]
            if len(self.buf) < 8:
                break
            if checksum(self.buf[:7]) != self.buf[7]:
                self.bad += 1
                del self.buf[0]
                continue
            length = (self.buf[3] << 8) | self.buf[4]
            if length > MAX_LEN:
                self.bad += 1
                del self.buf[0]
                continue
            total = 8 + length + 1
            if len(self.buf) < total:
                break
            data = bytes(self.buf[8:8 + length])
            if checksum(data) != self.buf[8 + length]:
                self.bad += 1
                del self.buf[0]
                continue
            fid = (self.buf[1] << 8) | self.buf[2]
            ftype = (self.buf[5] << 8) | self.buf[6]
            del self.buf[:total]
            self.frames += 1
            out.append((ftype, data, fid))
        if len(self.buf) > BUF_MAX:              # 머리만 맞고 끝나지 않는 쓰레기가 쌓이지 않게
            self.skipped += len(self.buf) - BUF_MAX
            del self.buf[:len(self.buf) - BUF_MAX]
        return out


def decode(ftype, data):
    """프레임 하나를 값 dict 로. 알 수 없거나 짧으면 빈 dict. 측정 못 함은 None."""
    if ftype == T_BREATH and len(data) >= 4:
        return {"rr_bpm": _pos(struct.unpack_from("<f", data, 0)[0])}
    if ftype == T_HEART and len(data) >= 4:
        return {"hr_bpm": _pos(struct.unpack_from("<f", data, 0)[0])}
    if ftype == T_DISTANCE and len(data) >= 8:
        flag = struct.unpack_from("<I", data, 0)[0]
        cm = _pos(struct.unpack_from("<f", data, 4)[0]) if flag else None
        return {"distance_mm": cm * 10.0 if cm is not None else None}
    if ftype == T_PRESENCE and len(data) >= 1:
        return {"presence": bool(bytearray(data)[0] or (len(data) > 1 and bytearray(data)[1]))}
    return {}


def eapd_estimate(samples, floor_mm, now, window_s=EAPD_WINDOW_S, min_n=EAPD_MIN_N):
    """EAPD = floor_mm − 최근 거리 중앙값. 설정이 없거나 표본이 모자라거나 0 이하면 None."""
    if not _num(floor_mm) or floor_mm <= 0:
        return None
    xs = sorted(d for t, d in samples if 0 <= now - t <= window_s)
    if len(xs) < min_n:
        return None
    n = len(xs)
    med = xs[n // 2] if n % 2 else (xs[n // 2 - 1] + xs[n // 2]) / 2.0
    e = float(floor_mm) - med
    return e if e > 0 else None


class RadarState(object):
    """수신한 값과 시각. 판독 스레드가 쓰고 브리지(asyncio)가 읽는다."""

    def __init__(self, clock=time.monotonic):
        self.clock = clock
        self._lock = threading.Lock()
        self._vals = {}                                     # 이름 -> (값, 시각)
        self._dist = collections.deque(maxlen=64)           # (시각, mm)
        self.last_rx = None

    def apply(self, vals):
        with self._lock:
            now = self.clock()
            self.last_rx = now
            if vals.get("presence") is False:               # 사람이 없으면 이전 값을 지운다
                for k in VALUE_KEYS:
                    self._vals.pop(k, None)
                self._dist.clear()
            for k, v in vals.items():
                if v is None:
                    self._vals.pop(k, None)
                    continue
                self._vals[k] = (v, now)
                if k == "distance_mm":
                    self._dist.append((now, v))

    def touch(self):
        """값이 없는 프레임(위상, 점군)도 수신으로 센다."""
        with self._lock:
            self.last_rx = self.clock()

    def snapshot(self, floor_mm=None):
        with self._lock:
            now = self.clock()
            out = {"receiving": self.last_rx is not None and now - self.last_rx <= RX_ALIVE_S}
            for k in VALUE_KEYS + ("presence",):
                if k in self._vals and now - self._vals[k][1] <= FRESH_S:
                    v = self._vals[k][0]
                    out[k] = round(v, 1) if _num(v) else v
            e = eapd_estimate(list(self._dist), floor_mm, now)
            if e is not None:
                out["eapd_mm"] = round(e, 1)
            out["floor_set"] = _num(floor_mm) and floor_mm > 0
            return out


class RadarLink(object):
    """레이더 직렬 포트 판독 스레드. 포트가 없거나 끊기면 다시 연다."""

    def __init__(self, port, baud=BAUD, on_link=None, clock=time.monotonic):
        self.port = port
        self.baud = baud
        self.on_link = on_link or (lambda connected, port: None)
        self.state = RadarState(clock=clock)
        self.parser = FrameParser()
        self.connected = False
        self._ser = None
        self._stop = threading.Event()
        self._thread = None

    def _open(self):
        if not self.port or not os.path.exists(self.port):
            return False
        try:
            if HAVE_PYSERIAL:
                self._ser = serial.Serial(self.port, self.baud, timeout=0)
            else:
                from .teensy_serial import _RawPort
                self._ser = _RawPort(self.port)
        except Exception:
            self._ser = None
            return False
        self.parser = FrameParser()
        self.connected = True
        self.on_link(True, self.port)
        return True

    def _close(self):
        if self._ser is not None:
            try:
                self._ser.close()
            except Exception:
                pass
        self._ser = None
        if self.connected:
            self.connected = False
            self.on_link(False, None)

    def _read_once(self):
        try:
            data = self._ser.read(256)
        except Exception:                   # USB 변환기를 뽑은 경우 등
            self._close()
            return
        if not data:
            return
        for ftype, payload, _fid in self.parser.feed(data):
            vals = decode(ftype, payload)
            if vals:
                self.state.apply(vals)
            else:
                self.state.touch()

    def _loop(self):
        backoff = 0.5
        while not self._stop.is_set():
            if self._ser is None:
                if self._open():
                    backoff = 0.5
                else:
                    self._stop.wait(backoff)
                    backoff = min(backoff * 1.6, 5.0)
                    continue
            self._read_once()
            self._stop.wait(0.01)
        self._close()

    def start(self):
        self._thread = threading.Thread(target=self._loop)
        self._thread.daemon = True
        self._thread.start()
        return self

    def stop(self):
        self._stop.set()
        if self._thread is not None:
            self._thread.join(timeout=2.0)

    def snapshot(self, floor_mm=None):
        out = self.state.snapshot(floor_mm)
        out.update({"enabled": True, "port": self.port, "connected": self.connected,
                    "frames": self.parser.frames, "bad": self.parser.bad})
        return out


def main():
    """현장 확인용: python3 -m saver.radar /dev/ttyTHS1 [--floor 500]
    브리지가 같은 포트를 읽고 있으면 먼저 saver-stop 한다(한 포트를 둘이 읽으면 바이트를 나눠 가진다)."""
    import argparse
    ap = argparse.ArgumentParser(description="MR60BHA2 레이더 값 보기 (1초마다)")
    ap.add_argument("port")
    ap.add_argument("--baud", type=int, default=BAUD)
    ap.add_argument("--floor", type=float, default=None, help="radar_floor_mm. 주면 EAPD 도 보인다")
    a = ap.parse_args()
    link = RadarLink(a.port, a.baud).start()
    print("레이더 %s %d bps. Ctrl+C 로 끝" % (a.port, a.baud))
    try:
        while True:
            time.sleep(1.0)
            s = link.snapshot(a.floor)
            print("%s 수신 %-3s 프레임 %d 버림 %d | 호흡 %s 심박 %s 거리 %s mm EAPD %s mm 사람 %s" % (
                time.strftime("%H:%M:%S"), "예" if s["receiving"] else "아니오", s["frames"], s["bad"],
                s.get("rr_bpm", "—"), s.get("hr_bpm", "—"), s.get("distance_mm", "—"),
                s.get("eapd_mm", "—"), {True: "있음", False: "없음"}.get(s.get("presence"), "—")))
    except KeyboardInterrupt:
        pass
    finally:
        link.stop()


if __name__ == "__main__":
    main()
