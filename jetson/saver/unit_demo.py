#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""unit_demo.py — 결선 시연 모드: 유닛별 시연의 장치 명령 절차 (브리지 --unit-demo, D-51)

10/24 KSME 결선은 통합 흐름(S1~S13) 대신 유닛 하나씩 보인다(10/15, 10/18 팀장).
이 모드의 브리지는 상태머신 흐름(S1 자동 초기화 포함)을 돌리지 않고, UI 유닛 화면에서
사람이 누른 시작·정지만 장치로 보낸다. 운용 상태 이름(S1~S13)을 새로 만들지 않는다.

  유닛      시작 (장치 상태를 GET_STATUS 로 확인한 뒤)                       정지
  patient   명령 없음. 레이더와 Teensy 텔레메트리(PPG)를 그대로 보인다
  compress  IDLE: ARM → HOME(원점 완료) → SET_TARGET → START_CPR            STOP_CPR
            ARMED: HOME 부터. HOMED·VACUUM_HOLD·CPR_READY: SET_TARGET 부터
  vacuum    IDLE: ARM → HOME(원점 완료) → VACUUM_ON → VACUUM_OK               VACUUM_OFF
  drive     IDLE: ARM → 압박점 탐색(search.py, v1.1 명령)               탐색 중지(DRIVE_STOP)

규칙
- 진입만으로 움직이지 않는다. 시작은 화면에서 사람이 눌러야 한다(자동 시작 없음).
- 명령은 요청이다. 허용 여부는 Teensy 인터록이 정하고, 거부 사유를 그대로 UI 에 보인다.
  표준 펌웨어(v1.0 §5)는 흡착(VACUUM_HOLD) 없이 START_CPR 을 NOT_READY 로 거부한다. 흡착 없이 압박을
  보이려면 펌웨어 쪽 시연 허용이 필요하다(한건민 합의 전. jetson/README.md 결선 시연 모드).
- 목표값은 CONFIG(확정 설정 파일)만 쓴다. 목표 깊이가 비어 있으면 아무 명령도 보내지 않고 끝낸다.
- 한 번에 유닛 하나. 다른 유닛 화면에 들어가거나 화면을 나가거나 UI 연결이 모두 끊기면
  진행 중인 동작을 멈춘다. 비상정지, 고장(sev 3), 링크 두절, 응답 없음은 실패로 끝낸다.

