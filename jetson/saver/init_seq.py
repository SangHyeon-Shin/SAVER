#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""init_seq.py — S1 초기화 절차 (상태머신 v2.4, 한건민 합의 10/5, DECISIONS D-46)

S1 의 명령을 한꺼번에 보내지 않고, ACK 와 장치 상태를 보며 차례로 보낸다.

  1. PING            → ACK ok
  2. GET_STATUS      → ACK ok, 그 뒤 텔레메트리에서 비상정지 아님, 활성 고장 없음
  3. 장치 상태별
       IDLE          ARM → ACK ok → HOME
       ARMED         HOME (ARM 을 다시 보내면 NOT_IDLE 로 거부되므로 생략)
       HOMED         ARM, HOME 생략. 원점 완료로 인정
       FAULT, ESTOP  INIT_ERROR
       그 밖         INIT_ERROR (UNEXPECTED_MCU_STATE). VACUUM_HOLD, CPR_* 를 자동 DISARM 하지 않는다
  4. HOME ACK ok     → INIT_DONE (X_MOVE)
  5. 원점 완료(HOMING_DONE 이나 상태 HOMED)는 뒤에서 기다린다. 그 사이 장치가 FAULT·ESTOP 이나
     ARMED·HOMED 가 아닌 상태가 되거나, HOMING_DONE 결과가 OK 가 아니거나, 시간 제한
     (설정 home_timeout_s)을 넘으면 INIT_ERROR. home_timeout_s 는 10/12 실측 뒤 정한다.
     비어 있으면 시간은 검사하지 않고, 탐색·흡착 진입은 상태머신 requires_mcu(HOMED)가 막는다.

ACK 가 거부(ok=false)되거나 오지 않으면(2-CM-002) 그 자리에서 INIT_ERROR.
INIT_DONE 전에 링크가 끊기면 다시 연결된 뒤 PING 부터 다시 한다.

펌웨어 판(프로토콜 v1.1): 링크가 GET_INFO 로 v1.1 을 확인한 뒤에야 PING 부터 시작한다.
v1.1 이 아니면(v1.0 펌웨어 등) 기다리지 않고 INIT_ERROR(FW_VERSION:<판>)로 끝낸다.

