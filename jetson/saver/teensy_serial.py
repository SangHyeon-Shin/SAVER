#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""teensy_serial.py — Jetson ↔ Teensy 4.1 USB 링크

담당하는 것 (명세 §9)
  - 포트 탐색·연결·자동 재연결 (지수 백오프, 상한 있음)
  - JSON Lines 송수신, 깨진 줄은 버리고 계속
  - 명령 seq 부여, ACK 대기 200 ms, 재전송 2회, 그 후 통신 오류
  - 텔레메트리 stale 판정 (500 ms)
  - 하트비트 PING 1 s
  - v1.1 초안: 연결마다 GET_INFO 먼저, 비조회 명령에 bid·aid, mot 수신과 tel 짝 맞춤,
    재부팅(boot_id 변화) 감지, tn 역전 폐기, 장치 플래그(mcu_flags)
  - 완료 이벤트 유실 때 같은 seq·aid 재전송(resend, 초안 §4.4)
  - 경과 시간은 단조 시계(time.monotonic)

담당하지 않는 것
  - 상태머신 판단 (state_machine.py)
  - 실시간 제어 (Teensy)
"""
import collections
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

    DEFER_MAX_S = 2.0       # GET_INFO 응답 전 보류한 명령의 유효 시간. 넘으면 보내지 않는다
    SENT_KEEP = 64

    def __init__(self, port=None, baud=115200,
                 on_telemetry=None, on_event=None, on_error=None, on_link=None,
                 logger=None, seq_start=None, on_mot=None, on_ack=None):
        self.port_hint = port
        self.baud = baud
        self.on_telemetry = on_telemetry or (lambda m: None)
        self.on_event = on_event or (lambda m: None)
        self.on_error = on_error or (lambda m: None)
        self.on_link = on_link or (lambda connected, port: None)
        self.on_mot = on_mot or (lambda m: None)
        self.on_ack = on_ack or (lambda m: None)
        self.log = logger

        self._ser = None
        self._buf = b""
        # 시작 seq 는 무작위: Jetson 만 재시작될 때 직전 세션 seq 와 겹치지 않게 (protocol.initial_seq)
        self._seq = P.SeqCounter(P.initial_seq() if seq_start is None else seq_start)
        self._pending = {}              # seq -> [msg, sent_at, tries]
        self._lock = threading.Lock()
        self._stop = threading.Event()

        self.connected = False
        self.mcu_state = None
        self.last_tel_at = 0.0
        self.last_tel = {}
        self.comm_fault = False

        # v1.1 초안
        self.proto = None               # None=확인 전, "1.0", "1.1"
        self.info = {}
        self.caps = set()
        self.boot_id = None
        self._aid = None
        self._info_seq = None
        self._info_at = 0.0
        self._deferred = []             # [(cmd, kw, at)]
        self.last_mot = {}
        self.last_mot_at = 0.0
        self._tn_last = {}              # "tel"/"mot" -> (boot_id, tn)
        self._pair = None               # (tel, mot, 수신 시각): 같은 tn 의 짝
        self._sent = collections.OrderedDict()   # seq -> 보낸 비조회 명령(재전송용, 최근 SENT_KEEP 개)

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
        self._handshake()               # v1.1: 다른 명령보다 먼저 GET_INFO (§2.4)
        self.send("GET_STATUS")         # 재연결 시 상태 재동기화 (명세 §9)
        return True

    def _handshake(self):
        self.proto = None
        self._info_at = time.monotonic()
        self._info_seq = self.send("GET_INFO")

    def _set_proto(self, proto, info=None):
        self.proto = proto
        if info is not None:
            self.info = info
            self.caps = set(info.get("caps") or [])
            self.boot_id = info.get("boot_id")
            if "last_seq" in info:      # seq 는 last_seq + 1 부터 이어 간다(§2.5)
                last = int(info["last_seq"])
                self._seq = P.SeqCounter(1 if last >= 65535 else last + 1)
            self._aid = P.AidCounter(int(info.get("last_aid", 0)) + 1)
        if self.log:
            self.log.event("sys", "PROTO_" + proto)
            if info is not None and str(info.get("drv_enc_mode")) == "1":
                self.log.event("sys", "DRV_ENC_MODE_1:" + str(info.get("drv_enc_side")))
        now = time.monotonic()
        pend, self._deferred = self._deferred, []
        for cmd, kw, at in pend:
            if now - at <= self.DEFER_MAX_S:
                self.send(cmd, **kw)
            else:
                self.on_error({"code": "2-CM-002", "sev": 2, "why": "NOT_SENT:" + cmd})

    def uses_aid(self):
        return self.proto == "1.1" and "AID" in self.caps

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
        """명령 전송. seq 를 돌려주며 ACK 는 백그라운드에서 처리한다.

        펌웨어 판(GET_INFO)을 확인하기 전에는 조회·정지 명령만 바로 보내고 나머지는 보류한다
        (bid·aid 없이 보내면 v1.1 펌웨어가 AID_REQUIRED 로 거부한다). 보류 중이면 None."""
        if self.proto is None and cmd not in P.QUERY_CMDS and cmd not in P.STOP_CMDS:
            self._deferred.append((cmd, kw, time.monotonic()))
            return None
        if self.uses_aid() and cmd not in P.QUERY_CMDS:
            aid = self._aid.next()
            if aid is None:             # aid 소진: 재시작 안내(§2.5-7). 정지 방향은 aid 없이도 실행된다
                if cmd not in P.STOP_CMDS:
                    self.on_error({"code": "2-CM-002", "sev": 2, "why": "AID_EXHAUSTED:" + cmd})
                    return None
            else:
                kw = dict(kw, bid=self.boot_id, aid=aid)
        seq = self._seq.next()
        msg = P.build_cmd(seq, cmd, **kw)
        with self._lock:
            self._pending[seq] = [msg, time.monotonic(), 0]
            if cmd not in P.QUERY_CMDS:
                self._sent[seq] = msg
                while len(self._sent) > self.SENT_KEEP:
                    self._sent.popitem(last=False)
        self._write(msg)
        if self.log:
            self.log.event("cmd", cmd, seq=seq)
        return seq

    def sent(self, seq):
        """보낸 비조회 명령(사본). 없으면 None."""
        with self._lock:
            m = self._sent.get(seq)
        return dict(m) if m else None

    def resend(self, seq):
        """같은 seq·aid·파라미터로 다시 보낸다(초안 §4.4-3). Teensy 는 재실행하지 않고
        원래 ACK 와 저장된 완료 이벤트를 다시 보낸다. 재부팅 전 명령은 남아 있지 않다."""
        with self._lock:
            msg = self._sent.get(seq)
            if msg is None:
                return False
            self._pending[seq] = [msg, time.monotonic(), 0]
        self._write(msg)
        if self.log:
            self.log.event("cmd", "RESEND:" + str(msg.get("cmd")), seq=seq)
        return True

    def status_pair(self):
        """같은 tn 의 (tel, mot, 수신 시각). 없으면 None."""
        return self._pair

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

        if "boot_id" in msg and self.boot_id is not None and msg["boot_id"] != self.boot_id:
            self._rebooted(msg["boot_id"])

        if t == "ack":
            seq = msg.get("seq")
            with self._lock:
                self._pending.pop(seq, None)
            if seq is not None and seq == self._info_seq and self.proto is None:
                if not msg.get("ok", False):
                    # v1.0 펌웨어는 GET_INFO 를 모른다 → v1.0 규칙(§2.4). 오류로 보고하지 않는다
                    self._set_proto("1.0")
                    return
            elif not msg.get("ok", False):
                self.on_error({"code": msg.get("err", ""), "sev": P.severity(msg.get("err")),
                               "why": msg.get("why", ""), "seq": seq})
            if msg.get("state"):
                self.mcu_state = msg["state"]
            self.on_ack(msg)

        elif t == "tel":
            if not self._in_order("tel", msg):
                return
            self.last_tel = msg
            self.last_tel_at = time.monotonic()
            self.mcu_state = msg.get("state", self.mcu_state)
            self.on_telemetry(msg)

        elif t == "mot":
            if not self._in_order("mot", msg):
                return
            self.last_mot = msg
            self.last_mot_at = time.monotonic()
            if self.last_tel.get("tn") is not None and self.last_tel.get("tn") == msg.get("tn"):
                self._pair = (self.last_tel, msg, self.last_mot_at)
            self.on_mot(msg)

        elif t == "evt":
            if msg.get("evt") == "INFO" and self.proto is None:
                self._set_proto("1.1" if "AID" in (msg.get("caps") or []) else "1.0", msg)
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
        now = time.monotonic()
        # INFO 응답이 유실되면 GET_INFO 를 다시 보낸다(조회라 같은 seq 가 아니어도 안전)
        if self._ser and self.proto is None and now - self._info_at > 1.0:
            self._handshake()
        stale_def = [d for d in self._deferred if now - d[2] > self.DEFER_MAX_S]
        if stale_def:
            self._deferred = [d for d in self._deferred if now - d[2] <= self.DEFER_MAX_S]
            for cmd, kw, at in stale_def:
                self.on_error({"code": "2-CM-002", "sev": 2, "why": "NOT_SENT:" + cmd})
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

    def _in_order(self, kind, msg):
        """같은 boot_id 에서 tn 이 줄어든 메시지는 순서가 뒤바뀐 것으로 보고 버린다(§3.3-5)."""
        tn, boot = msg.get("tn"), msg.get("boot_id")
        if tn is None:
            return True
        last = self._tn_last.get(kind)
        if last and last[0] == boot and tn < last[1]:
            return False
        self._tn_last[kind] = (boot, tn)
        return True

    def _rebooted(self, new_boot):
        """Teensy 재부팅: 이전 값과 진행 중 요청을 버리고 GET_INFO 부터 다시(§2.5 재부팅, §3.3-3)."""
        if self.log:
            self.log.event("sys", "MCU_REBOOT", seq=new_boot)
        self.boot_id = new_boot
        self.last_tel, self.last_mot, self._pair = {}, {}, None
        self._tn_last = {}
        with self._lock:
            self._pending.clear()       # 재부팅 전에 만든 요청은 재전송하지 않는다
            self._sent.clear()
        self._handshake()

    @property
    def mcu_flags(self):
        """상태머신 redirect 용 장치 플래그. 펌웨어가 mot 을 보내지 않으면 None.
        짝(같은 tn 의 tel·mot)이 없거나 오래됐으면 값을 None(불명)으로 둔다(§3.3-8)."""
        if self.proto != "1.1" or "MOT" not in self.caps:
            return None
        pair = self._pair
        if not pair or time.monotonic() - pair[2] > P.STALE_S:
            return {"rearm_lock": None, "vac_released": None}
        mot = pair[1]
        return {"rearm_lock": mot.get("rearm_lock"), "vac_released": mot.get("vac_released")}

    @property
    def drv_enc_mode(self):
        """주행 엔코더 모드(초안 8차 §4.7): 1 = 단일 엔코더 + 자이로, 2 = 좌우.
        v1.1 이 아니면 None, INFO 에 없으면 2(기존 조건)."""
        if self.proto != "1.1":
            return None
        try:
            return int(self.info.get("drv_enc_mode") or 2)
        except (TypeError, ValueError):
            return 2

    def mot_fresh(self):
        """mot 이 오래되면 새 동작 명령을 보내지 않는다(§3.3-2)."""
        return (time.monotonic() - self.last_mot_at) <= P.STALE_S

    def is_stale(self):
        """텔레메트리가 오래됐으면 표시값으로 쓰지 않는다 (명세 §9)."""
        return (time.monotonic() - self.last_tel_at) > P.STALE_S

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
            now = time.monotonic()
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
