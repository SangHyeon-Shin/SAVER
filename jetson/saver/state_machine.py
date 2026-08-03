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
                 mcu_state=None):
        import threading
        self._lock = threading.RLock()   # 리더 스레드와 asyncio 틱이 함께 호출한다
        with open(spec_path or DEFAULT_SPEC, "r", encoding="utf-8") as f:
            self.spec = json.load(f)
        self.nodes = self.spec["nodes"]
        self.events = self.spec["events"]
        self.on_change = on_change or (lambda snap, ev: None)
        self.on_command = on_command or (lambda cmd: None)
        # 장치(Teensy) 현재 상태를 돌려주는 함수. None 이면 검사하지 않는다.
        self.mcu_state = mcu_state or (lambda: None)
        self.blocked_reason = None

        self.node = self.spec["initial"]
        self.error_return = "S2"
        self.error_cause = None
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

        # 장치 선행조건 검사 — UI 가 장치보다 앞서가는 것을 막는다
        need = self.nodes[target if target != "@return" else self.node].get("requires_mcu")
        if need:
            cur_mcu = self.mcu_state()
            if cur_mcu is not None and cur_mcu not in need:
                self.blocked_reason = {"event": event, "target": target,
                                       "need": need, "mcu": cur_mcu}
                return False
        self.blocked_reason = None

        prev = self.node
        if target == "@return":
            target = self.error_return or "S2"

        if target == "S10":
            self.error_return = self.cur.get("error_return", "S2")
            self.error_cause = (
                "position" if event == "POSITION_ERROR" else
                "vacuum" if event == "VACUUM_ERROR" else
                payload.get("cause", "tubing"))
        else:
            self.error_cause = None

        self.node = target
        self._entered_at = time.time()
        self.log.append({"ts": time.time(), "from": prev, "event": event,
                         "to": target, "state": self.nodes[target].get("state")})
        if len(self.log) > 500:
            self.log = self.log[-500:]

        for cmd in self.nodes[target].get("enter_cmds", []):
            self.on_command(cmd)

        self.on_change(self.snapshot(), event)
        return True

    def auto_due(self):
        """자동 전이 시각이 됐으면 이벤트 이름을 돌려준다 (S1 부팅 등)."""
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