담당하지 않는 것: 전이 규칙(protocol/state_machine_v2.json), 실시간 제어와 인터록(Teensy).
Python 3.6 호환.
"""
import threading
import time

from . import protocol as P

PRE_DONE = ("wait_link", "ping", "status", "status_tel", "arm", "home")
HOMING_OK_STATES = ("ARMED", "HOMED")     # 원점 복귀 중(ARMED)이거나 끝남(HOMED)


def _fw_mismatch(msg):
    return msg.get("code") == "3-CM-001" and str(msg.get("why", "")).startswith("FW_NOT_V11")


class InitRunner(object):

    def __init__(self, link, fire, cfg=None, logger=None, clock=None):
        self.link = link
        self.fire = fire                    # fire(event, payload) -> bool
        self.cfg = cfg if cfg is not None else {}
        self.log = logger
        self.clock = clock or time.monotonic
        self.step = "idle"
        self.reason = None
        self.homed = False
        self._seq = None                    # 지금 기다리는 ACK 의 seq
        self._seqs = set()                  # 이 절차가 보낸 명령의 seq
        self._t = 0.0
        # 리더 스레드(ACK, tel)와 asyncio 틱이 함께 부른다. 명령을 보낸 직후 ACK 가 먼저 처리되어도
        # seq 를 놓치지 않게 잠그고, 상태머신 이벤트는 잠금을 푼 뒤에 낸다(상태머신 잠금과 엇갈리지 않게)
        self._lock = threading.RLock()
        self._out = []

    # ── 조회 ────────────────────────────────────────────────
    @property
    def active(self):
        return self.step not in ("idle", "done", "failed")

    def owns(self, seq):
        """이 절차가 보낸 명령인지. 브리지는 이 명령의 오류를 일반 규칙(ERROR, STOP)으로 처리하지 않는다."""
        return seq is not None and seq in self._seqs

    def handles(self, msg):
        """브리지가 일반 규칙 대신 이 절차에 넘길 오류인지: 절차가 보낸 명령의 오류, 또는 장치 연결을
        기다리는 동안 링크가 알린 펌웨어 판 불일치(3-CM-001 FW_NOT_V11)."""
        if self.owns(msg.get("seq")):
            return True
        return self.step == "wait_link" and _fw_mismatch(msg)

    def snapshot(self):
        return {"step": self.step, "reason": self.reason, "homed": self.homed}

    # ── 시작·중지 ───────────────────────────────────────────
    def start(self):
        with self._lock:
            self._start()
        self._flush()

    def _start(self):
        self.step = "wait_link"
        self.reason = None
        self.homed = False
        self._seq = None
        self._seqs = set()
        self._note("INIT_START")
        self._try_begin()

    def cancel(self, why):
        with self._lock:
            self._cancel(why)

    def _cancel(self, why):
        if self.active:
            self._note("INIT_CANCEL:" + why)
        self.step = "idle"
        self._seq = None

    # ── 장치에서 온 것 ──────────────────────────────────────
    def on_ack(self, msg):
        with self._lock:
            self._on_ack(msg)
        self._flush()

    def _on_ack(self, msg):
        seq = msg.get("seq")
        if not self.active or seq is None or seq != self._seq:
            return
        self._seq = None
        if not msg.get("ok", False):
            self._fail("%s_REJECTED:%s" % (self.step.upper(), msg.get("why") or msg.get("err") or ""))
            return
        if self.step == "ping":
            self._send("GET_STATUS", "status")
        elif self.step == "status":
            self.step = "status_tel"            # GET_STATUS 는 텔레메트리 1회를 바로 보낸다(명세 §4)
            self._t = self.clock()
        elif self.step == "arm":
            self._send("HOME", "home")
        elif self.step == "home":
            self._init_done()

    def on_tel(self, msg):
        with self._lock:
            self._on_tel(msg)
        self._flush()

    def _on_tel(self, msg):
        if self.step == "status_tel":
            self._check_status(msg)
        elif self.step == "homing":
            st = msg.get("state")
            if msg.get("estop") or st == "ESTOP":
                self._fail("HOMING_ESTOP")
            elif st == "HOMED":
                self._homed()
            elif st is not None and st not in HOMING_OK_STATES:
                self._fail("HOMING_LOST:" + str(st))

    def on_event(self, msg):
        with self._lock:
            self._on_event(msg)
        self._flush()

    def _on_event(self, msg):
        if msg.get("evt") != "HOMING_DONE" or self.step not in ("home", "homing"):
            return
        result = msg.get("result", "OK")
        if result != "OK":
            self._fail("HOMING_" + str(result))
        elif self.step == "homing":
            self._homed()
        else:
            self.homed = True                   # HOME ACK 보다 먼저 처리된 경우

    def on_error(self, msg):
        """ACK 가 오지 않은 경우만 여기서 처리한다. 거부(ok=false)는 on_ack 가 처리한다."""
        with self._lock:
            self._on_error(msg)
        self._flush()

    def _on_error(self, msg):
        seq = msg.get("seq")
        if self.active and seq is not None and seq == self._seq \
                and str(msg.get("why", "")).startswith("ACK_TIMEOUT"):
            self._seq = None
            self._fail(str(msg.get("why")))
        elif self.step == "wait_link" and _fw_mismatch(msg):
            self._check_fw()

    def on_link(self, connected):
        with self._lock:
            self._on_link(connected)
        self._flush()

    def _on_link(self, connected):
        if connected:
            self._try_begin()
        elif self.step in PRE_DONE and self.step != "wait_link":
            self._note("INIT_RESTART:LINK_DOWN")
            self.step = "wait_link"
            self._seq = None

    def tick(self):
        with self._lock:
            self._tick()
        self._flush()

    def _tick(self):
        now = self.clock()
        if self.step == "wait_link":
            self._check_fw()
            self._try_begin()
        elif self.step == "status_tel" and now - self._t > P.STALE_S:
            self._fail("NO_STATUS")
        elif self.step == "homing":
            limit = self.cfg.get("home_timeout_s")
            if limit and now - self._t > limit:
                self._fail("HOME_TIMEOUT")

    # ── 내부 ────────────────────────────────────────────────
    def _ready(self):
        return bool(self.link.connected) and self.link.proto == P.PROTO_REV

    def _try_begin(self):
        if self.step == "wait_link" and self._ready():
            self._send("PING", "ping")

    def _check_fw(self):
        """연결됐는데 펌웨어 판이 v1.1 이 아니면 기다리지 않고 끝낸다(명령은 하나도 보내지 않았다)."""
        proto = self.link.proto
        if self.step == "wait_link" and self.link.connected and proto is not None and proto != P.PROTO_REV:
            self._fail("FW_VERSION:" + str(proto))

    def _send(self, cmd, step):
        self.step = step
        self._t = self.clock()
        seq = self.link.send(cmd)
        if seq is None:                         # 보류(펌웨어 판 확인 전)나 aid 소진
            self._fail("NOT_SENT:" + cmd)
            return
        self._seq = seq
        self._seqs.add(seq)

    def _check_status(self, tel):
        st = tel.get("state")
        faults = tel.get("faults") or []
        if tel.get("estop") or st == "ESTOP":
            self._fail("ESTOP")
        elif faults:
            self._fail("FAULT:" + str(faults[0]))
        elif st == "FAULT":
            self._fail("FAULT")
        elif st == "IDLE":
            self._send("ARM", "arm")
        elif st == "ARMED":
            self._send("HOME", "home")
        elif st == "HOMED":
            self.homed = True
            self._init_done()
        else:
            self._fail("UNEXPECTED_MCU_STATE:" + str(st))

    def _init_done(self):
        if self.homed:
            self.step = "done"
        else:
            self.step = "homing"
            self._t = self.clock()
        self._note("INIT_OK" + ("" if self.homed else ":HOMING"))
        self._out.append(("INIT_DONE", None))

    def _homed(self):
        self.homed = True
        self.step = "done"
        self._note("HOMING_OK")

    def _fail(self, why):
        self.step = "failed"
        self.reason = why
        self._seq = None
        self._note("INIT_ERROR:" + why)
        self._out.append(("INIT_ERROR", {"cause": "init", "why": why}))

    def _flush(self):
        """잠금 밖에서 상태머신에 이벤트를 낸다."""
        while True:
            with self._lock:
                if not self._out:
                    return
                event, payload = self._out.pop(0)
            if not self.fire(event, payload):
                self._note(event + "_NOT_ACCEPTED")

    def _note(self, text):
        if self.log:
            try:
                self.log.event("sys", text)
            except Exception:
                pass                            # 기록 실패가 절차를 막지 않는다
