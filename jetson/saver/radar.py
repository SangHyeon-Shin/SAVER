# -*- coding: utf-8 -*-
"""radar.py — MR60BHA2(Seeed 60 GHz 레이더) 판독: 호흡수, 심박수, 거리, 가슴 두께(EAPD)

연결
  XIAO ESP32C6 키트(USB): 키트 기본 펌웨어(ESPHome)는 레이더 원시 프레임을 USB 로 보내지 않는다.
    jetson/radar_fw/mr60bha2_passthrough 를 올리면 레이더 UART 를 USB 로 그대로 전달한다 → /dev/saver-radar(/dev/ttyACM*)
  레이더 모듈 단독: 레이더 UART(115200 8N1, 3.3 V) → 젯슨 40핀 UART /dev/ttyTHS1(8번 TX, 10번 RX)
  브리지 --radar PORT 로 켠다. 주지 않으면 레이더 기능을 쓰지 않는다.
  레이더는 켜지면 스스로 프레임을 보낸다. 젯슨은 읽기만 하고 명령을 보내지 않는다.
  현장 확인: python3 -m saver.radar PORT (포트 상태, 받은 바이트, 프레임 수와 값을 1초마다. 원인 안내 포함)

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
import errno
import glob
import math
import os
import re
import struct
import sys
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
SAMPLE_MAX = 256        # 진단 표시용으로 남기는 연결 직후 바이트 수
SETTLE_S = 1.0          # 포트를 연 직후 이 시간에 받은 바이트는 해석하지 않는다. 키트(XIAO)가 앞 연결이 끝난 뒤 쌓아 둔
                        # 묵은 자료(최대 수 KB)가 한꺼번에 와서 지난 값과 번호 빠짐으로 보이기 때문이다(10/15 실기)

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
        self.lost = 0           # 프레임 번호(ID)가 건너뛴 수. 전송 중 잃은 프레임 추정(MR60BHA2 는 프레임마다 ID 를 1씩 올린다, 10/15 실기)
        self._last_id = None

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
            if self._last_id is not None:
                step = (fid - self._last_id) & 0xFFFF
                if 1 < step < 1000:             # 0 이나 큰 되돌아감(레이더 재시작)은 손실로 세지 않는다
                    self.lost += step - 1
            self._last_id = fid
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


def open_port(port, baud=BAUD):
    """직렬 포트를 비차단으로 연다(pyserial, 없으면 raw 파일 IO)."""
    if HAVE_PYSERIAL:
        return serial.Serial(port, baud, timeout=0)
    from .teensy_serial import _RawPort
    return _RawPort(port)


def _open_error(e):
    """포트 열기 실패를 짧은 글로(진단 표시용)."""
    no = getattr(e, "errno", None)
    if no == errno.EACCES:
        return "권한 없음"
    if no == errno.EBUSY:
        return "다른 프로그램이 사용 중"
    return (str(e) or e.__class__.__name__)[:80]


class RadarLink(object):
    """레이더 직렬 포트 판독 스레드. 포트가 없거나 끊기면 다시 연다."""

    def __init__(self, port, baud=BAUD, on_link=None, clock=time.monotonic):
        self.port = port
        self.baud = baud
        self.on_link = on_link or (lambda connected, port: None)
        self.state = RadarState(clock=clock)
        self.parser = FrameParser()
        self.connected = False
        self.error = None               # 마지막 열기 실패 이유("없음", "권한 없음" 등). 열리면 None
        self.rx_bytes = 0               # 이번 연결에서 받은 바이트
        self.sample = bytearray()       # 이번 연결에서 처음 받은 바이트(SAMPLE_MAX, 진단 표시용)
        self.settle_s = SETTLE_S
        self._t_open = 0.0
        self._ser = None
        self._stop = threading.Event()
        self._thread = None

    def _open(self):
        if not self.port or not os.path.exists(self.port):
            self.error = "없음"
            return False
        try:
            self._ser = open_port(self.port, self.baud)
        except Exception as e:
            self._ser = None
            self.error = _open_error(e)
            return False
        self.parser = FrameParser()
        self.error = None
        self.rx_bytes = 0
        self.sample = bytearray()
        self._t_open = time.monotonic()
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
        self.rx_bytes += len(data)
        if len(self.sample) < SAMPLE_MAX:
            self.sample.extend(data[:SAMPLE_MAX - len(self.sample)])
        if time.monotonic() - self._t_open < self.settle_s:
            return                          # 묵은 자료는 해석하지 않는다(SETTLE_S)
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
                    "frames": self.parser.frames, "bad": self.parser.bad, "lost": self.parser.lost,
                    "rx_bytes": self.rx_bytes, "skipped": self.parser.skipped})
        if self.error:
            out["error"] = self.error
        return out


def list_ports(root="/"):
    """[(경로, 설명)] 지금 있는 직렬 포트. by-id 이름과 /dev/saver-* 별칭을 설명으로 붙인다."""
    dev = os.path.join(root, "dev")
    alias = {}
    for p in sorted(glob.glob(os.path.join(dev, "serial", "by-id", "*"))) + sorted(glob.glob(os.path.join(dev, "saver-*"))):
        alias.setdefault(os.path.realpath(p), []).append(os.path.basename(p))
    out = []
    for pat in ("ttyACM*", "ttyUSB*", "ttyTHS*"):
        for p in sorted(glob.glob(os.path.join(dev, pat))):
            names = alias.get(os.path.realpath(p), [])
            if os.path.basename(p) == "ttyTHS1":
                names = names + ["젯슨 40핀 UART 8, 10번"]
            out.append(("/dev/" + os.path.basename(p), ", ".join(names)))
    return out


def port_identity(port, root="/"):
    """(실제 장치 경로, [by-id 이름]). by-id 이름에 USB 제조사가 들어 있어 Teensy 와 XIAO 를 가린다."""
    real = os.path.realpath(port)
    names = [os.path.basename(p) for p in sorted(glob.glob(os.path.join(root, "dev", "serial", "by-id", "*")))
             if os.path.realpath(p) == real]
    return real, names


def classify(b):
    """레이더 프레임이 아닌 자료의 종류 (종류, 안내 한 줄). 앞부분 몇백 바이트로 판단한다."""
    b = bytes(bytearray(b))
    t = b.decode("latin-1")
    if re.search(r"\[[DIWECV]\]\[[\w.]+(:\d+)?\]", t):
        return "esphome", ("ESPHome 기록 글자입니다. XIAO 에 키트 기본 펌웨어(ESPHome)가 그대로 있다. 전달 펌웨어"
                           "(jetson/radar_fw/mr60bha2_passthrough)를 올려야 레이더 프레임이 나온다")
    if '"v":1' in t and '"t":"' in t:
        return "teensy", ("SAVER Teensy 의 JSON 줄입니다. 레이더가 아니라 Teensy 포트를 열었다. 레이더 키트는 "
                          "/dev/saver-radar(by-id 이름에 Espressif), Teensy 는 /dev/saver-teensy")
    if re.search(r"(breath_rate|heart_rate|total_phase|distance)\s*:", t):
        return "seeed_example", ("Seeed 예제 펌웨어가 값을 글자로 내보내고 있다. 전달 펌웨어"
                                 "(jetson/radar_fw/mr60bha2_passthrough)로 바꿔야 한다")
    if "ESP-ROM" in t or re.search(r"rst:0x[0-9a-f]+", t) or "boot:" in t:
        return "boot", "XIAO(ESP32) 부팅 기록입니다. 몇 초 기다려도 같으면 펌웨어가 다시 켜지기를 되풀이하는지 확인"
    if looks_text(b) and re.search(r"^\s*-?\d+(\.\d+)?(\s*,\s*[^,\r\n]+){2,}\s*$", t, re.M):
        return "csv", ("값을 쉼표로 나눈 글자 줄입니다. XIAO 에 다른 펌웨어(아두이노 스케치 등)가 올라가 값을 직접 내보내고 있다. "
                       "전달 펌웨어(jetson/radar_fw/mr60bha2_passthrough)로 바꿔야 레이더 프레임이 나온다. 지금 펌웨어가 "
                       "필요하면 먼저 백업(jetson/README.md)")
    if looks_text(b):
        return "text", ("글자 자료입니다. 레이더 프레임이 아니다. XIAO 펌웨어(기본 ESPHome 등)나 다른 장치의 출력으로 "
                        "보인다. 전달 펌웨어를 올렸는지, 레이더 포트가 맞는지 확인")
    if b and b"\x01" not in b:
        return "binary_nosof", ("이진 자료지만 레이더 프레임 시작 바이트(0x01)가 없다. 통신 속도(--baud, 기본 115200)나 "
                                "다른 장치인지 확인")
    return "binary", "통신 속도(--baud, 기본 115200)나 다른 장치인지 확인"


def looks_text(b):
    """글자(ASCII, 줄바꿈, 색 코드)가 85 % 이상이면 참. 레이더 프레임은 이진 자료다."""
    b = bytearray(b)
    if not b:
        return False
    ok = sum(1 for c in b if 32 <= c < 127 or c in (9, 10, 13, 27))
    return ok >= 0.85 * len(b)


def preview(b, n=48):
    """(글자로 보기, 앞 16 바이트 16진수)."""
    b = bytearray(b)
    txt = "".join(chr(c) if 32 <= c < 127 else "." for c in b[:n])
    return txt, " ".join("%02x" % c for c in b[:16])


def diagnose(s, sample, elapsed, port, since_open=None):
    """CLI 안내: [(키, 줄 목록)]. 같은 키는 한 번만 보인다. s 는 RadarLink.snapshot().
    elapsed 는 시작부터, since_open 은 포트가 열린 뒤부터 지난 초(없으면 elapsed)."""
    out = []
    opened = elapsed if since_open is None else since_open
    err = s.get("error")
    if not s.get("connected") and err == "없음" and elapsed >= 2:
        lines = ["  ! 포트 %s 가 없습니다. 지금 있는 직렬 포트:" % port]
        lines += ["      %s%s" % (p, ("  (%s)" % d) if d else "") for p, d in list_ports()] or ["      (없음)"]
        lines.append("    XIAO ESP32C6 레이더 키트를 USB 로 꽂으면 /dev/ttyACM* 이고 이름에 Espressif 가 들어간다"
                     "(설치 스크립트의 udev 규칙이 /dev/saver-radar 를 붙인다)")
        out.append(("missing", lines))
    elif not s.get("connected") and err and err != "없음":
        lines = ["  ! 포트를 열지 못했습니다: %s" % err]
        if err == "권한 없음":
            lines.append("    dialout 그룹인지 groups 로 확인. 없으면 sudo usermod -aG dialout $USER 뒤 다시 로그인")
        elif err == "다른 프로그램이 사용 중":
            lines.append("    브리지(saver-stop)나 picocom 같은 다른 프로그램을 끈다")
        out.append(("open", lines))
    if s.get("connected") and s.get("rx_bytes", 0) == 0 and opened >= 5:
        out.append(("silent", ["  ! 포트는 열렸지만 5초 동안 아무것도 들어오지 않습니다.",
                               "    레이더 전원, 배선(TX↔RX 교차, GND 공통), 포트가 맞는지 확인. XIAO 키트는 전달 펌웨어가"
                               " 아니면 레이더 자료를 USB 로 보내지 않을 수 있다(jetson/README.md)"]))
    if s.get("connected") and s.get("rx_bytes", 0) >= 64 and s.get("frames", 0) == 0 and opened >= 3:
        txt, hx = preview(sample, 72)
        kind, msg = classify(sample)
        lines = ["  ! %d 바이트를 받았지만 레이더 프레임이 없습니다(종류: %s). 앞부분: '%s'" % (s["rx_bytes"], kind, txt),
                 "    16진수: %s" % hx, "    " + msg]
        out.append(("noframe", lines))
    return out


def main(argv=None):
    """현장 확인용: python3 -m saver.radar /dev/saver-radar [--floor 500]
    브리지가 같은 포트를 읽고 있으면 먼저 saver-stop 한다(한 포트를 둘이 읽으면 바이트를 나눠 가진다)."""
    import argparse
    ap = argparse.ArgumentParser(description="MR60BHA2 레이더 값 보기 (1초마다)")
    ap.add_argument("port")
    ap.add_argument("--baud", type=int, default=BAUD)
    ap.add_argument("--floor", type=float, default=None, help="radar_floor_mm. 주면 EAPD 도 보인다")
    ap.add_argument("--seconds", type=float, default=0, help="이 시간 뒤 끝냄(0 이면 Ctrl+C 까지)")
    ap.add_argument("--dump", action="store_true", help="받은 바이트를 16진수와 글자로 그대로 보이기(기본 5초)")
    a = ap.parse_args(argv)
    if a.dump:
        return dump(a.port, a.baud, a.seconds or 5.0)
    link = RadarLink(a.port, a.baud).start()
    print("레이더 %s %d bps. Ctrl+C 로 끝" % (a.port, a.baud))
    t0, shown, t_open = time.monotonic(), set(), None
    try:
        while not a.seconds or time.monotonic() - t0 < a.seconds:
            time.sleep(1.0)
            s = link.snapshot(a.floor)
            err = s.get("error")
            state = "열림" if s["connected"] else ("없음" if err == "없음" else ("열기 실패" if err else "여는 중"))
            print("%s 포트 %s 받음 %d B 프레임 %d 깨짐 %d 빠짐 %d | 호흡 %s 심박 %s 거리 %s mm EAPD %s mm 사람 %s" % (
                time.strftime("%H:%M:%S"), state, s.get("rx_bytes", 0), s["frames"], s["bad"], s.get("lost", 0),
                s.get("rr_bpm", "—"), s.get("hr_bpm", "—"), s.get("distance_mm", "—"),
                s.get("eapd_mm", "—"), {True: "있음", False: "없음"}.get(s.get("presence"), "—")))
            now = time.monotonic()
            if s["connected"] and t_open is None:
                t_open = now
                shown.difference_update(("silent", "noframe"))      # 다시 연결되면 안내를 다시 보인다
                real, names = port_identity(a.port)
                print("  포트 정보: %s%s%s" % (a.port, (" → " + real) if real != a.port else "",
                                              ("  (" + ", ".join(names) + ")") if names else ""))
            elif not s["connected"]:
                if t_open is not None:
                    shown.difference_update(("missing", "open"))    # 끊기면 지금 있는 포트 목록을 다시 보인다
                t_open = None
            for key, lines in diagnose(s, bytes(link.sample), now - t0, a.port,
                                       now - t_open if t_open is not None else 0.0):
                if key not in shown:
                    shown.add(key)
                    print("\n".join(lines))
            sys.stdout.flush()
    except KeyboardInterrupt:
        pass
    finally:
        link.stop()


def dump(port, baud=BAUD, seconds=5.0):
    """받은 바이트를 16진수와 글자로 보인다(진단용). 끝에 0x01 개수와 종류 판별."""
    if not os.path.exists(port):
        print("포트가 없습니다: %s" % port)
        for p, d in list_ports():
            print("  %s%s" % (p, ("  (%s)" % d) if d else ""))
        return 1
    real, names = port_identity(port)
    print("포트 %s%s%s, %d bps, %.0f초" % (port, (" → " + real) if real != port else "",
                                       ("  (" + ", ".join(names) + ")") if names else "", baud, seconds))
    try:
        ser = open_port(port, baud)
    except Exception as e:
        print("포트를 열지 못했습니다: %s" % _open_error(e))
        return 1
    # 받는 동안에는 출력하지 않는다. 줄마다 화면에 쓰면 읽기가 밀려 키트 쪽에서 자료를 버린다
    # (10/15 실기: 받으며 출력한 10초 덤프에서 프레임 번호 775개가 빠짐)
    print("받는 중(출력은 끝난 뒤)...")
    sys.stdout.flush()
    t0 = time.monotonic()
    got, end, settle_off = bytearray(), t0 + seconds, None
    try:
        while time.monotonic() < end:
            data = ser.read(4096)
            if settle_off is None and time.monotonic() - t0 >= SETTLE_S:
                settle_off = len(got)
            if data:
                got.extend(data)
            else:
                time.sleep(0.005)
    except KeyboardInterrupt:
        pass
    finally:
        try:
            ser.close()
        except Exception:
            pass
    for off in range(0, len(got), 16):
        _dump_line(off, got[off:off + 16])
    parser = FrameParser()
    frames = parser.feed(bytes(got))
    kind, msg = classify(got[:SAMPLE_MAX]) if not frames else ("radar", "레이더 프레임이 있다")
    print("합계 %d 바이트, 0x01 %d개, 레이더 프레임 %d개. 종류: %s. %s" % (len(got), got.count(1), len(frames), kind, msg))
    if frames:
        cnt = collections.Counter(t for t, _d, _i in frames)
        print("프레임 종류: %s" % ", ".join("%04X %d" % (t, n) for t, n in sorted(cnt.items())))
        live = FrameParser()
        live.feed(bytes(got[settle_off:]) if settle_off is not None else b"")
        print("처음 %.0f초 뒤: 레이더 프레임 %d개, 번호 빠짐 %d개, 깨짐 %d개(빠짐은 전송 중 잃은 프레임 추정. 처음 %.0f초에는"
              " 키트에 쌓였던 묵은 자료가 온다)" % (SETTLE_S, live.frames, live.lost, live.bad, SETTLE_S))
    return 0


def _dump_line(off, chunk):
    chunk = bytearray(chunk)
    print("%06x  %-48s %s" % (off, " ".join("%02x" % c for c in chunk),
                              "".join(chr(c) if 32 <= c < 127 else "." for c in chunk)))
    sys.stdout.flush()


if __name__ == "__main__":
    sys.exit(main())
