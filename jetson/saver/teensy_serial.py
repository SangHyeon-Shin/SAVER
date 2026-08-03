#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""teensy_serial.py — Jetson ↔ Teensy 4.1 USB 링크

담당하는 것 (명세 §9)
  - 포트 탐색·연결·자동 재연결 (지수 백오프, 상한 있음)
  - JSON Lines 송수신, 깨진 줄은 버리고 계속
  - 명령 seq 부여, ACK 대기 200 ms, 재전송 2회, 그 후 통신 오류
  - 텔레메트리 stale 판정 (500 ms)
  - 하트비트 PING 1 s

담당하지 않는 것
  - 상태머신 판단 (state_machine.py)
  - 실시간 제어 (Teensy)
"""
import threading
import time

from . import protocol as P

try:
    import serial                       # pyserial
    from serial.tools import list_ports
    HAVE_PYSERIAL = True
except ImportError:                     # 없으면 raw 파일 IO 로 대체 (pty 시험용)
    HAVE_PYSERIAL = False

TEENSY_VID_PID = (0x16C0, 0x0483)


def find_port():
    """VID/PID 로 Teensy 탐색. 못 찾으면 None."""
    if not HAVE_PYSERIAL:
        return None
    for p in list_ports.comports():
        if (p.vid, p.pid) == TEENSY_VID_PID:
            return p.device
    for p in list_ports.comports():
        if "ACM" in (p.device or ""):
            return p.device
    return None


class _RawPort(object):
    """pyserial 이 없을 때 pty·시리얼 경로를 직접 다루는 최소 구현."""

    def __init__(self, path):
        import os
        self._os = os
        self.fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)

    def read(self, n):
        try:
            return self._os.read(self.fd, n)
        except (OSError, BlockingIOError):
            return b""

    def write(self, b):
        """O_NONBLOCK 에서는 부분 쓰기가 일어난다. 다 나갈 때까지 반복해야
        줄이 잘려 상대가 파싱 오류를 내지 않는다."""
        sent = 0
        while sent < len(b):
            try:
                sent += self._os.write(self.fd, b[sent:])
            except BlockingIOError:
                import time as _t
                _t.sleep(0.001)
        return sent

    def close(self):
        try:
            self._os.close(self.fd)
        except OSError:
            pass


class TeensyLink(object):

    def __init__(self, port=None, baud=115200,
                 on_telemetry=None, on_event=None, on_error=None, on_link=None,
                 logger=None):
        self.port_hint = port
        self.baud = baud
        self.on_telemetry = on_telemetry or (lambda m: None)
        self.on_event = on_event or (lambda m: None)
        self.on_error = on_error or (lambda m: None)
        self.on_link = on_link or (lambda connected, port: None)
        self.log = logger

        self._ser = None
        self._buf = b""
        self._seq = P.SeqCounter()
        self._pending = {}              # seq -> [msg, sent_at, tries]
        self._lock = threading.Lock()
        self._stop = threading.Event()

        self.connected = False
        self.mcu_state = None
        self.last_tel_at = 0.0
        self.last_tel = {}
        self.comm_fault = False

    # ── 연결 ────────────────────────────────────────────────
    def _open(self):
        path = self.port_hint or find_port()
        if not path:
            return False
        try:
            if HAVE_PYSERIAL:
                self._ser = serial.Serial(path, self.baud, timeout=0)
            else:
                self._ser = _RawPort(path)
        except Exception:
            self._ser = None
            return False
        self._buf = b""
        self.connected = True
        self.comm_fault = False
        self.on_link(True, path)
        self.send("GET_STATUS")         # 재연결 시 상태 재동기화 (명세 §9)
        return True

    def _close(self):
        if self._ser:
            try:
                self._ser.close()
            except Exception:
                pass
        self._ser = None
        if self.connected:
            self.connected = False
            self.on_link(False, None)

    # ── 송신 ────────────────────────────────────────────────
    def send(self, cmd, **kw):
        """명령 전송. seq 를 돌려주며 ACK 는 백그라운드에서 처리한다."""
        seq = self._seq.next()
        msg = P.build_cmd(seq, cmd, **kw)
        with self._lock:
            self._pending[seq] = [msg, time.time(), 0]
        self._write(msg)
        if self.log:
            self.log.event("cmd", cmd, seq=seq)
        return seq

    def _write(self, msg):
        if not self._ser:
            return
        try:
            self._ser.write(P.encode(msg))
        except Exception:
            self._close()

    # ── 수신 ────────────────────────────────────────────────
    def _handle(self, msg):
        t = msg.get("t")

        if t == "ack":
            seq = msg.get("seq")
            with self._lock:
                self._pending.pop(seq, None)
            if not msg.get("ok", False):
                self.on_error({"code": msg.get("err", ""), "sev": P.severity(msg.get("err")),
                               "why": msg.get("why", ""), "seq": seq})
            if msg.get("state"):
                self.mcu_state = msg["state"]

        elif t == "tel":
            self.last_tel = msg
            self.last_tel_at = time.time()
            self.mcu_state = msg.get("state", self.mcu_state)
            self.on_telemetry(msg)

        elif t == "evt":
            self.on_event(msg)

        elif t == "err":
            self.on_error(msg)

    def _read_once(self):
        try:
            data = self._ser.read(512)
        except Exception:
            self._close()
            return
        if not data:
            return
        self._buf += data
        while b"\n" in self._buf:
            raw, self._buf = self._buf.split(b"\n", 1)
            msg = P.decode(raw)
            if msg is None:
                continue                # 깨진 줄은 버리고 계속 (명세 §3)
            self._handle(msg)
        if len(self._buf) > P.MAX_LINE:
            self._buf = b""             # 줄바꿈 없이 넘치면 버린다

    # ── ACK 감시 / 하트비트 ─────────────────────────────────
    def _tick(self):
        now = time.time()
        expired = []
        with self._lock:
            for seq, ent in list(self._pending.items()):
                if now - ent[1] < P.ACK_TIMEOUT_S:
                    continue
                if ent[2] < P.ACK_RETRY:
                    ent[2] += 1
                    ent[1] = now
                    expired.append(("retry", ent[0]))
                else:
                    del self._pending[seq]
                    expired.append(("fail", ent[0]))
        for kind, msg in expired:
            if kind == "retry":
                self._write(msg)
            else:
                self.comm_fault = True
                self.on_error({"code": "2-CM-002", "sev": 2,
                               "why": "ACK_TIMEOUT:" + str(msg.get("cmd"))})

    def is_stale(self):
        """텔레메트리가 오래됐으면 표시값으로 쓰지 않는다 (명세 §9)."""
        return (time.time() - self.last_tel_at) > P.STALE_S

    # ── 루프 ────────────────────────────────────────────────
    def _loop(self):
        backoff = 0.5
        last_ping = 0.0
        while not self._stop.is_set():
            if not self._ser:
                if self._open():
                    backoff = 0.5
                else:
                    self._stop.wait(backoff)
                    backoff = min(backoff * 1.6, 5.0)
                    continue
            self._read_once()
            now = time.time()
            if now - last_ping >= P.HEARTBEAT_S:
                last_ping = now
                self.send("PING")
            self._tick()
            self._stop.wait(0.005)
        self._close()

    def start(self):
        self._thread = threading.Thread(target=self._loop)
        self._thread.daemon = True
        self._thread.start()
        return self

    def stop(self):
        self._stop.set()
