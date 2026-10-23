#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""state_machine.py — SAVER 운용 상태머신 (S1~S13)

전이표는 protocol/state_machine_v2.json 이 유일한 기준입니다.
UI(React)도 같은 파일을 읽으므로, 여기서 규칙을 추가로 만들지 마세요.
"""
import json
import os
import time

_HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_SPEC = os.path.normpath(
    os.path.join(_HERE, "..", "..", "protocol", "state_machine_v2.json"))


class StateMachine(object):

    def __init__(self, spec_path=None, on_change=None, on_command=None,
                 mcu_state=None, mcu_flags=None):
        import threading
        self._lock = threading.RLock()   # 리더 스레드와 asyncio 틱이 함께 호출한다
        with open(spec_path or DEFAULT_SPEC, "r", encoding="utf-8") as f:   # 한국어 Windows 기본(cp949)으로 읽지 않게
            self.spec = json.load(f)
        self.nodes = self.spec["nodes"]
        self.events = self.spec["events"]
        self.on_change = on_change or (lambda snap, ev: None)
        self.on_command = on_command or (lambda cmd: None)
        # 장치(Teensy) 현재 상태를 돌려주는 함수. None 이면 검사하지 않는다.
        self.mcu_state = mcu_state or (lambda: None)
        # 장치 플래그(mot 의 rearm_lock, vac_released 등)를 dict 로 돌려주는 함수.
        # None 을 돌려주면 장치가 플래그를 보내지 않는 것(v1.0)이므로 redirect 를 검사하지 않는다.
        # dict 안의 값이 None 이면 값이 없거나 오래된 것이다.
        self.mcu_flags = mcu_flags or (lambda: None)
        self.blocked_reason = None
        self._held = None                # (event, node, 마감 시각) 장치 이벤트 보류

        self.node = self.spec["initial"]
        self.error_return = "S2"
        self.error_cause = None
        self.error_why = None            # 오류 화면(S10)에 보일 세부 사유(예: INIT_ERROR 의 why). 없으면 None
        self.log = []
        self._entered_at = time.time()

    def enter_initial(self):
        """시작 노드의 enter_cmds 를 1회 실행한다.
        전이로 진입한 게 아니라서 fire() 가 호출되지 않기 때문에 필요하다."""
        for cmd in self.cur.get("enter_cmds", []):
            self.on_command(cmd)
        self.on_change(self.snapshot(), "BOOT")

    # ── 조회 ────────────────────────────────────────────────
    @property
    def cur(self):
        return self.nodes[self.node]

    def is_running(self):
        return bool(self.cur.get("running"))

    def allowed(self):
        return sorted(self.cur.get("on", {}).keys())

    def snapshot(self):
        n = self.cur
        return {
            "node": self.node,
            "state": n.get("state"),
            "ui": n.get("ui"),
            "screen": n.get("screen"),
            "name": n.get("name"),
            "alarm": n.get("alarm", "normal"),
            "running": bool(n.get("running")),
            "allowed": self.allowed(),
            "error_cause": self.error_cause,
            "error_why": self.error_why,
        }

    # ── 전이 ────────────────────────────────────────────────
    def fire(self, event, payload=None):
        """이벤트 적용. 허용되지 않으면 False 를 돌려주고 상태는 그대로 둔다."""
        with self._lock:
            return self._fire(event, payload)

    def _fire(self, event, payload=None):
        payload = payload or {}
        target = self.cur.get("on", {}).get(event)
        if not target:
            return False
        if target == "@return":
            target = self.error_return or "S2"

        # 장치 플래그에 따른 목적지 변경 (예: 음압이 남아 있으면 S13 → S12)
        redirected = self._redirect(event)
        if redirected:
            target = redirected

        # 이벤트별 장치 조건 (v2.3) — 일시정지 노드의 재개는 장치가 CPR_PAUSED 일 때만
        need = self.cur.get("event_requires_mcu", {}).get(event)
        if need:
            cur_mcu = self.mcu_state()
            if cur_mcu is not None and cur_mcu not in need:
                self.blocked_reason = {"event": event, "target": target,
                                       "need": need, "mcu": cur_mcu}
                return False

        # 장치 선행조건 검사 — UI 가 장치보다 앞서가는 것을 막는다
        need = self.nodes[target].get("requires_mcu")
        if need:
            cur_mcu = self.mcu_state()
            if cur_mcu is not None and cur_mcu not in need:
                self.blocked_reason = {"event": event, "target": target,
                                       "need": need, "mcu": cur_mcu}
                return False
        self.blocked_reason = None

        prev = self.node

        if target == "S10":
            # 이벤트별 복귀 노드(v2.4): 초기화 오류는 어느 노드에서 났든 S1 에서 다시 점검한다
            self.error_return = (self.spec.get("error_return_by_event", {}).get(event)
                                 or self.cur.get("error_return", "S2"))
            self.error_cause = (
                "position" if event == "POSITION_ERROR" else
                "vacuum" if event == "VACUUM_ERROR" else
                "init" if event == "INIT_ERROR" else
                payload.get("cause", "tubing"))
            self.error_why = payload.get("why")
        else:
            self.error_cause = None
            self.error_why = None

        self.node = target
        self._entered_at = time.time()
        entry = {"ts": time.time(), "from": prev, "event": event,
                 "to": target, "state": self.nodes[target].get("state")}
        if redirected:
            entry["redirect"] = True
        self.log.append(entry)
        if len(self.log) > 500:
            self.log = self.log[-500:]

        for cmd in self.nodes[target].get("enter_cmds", []):
            self.on_command(cmd)

        self.on_change(self.snapshot(), event)
        return True

    def fire_device(self, event, ttl_s):
        """장치(Teensy)가 보낸 이벤트. 장치 상태 조건(requires_mcu)에 막히면 ttl_s 동안 보류하고
        다음 텔레메트리에서 retry_device() 로 한 번 더 확인한다.
        장치는 상태가 바뀌는 순간 이벤트를 보내고, 바뀐 상태는 다음 tel 에 실린다.
        이벤트가 tel 보다 먼저 오면 직전 tel 의 상태로 검사해 막히기 때문이다(예: VACUUM_OK)."""
        with self._lock:
            ok = self._fire(event)
            if not ok and self.blocked_reason and self.blocked_reason["event"] == event:
                self._held = (event, self.node, time.monotonic() + ttl_s)
            return ok

    def retry_device(self):
        """보류한 장치 이벤트를 다시 확인한다. ("fired"|"expired", 이벤트) 또는 None.
        노드가 바뀌었거나 마감이 지나면 버린다(오래된 이벤트로 전이하지 않는다)."""
        with self._lock:
            h = self._held
            if not h:
                return None
            event, node, deadline = h
            if self.node != node or time.monotonic() > deadline:
                self._held = None
                return ("expired", event)
            if self._fire(event):
                self._held = None
                return ("fired", event)
            return None

    def _redirect(self, event):
        """현재 노드의 redirect 규칙에 걸리면 바뀐 목적지를, 아니면 None 을 돌려준다."""
        rule = self.cur.get("redirect", {}).get(event)
        if not rule:
            return None
        flags = self.mcu_flags()
        if flags is None:                  # 장치가 플래그를 보내지 않음(v1.0)
            return None
        for key, values in rule.get("when_mcu", {}).items():
            if flags.get(key) in values:   # 값 없음·오래됨(None)도 목록에 있으면 해당
                return rule["to"]
        return None

    def procedure(self):
        """현재 노드의 절차 이름(v2.4). 예: S1 의 "init". 없으면 None."""
        return self.cur.get("procedure")

    def auto_due(self):
        """자동 전이 시각이 됐으면 이벤트 이름을 돌려준다.
        v2.4 부터 이 기능을 쓰는 노드는 없다. offline_auto(오프라인 시연 전용)는 읽지 않는다."""
        a = self.cur.get("auto")
        if not a:
            return None
        if (time.time() - self._entered_at) * 1000 >= a["ms"]:
            return a["event"]
        return None

    def force(self, node, reason="FORCE"):
        """강제 이동. 안전정지 등 전이표에 없는 경로로 갈 때만 씁니다.
        로그를 남기지 않으면 다음 on_change 가 직전 전이를 중복 기록합니다."""
        with self._lock:
            if node not in self.nodes:
                return False
            prev = self.node
            self.node = node
            self._entered_at = time.time()
            self.log.append({"ts": time.time(), "from": prev, "event": reason,
                             "to": node, "state": self.nodes[node].get("state")})
            self.on_change(self.snapshot(), reason)
            return True