담당하지 않는 것: 실시간 제어와 인터록(Teensy), 탐색 판정(search.py), 전이 규칙(state_machine_v2.json).
Python 3.6 호환.
"""
import threading
import time

from . import protocol as P
from . import search as S

UNITS = ("patient", "compress", "drive", "vacuum")
ACTIONS = ("enter", "leave", "start", "stop")
BUSY = ("starting", "running", "stopping")
# 원점 복귀·흡착 완료를 기다리는 화면 대기 한도(초). 장치 안전 판단이 아니다(인터록과 정지는 Teensy 가 맡는다).
STEP_TIMEOUT_S = 30.0
# GET_STATUS 뒤 텔레메트리를 기다리는 시간
STATUS_TEL_S = 1.0
FAULT_STATES = ("FAULT", "ESTOP")


class UnitDemo(object):

    def __init__(self, link, cfg, search=None, eapd=None, push=None, logger=None, clock=None):
        self.link = link
        self.cfg = cfg
        self.search = search
        self.eapd = eapd or (lambda: None)      # 탐색 시작 순간의 EAPD(mm). 없으면 None
        self.push = push or (lambda msg: None)
        self.log = logger
        self.clock = clock or time.monotonic
        self.screen = None          # UI 가 보고 있는 유닛 화면(enter, leave)
        self.unit = None            # 마지막으로 시작한 유닛
        self.phase = "idle"         # idle | starting | running | stopping | stopped | done | failed
        self.step = None            # 보낸 명령이나 기다리는 것
        self.reason = None
        self._plan = []             # 남은 단계 [(명령, 인자, 기다릴 것)]
        self._seq = None            # 지금 ACK 를 기다리는 명령의 seq
        self._seqs = set()          # 이 모듈이 보낸 명령의 seq
        self._wait = None           # ack | status_tel | homed | homing | vacuum_ok | vacuum | search | stop
        self._flag = None           # ACK 보다 먼저 온 완료 이벤트(HOMING_DONE, VACUUM_OK)
        self._ran = False           # 압박: 텔레메트리에서 CPR_RUNNING 을 본 뒤에만 이탈을 감시한다
        self._eapd = None           # 구동: 시작 판단 때 읽은 EAPD. 탐색에 같은 값을 쓴다
        self._t = 0.0
        # 리더 스레드(ACK, tel, evt)와 asyncio 틱, WebSocket 요청이 함께 부른다.
        # 탐색 모듈 호출과 UI 알림은 잠금을 푼 뒤에 한다(탐색 모듈 잠금과 엇갈리지 않게).
        self._lock = threading.RLock()
        self._calls = []
        self._dirty = False

    # ── 조회 ────────────────────────────────────────────────
    @property
    def busy(self):
        return self.phase in BUSY

    def owns(self, seq):
        """이 모듈이 보낸 명령인지. 브리지는 이 명령의 거부를 일반 규칙으로 처리하지 않는다."""
        return seq is not None and seq in self._seqs

    def snapshot(self):
        return {"type": "unit", "screen": self.screen, "unit": self.unit, "phase": self.phase,
                "step": self.step, "reason": self.reason, "mcu": self.link.mcu_state}

    # ── UI 요청 ─────────────────────────────────────────────
    def request(self, unit, action):
        """UI 요청. (ok, 거부 사유)를 돌려주고, 결과 상태는 push 로 알린다."""
        with self._lock:
            r = self._request(unit, action)
            self._dirty = True
        self._emit()
        return r

    def _request(self, unit, action):
        if action not in ACTIONS:
            return False, "UNKNOWN_ACTION"
        if action == "leave":
            if self.busy:
                self._stop("LEFT_SCREEN")
            self.screen = None
            return True, None
        if unit not in UNITS:
            return False, "UNKNOWN_UNIT"
        if action == "enter":
            if self.busy and self.unit != unit:
                self._stop("LEFT_SCREEN")
            if self.screen != unit and not self.busy:
                self._clear()                   # 다른 유닛의 지난 결과는 지운다
            self.screen = unit
            return True, None
        if action == "start":
            if unit != self.screen:
                return False, "NOT_ON_SCREEN"   # 보고 있지 않은 유닛은 움직이지 않는다
            if unit == "patient":
                return False, "NO_ACTION"
            if self.busy:
                return False, "BUSY"
            self._begin(unit)
            return True, None
        # stop
        if self.unit == unit and self.busy:
            self._stop("USER_STOP")
        return True, None

    def ui_gone(self):
        """UI 연결이 모두 끊겼다. 진행 중인 동작을 멈춘다."""
        with self._lock:
            if self.busy:
                self._stop("UI_GONE")
            self.screen = None
            self._dirty = True
        self._emit()

    # ── 장치에서 온 것 ──────────────────────────────────────
    def on_ack(self, msg):
        with self._lock:
            self._on_ack(msg)
        self._emit()

    def _on_ack(self, msg):
        seq = msg.get("seq")
        if seq is None or seq != self._seq:
            return
        self._seq = None
        cmd = self.step
        if not msg.get("ok", False):
            why = msg.get("why") or msg.get("err") or "REJECTED"
            if self._wait == "stop":
                self._fail("STOP_REJECTED:" + str(why))
            else:
                self._fail(str(why))
                if cmd == "START_CPR":
                    self._note("UNIT_START_REJECTED:" + str(why))
            return
        w = self._wait
        if w == "stop":
            self._done_phase("stopped")
        elif w == "status":
            self._wait = "status_tel"
            self._t = self.clock()
        elif w == "homed":
            if self._flag == "HOMING_DONE" or self.link.mcu_state == "HOMED":
                self._next()
            else:
                self._wait = "homing"
                self._t = self.clock()
        elif w == "vacuum_ok":
            if self._flag == "VACUUM_OK" or self.link.mcu_state == "VACUUM_HOLD":
                self._running()
            else:
                self._wait = "vacuum"
                self._t = self.clock()
        elif cmd == "START_CPR":
            self._running()
        else:
            self._next()

    def on_tel(self, msg):
        with self._lock:
            self._on_tel(msg)
        self._emit()

    def _on_tel(self, msg):
        st = msg.get("state")
        bad = bool(msg.get("estop")) or st in FAULT_STATES
        w = self._wait
        if w == "status_tel":
            self._decide(msg)
            return
        if not self.busy:
            return
        if bad and w != "stop":
            self._fail("ESTOP" if (msg.get("estop") or st == "ESTOP") else "FAULT")
            return
        if w == "homing":
            if st == "HOMED":
                self._next()
            elif st is not None and st not in ("ARMED", "HOMED"):
                self._fail("HOMING_LOST:" + str(st))
        elif w == "vacuum":
            if st == "VACUUM_HOLD":
                self._running()
        elif self.phase == "running" and self.unit == "compress":
            if st == "CPR_RUNNING":
                self._ran = True
            elif self._ran and st is not None and st not in ("CPR_RUNNING", "CPR_PAUSED"):
                self._done_phase("stopped", "DEVICE_STOPPED")     # 장치가 스스로 멈췄다

    def on_event(self, msg):
        with self._lock:
            self._on_event(msg)
        self._emit()

    def _on_event(self, msg):
        name = msg.get("evt", "")
        w = self._wait
        if name == "HOMING_DONE" and w in ("homed", "homing"):
            if msg.get("result", "OK") != "OK":
                self._fail("HOMING_" + str(msg.get("result")))
            elif w == "homing":
                self._next()
            else:
                self._flag = "HOMING_DONE"          # HOME ACK 보다 먼저 처리된 경우
        elif name == "VACUUM_OK" and w in ("vacuum_ok", "vacuum"):
            if w == "vacuum":
                self._running()
            else:
                self._flag = "VACUUM_OK"
        elif name == "VACUUM_FAIL" and self.unit == "vacuum" and self.busy and w != "stop":
            self._fail("VACUUM_FAIL")

    def on_error(self, msg):
        with self._lock:
            self._on_error(msg)
        self._emit()

    def _on_error(self, msg):
        seq = msg.get("seq")
        why = str(msg.get("why", ""))
        if seq is not None and seq == self._seq and why.startswith("ACK_TIMEOUT"):
            self._seq = None
            self._fail("TIMEOUT")
            return
        if self.owns(seq):
            return                                  # 거부(ok=false)는 on_ack 가 처리한다
        sev = msg.get("sev") or P.severity(msg.get("code"))
        if sev >= 3 and self.busy and self._wait != "stop":
            self._fail(str(msg.get("code") or "FAULT"))

    def on_link(self, connected):
        with self._lock:
            if not connected and self.busy:
                self._fail("LINK_DOWN")             # 장치 쪽 정지는 Teensy 통신 감시가 맡는다
            self._dirty = True
        self._emit()

    def on_search_done(self, ok, snap):
        with self._lock:
            if self.unit == "drive" and self._wait == "search":
                reason = (snap or {}).get("reason")
                if ok:
                    self._done_phase("done")
                elif reason in ("USER_STOP", "LEFT_NODE", "LEFT_SCREEN", "UI_GONE"):
                    self._done_phase("stopped", reason)
                else:
                    self._done_phase("failed", reason or "SEARCH_FAIL")
        self._emit()

    def tick(self):
        with self._lock:
            now = self.clock()
            w = self._wait
            if w == "status_tel" and now - self._t > STATUS_TEL_S:
                self._fail("NO_STATUS")
            elif w in ("homing",) and now - self._t > STEP_TIMEOUT_S:
                self._fail("HOME_TIMEOUT")
            elif w in ("vacuum",) and now - self._t > STEP_TIMEOUT_S:
                self._fail("VACUUM_TIMEOUT")
                self._send_quiet("VACUUM_OFF", {})   # 목표에 못 닿은 채 블로워를 켜 두지 않는다
        self._emit()

    # ── 내부 ────────────────────────────────────────────────
    def _clear(self):
        self.unit = None
        self.phase = "idle"
        self.step = None
        self.reason = None
        self._plan = []
        self._seq = None
        self._wait = None
        self._flag = None
        self._ran = False
        self._eapd = None

    def _begin(self, unit):
        self._clear()
        self.unit = unit
        self.phase = "starting"
        self._note("UNIT_START:" + unit)
        if not self.link.connected:
            self._fail("NOT_CONNECTED")
        elif self.link.proto is None:
            self._fail("LINK_CHECKING")         # 펌웨어 판(GET_INFO) 확인 전. 잠시 뒤 다시
        elif self.link.proto != P.PROTO_REV:
            self._fail("FW_VERSION")            # v1.1 펌웨어가 아님. 장치로 동작 명령을 보내지 않는다
        elif unit == "compress" and not self.cfg.get("target_depth_mm"):
            self._fail("TARGET_NOT_SET")        # 미확정 목표로 움직이지 않는다(원점 복귀도 하지 않음)
        else:
            self._send("GET_STATUS", {}, "status")

    def _decide(self, tel):
        """GET_STATUS 뒤 텔레메트리로 보낼 순서를 정한다."""
        st = tel.get("state")
        faults = tel.get("faults") or []
        if tel.get("estop") or st == "ESTOP":
            return self._fail("ESTOP")
        if st == "FAULT" or faults:
            return self._fail("FAULT:" + str(faults[0]) if faults else "FAULT")
        u = self.unit
        plan = []
        if u == "drive":
            self._eapd = self.eapd()
            if not self._drive_ready(self._eapd):
                return self._start_search()     # 탐색 모듈이 사유(설정 없음, 미지원, EAPD 없음)를 낸다
            if st == "IDLE":
                plan.append(("ARM", {}, "ack"))
            elif st not in ("ARMED", "HOMED"):
                return self._fail("STATE:" + str(st))
            plan.append(("SEARCH", None, "search"))
        elif u == "compress":
            if st == "CPR_RUNNING":
                self._ran = True
                return self._running()
            if st == "IDLE":
                plan.append(("ARM", {}, "ack"))
            if st in ("IDLE", "ARMED"):
                plan.append(("HOME", {}, "homed"))
            elif st not in ("HOMED", "VACUUM_HOLD", "CPR_READY"):
                return self._fail("STATE:" + str(st))
            plan.append(("SET_TARGET", self._targets(), "ack"))
            plan.append(("START_CPR", {}, "ack"))
        elif u == "vacuum":
            if st == "VACUUM_HOLD":
                return self._running()
            if st == "IDLE":
                plan.append(("ARM", {}, "ack"))
            if st in ("IDLE", "ARMED"):
                plan.append(("HOME", {}, "homed"))
            elif st != "HOMED":
                return self._fail("STATE:" + str(st))
            kw = {}
            if self.cfg.get("vacuum_target_kPa"):
                kw["target_kPa"] = self.cfg["vacuum_target_kPa"]
            plan.append(("VACUUM_ON", kw, "vacuum_ok"))
        self._plan = plan
        self._next()

    def _targets(self):
        """SET_TARGET 인자. 비어 있는 값은 보내지 않는다(브리지 S6 과 같은 규칙)."""
        kw = {"bpm": self.cfg.get("target_bpm")}
        if self.cfg.get("target_depth_mm"):
            kw["depth_mm"] = self.cfg["target_depth_mm"]
        if self.cfg.get("force_max_N"):
            kw["force_max_N"] = self.cfg["force_max_N"]
        return kw

    def _drive_ready(self, eapd):
        """탐색 모듈이 시작을 받아들일 조건(search.py start 와 같은 검사). 아니면 ARM 없이 탐색에 맡겨 사유를 받는다."""
        p, bad = S.check_params(self.cfg)
        if bad:
            return False
        if self.link.proto != P.PROTO_REV or not set(S.NEED_CAPS) <= set(self.link.caps or ()):
            return False
        return S.cp_distance(eapd, p) is not None

    def _next(self):
        if not self._plan:
            return
        cmd, kw, wait = self._plan.pop(0)
        if cmd == "SEARCH":
            self._start_search()
        else:
            self._send(cmd, kw, wait)

    def _start_search(self):
        self.step = "SEARCH"
        self._wait = "search"
        self.phase = "running"
        self._dirty = True
        eapd = self._eapd
        self._calls.append(lambda: self.search.start(eapd))

    def _send(self, cmd, kw, wait):
        self.step = cmd
        self._wait = wait
        self._flag = None
        self._dirty = True
        self._t = self.clock()
        seq = self.link.send(cmd, **kw)
        if seq is None:                         # 보류(펌웨어 판 확인 전)나 aid 소진
            self._fail("NOT_SENT:" + cmd)
            return
        self._seq = seq
        self._seqs.add(seq)

    def _send_quiet(self, cmd, kw):
        """결과를 기다리지 않는 정지 방향 명령."""
        seq = self.link.send(cmd, **kw)
        if seq is not None:
            self._seqs.add(seq)

    def _running(self):
        self.phase = "running"
        self._wait = None
        self._plan = []
        self._note("UNIT_RUNNING:" + str(self.unit))

    def _stop(self, why):
        u = self.unit
        self._plan = []
        self._note("UNIT_STOP:%s:%s" % (u, why))
        if u == "drive":
            if self._wait == "search":
                self.phase = "stopping"
                self.step = "DRIVE_STOP"
                reason = "USER_STOP" if why == "USER_STOP" else "LEFT_NODE"
                self._calls.append(lambda: self.search.cancel(reason))
            else:
                self._done_phase("stopped", why)
            return
        if u == "compress":
            cmd, kw = "STOP_CPR", {"reason": "USER_STOP" if why in ("USER_STOP", "LEFT_SCREEN") else "SAFETY"}
        elif u == "vacuum":
            cmd, kw = "VACUUM_OFF", {}
        else:
            self._done_phase("stopped", why)
            return
        if not self.link.connected:
            self._done_phase("failed", "LINK_DOWN")
            return
        self.phase = "stopping"
        self.reason = why
        self._send(cmd, kw, "stop")

    def _done_phase(self, phase, reason=None):
        self.phase = phase
        if reason is not None:
            self.reason = reason
        self._wait = None
        self._plan = []
        self._seq = None
        self._note("UNIT_%s:%s:%s" % (phase.upper(), self.unit, self.reason))

    def _fail(self, why):
        unit = self.unit
        # 압박이 돌고 있었거나 START_CPR 의 응답을 못 받았으면 모터 정지를 한 번 더 요청한다
        # (sev 3 이면 Teensy 가 이미 껐다. 정지 방향 명령이라 해가 없다)
        need_stop = unit == "compress" and (self.phase == "running" or
                                            (self.step == "START_CPR" and why == "TIMEOUT"))
        self._done_phase("failed", why)
        if need_stop and self.link.connected:
            self._send_quiet("STOP_CPR", {"reason": "SAFETY"})
        elif unit == "drive" and self.search is not None and getattr(self.search, "active", False):
            self._calls.append(lambda: self.search.cancel("LEFT_NODE"))

    def _emit(self):
        while True:
            with self._lock:
                calls, self._calls = self._calls, []
            if not calls:
                break
            for c in calls:
                try:
                    c()
                except Exception as e:              # 탐색 모듈 오류가 브리지를 멈추지 않게
                    self._note("UNIT_CALL_ERROR:" + str(e))
        with self._lock:
            if not self._dirty:
                return
            self._dirty = False
            snap = self.snapshot()
        self.push(snap)

    def _note(self, text):
        self._dirty = True
        if self.log:
            try:
                self.log.event("sys", text)
            except Exception:
                pass                                # 기록 실패가 절차를 막지 않는다
