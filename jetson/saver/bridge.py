#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""bridge.py — SAVER Jetson 통합 진입점

    React UI  ←ws://0.0.0.0:8765→  bridge  ←USB Serial→  Teensy 4.1

이 프로세스가 상태머신의 주인입니다. UI 는 화면일 뿐입니다.
  UI → {"type":"intent","name":"CPR_START"}      전이 요청
  UI ← {"type":"state", ...}                     확정된 상태 (UI 는 그대로 그림)
  UI ← {"type":"telemetry", ...}                 실측값 (stale 이면 stale:true)
  UI ← {"type":"error", ...}                     오류
  UI → {"type":"search","action":"start"|"stop"} 압박점 탐색 시작·중지 (X_SEARCH 에서만)
  UI ← {"type":"search", ...}                    탐색 진행 (반력 곡선, 판정, D, 압박점)
  UI ← {"type":"radar", ...}                     MR60BHA2 레이더(1초마다): 호흡수, 심박수, 거리, EAPD.
                                                 측정 못 했거나 오래된 값은 보내지 않는다(radar.py)
  UI ← {"type":"config", "mode":"operate"|"unit_demo", 목표값...}

결선 시연 모드(--unit-demo, D-51): 상태머신 흐름(S1 자동 초기화 포함)을 돌리지 않는다.
  UI → {"type":"unit","unit":"patient"|"compress"|"drive"|"vacuum","action":"enter"|"leave"|"start"|"stop"}
  UI ← {"type":"unit", screen, unit, phase, step, reason, mcu}   (unit_demo.py)

실행
  python3 -m saver.bridge                                  # 시리얼 없이 (UI 시험)
  python3 -m saver.bridge --serial /tmp/ttyFAKE            # 가짜 Teensy 연결
  python3 -m saver.bridge --serial /dev/ttyACM0 --serve dist --http-port 8080
  python3 -m saver.bridge --radar /dev/ttyTHS1             # MR60BHA2 레이더(40핀 UART)
  python3 -m saver.bridge --unit-demo --serial /tmp/ttyFAKE   # 결선 시연 모드(유닛별 시연)
"""
import argparse
import asyncio
import functools
import json
import os
import sys
import threading
import time

try:
    import websockets
except ImportError:
    sys.exit("websockets 가 필요합니다:  pip3 install websockets\n"
             "  ※ 젯슨 나노(Python 3.6)면  pip3 install 'websockets==9.1'")

if __package__ in (None, ""):                     # 직접 실행 대응
    sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    from saver import protocol as P
    from saver.state_machine import StateMachine
    from saver.teensy_serial import TeensyLink
    from saver.logger import CsvLogger
    from saver.search import SearchRunner, PARAM_KEYS as SEARCH_KEYS
    from saver.init_seq import InitRunner
    from saver.radar import RadarLink
    from saver.unit_demo import UnitDemo
else:
    from . import protocol as P
    from .state_machine import StateMachine
    from .teensy_serial import TeensyLink
    from .logger import CsvLogger
    from .search import SearchRunner, PARAM_KEYS as SEARCH_KEYS
    from .init_seq import InitRunner
    from .radar import RadarLink
    from .unit_demo import UnitDemo


# ─────────────────────────────────────────────────────────────
# ⚠ 미확정 설정값 (명세 §11). 0 이면 해당 파라미터를 보내지 않고,
#   Teensy 는 목표 깊이가 없으면 START_CPR 을 거부합니다.
#   추측으로 채우지 말고 확정된 근거가 나온 뒤에만 넣으세요.
CONFIG = {
    "target_bpm": 110,            # 확정
    "target_depth_mm": 0,         # TODO 한성윤 (가이드라인 근거)
    "force_max_N": 0,             # TODO 한성윤 / 서정준
    "vacuum_target_kPa": 0,       # TODO 서정준 + 한건민
    "aed_pause_warn_s": None,     # TODO 한성윤. AED 분석·충격 중 압박 중단 경고 기준(초). 비어 있으면 경고 안 함
    "pause_stop_verified": False, # TODO 한건민. PAUSE_CPR 로 압박 모터가 실제로 멈추는 것을 실기에서 확인한 뒤 true
                                  # (인터록 점검표 C11). false 면 장치 연결 중 인공호흡 안내(X_VENT)에 들어가지 않는다(D-45)
    "home_timeout_s": None,       # TODO 한건민. S1 초기화 뒤 원점 완료(HOMING_DONE)를 기다리는 시간 제한(초). 10/12 실측 뒤.
                                  # 비어 있으면 시간은 검사하지 않고, 탐색·흡착 진입은 requires_mcu(HOMED)가 막는다(D-46)
    "radar_floor_mm": None,       # TODO 신상현(실측). 탐색 자세(바퀴 내림)에서 레이더 면의 바닥 위 높이(mm).
                                  # EAPD = radar_floor_mm − 레이더 거리(radar.py, D-50). 비어 있으면 레이더로 EAPD 를
                                  # 만들지 않아 탐색이 EAPD_MISSING 으로 시작하지 않는다(대체 상수 없음)
}
# 압박점 탐색(saver/search.py, 초안 §7.2). 전부 비어 있으면 탐색을 시작하지 않는다.
CONFIG.update(dict((k, None) for k in SEARCH_KEYS))

# 장치가 정하는 이벤트. 장치에 연결돼 있으면 UI 가 보내도 받지 않는다(압박점을 임의로 확정하지 않는다).
DEVICE_OWNED = ("SEARCH_DONE",)
# 이 노드에 들어오면 이전 탐색 결과를 버린다.
CP_RESET_NODES = ("S1", "S2", "X_SEARCH", "S12", "S13")
# 구조자가 환자 가슴과 얼굴 가까이 손과 얼굴을 대는 안내 화면. 장치에 연결된 동안에는
#   ① 진입 명령에 압박 정지(PAUSE_CPR)가 없으면(D-44)
#   ② PAUSE_CPR 로 압박 모터가 실제로 멈추는 것을 실기에서 확인하기 전이면(D-45, pause_stop_verified)
# 들어가지 않는다.
HANDS_ON_NODES = ("X_VENT",)


def hands_on_block_reason(fsm, name, cfg=None):
    """이 이벤트의 목적지가 압박을 멈췄다고 믿을 수 없는 손 대기 화면이면 사유, 아니면 None"""
    cfg = CONFIG if cfg is None else cfg
    target = fsm.cur.get("on", {}).get(name)
    if target not in HANDS_ON_NODES:
        return None
    if "PAUSE_CPR" not in fsm.nodes[target].get("enter_cmds", []):
        return "no_pause_cmd"
    if not cfg.get("pause_stop_verified"):
        return "pause_unverified"
    return None


def hands_on_blocked(fsm, name, cfg=None):
    return hands_on_block_reason(fsm, name, cfg) is not None


def load_config(path):
    """설정 파일로 미확정 값을 채웁니다.
    config.sim.json 은 시뮬레이션 전용이며 의학적 근거가 없습니다."""
    if not path:
        return
    with open(path, encoding="utf-8") as f:
        data = json.load(f)
    sim = bool(data.pop("_simulation_only", False))
    if not sim and "sim_eapd_mm" in data:
        print("[주의] sim_eapd_mm 은 시뮬레이션 설정에서만 씁니다. 무시합니다.")
        data.pop("sim_eapd_mm")
    CONFIG.update({k: v for k, v in data.items() if not k.startswith("_")})
    if sim:
        print("=" * 62)
        print(" ⚠ 시뮬레이션 설정을 불러왔습니다 (%s)" % path)
        print("   이 값들은 의학적 근거가 없습니다. 실제 장비 구동에 쓰지 마세요.")
        print("=" * 62)


def warn_unset():
    miss = [k for k, v in CONFIG.items() if not v and k != "sim_eapd_mm"]
    if miss:
        print("[주의] 미확정 설정값: %s" % ", ".join(miss))
    if "target_depth_mm" in miss:
        print("       목표 깊이가 확정되기 전까지 압박 개시(START_CPR)는 Teensy가 거부합니다. 정상입니다.")


class Bridge(object):

    LINK_LOG_S = 10.0       # 링크 품질 누적 횟수를 세션 기록에 남기는 주기
    RADAR_PUSH_S = 1.0      # 레이더 값을 UI 로 보내고 기록하는 주기

    def __init__(self, args):
        self.args = args
        # 결선 시연 모드(D-51): 상태머신 흐름을 돌리지 않고 유닛 화면의 시작·정지만 장치로 보낸다
        self.unit_demo = bool(getattr(args, "unit_demo", False))
        self.clients = set()
        self.loop = None
        self.log = CsvLogger(args.logdir)
        self._link_logged_at = time.monotonic()
        # MR60BHA2 레이더(--radar PORT). 없으면 None: 레이더 값과 레이더 EAPD 를 쓰지 않는다
        port = getattr(args, "radar", None)
        self.radar = RadarLink(port, getattr(args, "radar_baud", 115200),
                               on_link=self._on_radar_link) if port else None
        self._radar_at = time.monotonic()

        # 탐색으로 압박점 위로 이동을 마쳤는지. 3단계 탐색 모듈이 켠다.
        # 꺼져 있으면 SET_CP_POINT 를 보내지 않는다(압박점을 임의로 정하지 않는다).
        self.cp_point_ready = False
        # 환자 EAPD(mm). 레이더 모듈이 채운다. 시뮬레이션 설정의 sim_eapd_mm 만 예외다.
        # 없으면 탐색을 시작하지 않는다(대체 상수 없음, 초안 §0.1-3).
        self.eapd_mm = CONFIG.get("sim_eapd_mm")
        self.eapd_sim = self.eapd_mm is not None       # 화면에 '모의'로 표시
        self.fsm = StateMachine(spec_path=args.spec,
                                on_change=self._on_state,
                                on_command=self._on_command,
                                mcu_state=lambda: self.link.mcu_state,
                                mcu_flags=lambda: getattr(self.link, "mcu_flags", None))
        self.link = TeensyLink(port=args.serial,
                               on_telemetry=self._on_tel,
                               on_event=self._on_evt,
                               on_error=self._on_err,
                               on_link=self._on_link,
                               on_mot=self._on_mot,
                               on_ack=self._on_ack,
                               on_proto=self._on_proto,
                               logger=self.log)
        self._link_port = None
        self.search = SearchRunner(self.link, CONFIG, on_update=self._on_search,
                                   on_done=self._on_search_done, logger=self.log)
        # S1 초기화 절차(v2.4, D-46): ACK 와 장치 상태를 보며 PING → GET_STATUS → ARM/HOME 을 차례로 보낸다
        self.init = InitRunner(self.link, fire=lambda ev, payload=None: self.fsm.fire(ev, payload),
                               cfg=CONFIG, logger=self.log)
        self.units = UnitDemo(self.link, CONFIG, search=self.search, eapd=self._unit_eapd,
                              push=self.push, logger=self.log)

    # ── 브로드캐스트 ────────────────────────────────────────
    def push(self, msg):
        if self.loop is None:
            return
        asyncio.run_coroutine_threadsafe(self._push(msg), self.loop)

    async def _push(self, msg):
        if not self.clients:
            return
        data = json.dumps(msg, ensure_ascii=False)
        for ws in list(self.clients):
            try:
                await ws.send(data)
            except Exception:
                self.clients.discard(ws)

    # ── 콜백 ────────────────────────────────────────────────
    def _on_state(self, snap, event):
        if self.fsm.log:
            last = self.fsm.log[-1]
            self.log.transition(last["from"], last["event"], last["to"], last["state"])
        snap["event"] = event
        snap["type"] = "state"
        self.push(snap)
        if snap.get("node") in CP_RESET_NODES:
            self.cp_point_ready = False
        if snap.get("node") != "X_SEARCH" and self.search.active:
            self.search.cancel("LEFT_NODE")        # 탐색 화면을 벗어나면 동작을 멈춘다
        if self.fsm.nodes.get(snap.get("node"), {}).get("procedure") == "init":
            self.init.start()                      # S1 에 들어올 때마다(부팅, 오류 확인 뒤) 처음부터
        elif self.init.step in ("wait_link", "ping", "status", "status_tel", "arm"):
            self.init.cancel("LEFT_NODE")          # 원점 명령 전에 S1 을 벗어남(시연 패널 강제 이동 등)

    def _on_command(self, cmd):
        """상태 진입 시 정의된 Teensy 명령 (state_machine_v2.json 의 enter_cmds)"""
        sent = P.resolve_enter_cmd(cmd, self.link.mcu_state)    # 재개면 RESUME_CPR (v2.2)
        if sent != cmd:
            self.log.event("sys", "CMD_RESOLVED:%s->%s" % (cmd, sent))
        if sent is None:
            return
        cmd = sent
        kw = {}
        if cmd == "SET_TARGET":
            kw = {"bpm": CONFIG["target_bpm"]}
            if CONFIG["target_depth_mm"]:            # 미확정이면 보내지 않는다
                kw["depth_mm"] = CONFIG["target_depth_mm"]
            if CONFIG["force_max_N"]:
                kw["force_max_N"] = CONFIG["force_max_N"]
        elif cmd == "VACUUM_ON":
            if CONFIG["vacuum_target_kPa"]:
                kw = {"target_kPa": CONFIG["vacuum_target_kPa"]}
        elif cmd == "SET_CP_POINT":
            if not self.cp_point_ready:
                return                       # 탐색 결과 없이 압박점을 임의로 정하지 않는다
            kw = {"offset_mm": 0}            # 현재 위치가 압박점(v1.1 §2.2, 탐색 후 고정)
        elif cmd == "STOP_CPR":
            kw = {"reason": "SAFETY"}
        self.link.send(cmd, **kw)

    def _on_tel(self, msg):
        self.log.telemetry(msg)
        if self.unit_demo:
            self.units.on_tel(msg)
        else:
            self.init.on_tel(msg)
            r = self.fsm.retry_device()          # 상태 조건에 막혀 보류한 장치 이벤트(fire_device)
            if r and r[0] == "expired":
                self.log.event("sys", "DEVICE_EVENT_EXPIRED:" + r[1])
        out = dict(msg)
        out["type"] = "telemetry"
        out["stale"] = False
        self.push(out)

    def _on_mot(self, msg):
        """v1.1 mot(동작 상태): 표시용. 판단은 완료 이벤트로 한다(초안 §3.3-6)."""
        out = dict(msg)
        out["type"] = "motion"
        self.push(out)

    def _on_ack(self, msg):
        self.search.on_ack(msg)
        if self.unit_demo:
            self.units.on_ack(msg)
        else:
            self.init.on_ack(msg)

    def _on_search(self, snap):
        out = dict(snap)
        out["type"] = "search"
        out["eapd_sim"] = self.eapd_sim
        self.push(out)

    def _on_search_done(self, ok, snap):
        self._on_search(snap)
        if self.unit_demo:
            self.units.on_search_done(ok, snap)   # 구동 시연: 상태머신 이벤트를 내지 않는다
            return
        if self.fsm.node != "X_SEARCH":
            return
        if ok:
            self.cp_point_ready = True              # S6 에서 SET_CP_POINT(offset_mm=0)
            self.fsm.fire("SEARCH_DONE")
        elif snap.get("moved") and snap.get("reason") not in ("USER_STOP", "LEFT_NODE"):
            self.fsm.fire("SEARCH_FAIL", {"cause": "search"})   # 동작 뒤 실패 → 오류 화면, RECHECK 로 다시 탐색
        # 시작 전 거부(설정 없음, EAPD 없음, 미지원 펌웨어)는 탐색 화면에 사유만 표시한다

    def _on_evt(self, msg):
        self.search.on_event(msg)
        name = msg.get("evt", "")
        self.log.event("mcu", name)
        if self.unit_demo:
            self.units.on_event(msg)
        else:
            self.init.on_event(msg)
            # 장비가 UI 상태를 바꿔야 하는 경우만 상태머신에 주입
            mapping = {"VACUUM_OK": "VACUUM_OK", "VACUUM_FAIL": "VACUUM_ERROR"}
            if name in mapping:
                self.fsm.fire_device(mapping[name], P.STALE_S)
        out = dict(msg)
        out["type"] = "device_event"
        self.push(out)

    def _on_err(self, msg):
        self.log.error(msg)
        sev = msg.get("sev") or P.severity(msg.get("code"))
        out = {"type": "error", "code": msg.get("code", ""), "sev": sev,
               "why": msg.get("why", ""), "latched": bool(msg.get("latched")),
               "desc": P.ERR.get(msg.get("code", ""), "")}
        if msg.get("replay"):
            out["replay"] = True
        self.push(out)
        if msg.get("replay"):
            return                            # GET_FAULTS 로 다시 받은 이미 아는 고장: 표시만 한다(v1.1 §3.5)
        if self.unit_demo:
            self.units.on_error(msg)          # 시연 모드: 상태머신 대신 유닛 동작을 멈추고 실패로 알린다
            return
        if self.init.handles(msg):
            # 초기화 명령의 거부·무응답, 연결 대기 중 펌웨어 판 불일치는 절차가 INIT_ERROR → S10 으로
            # 처리한다(장치 상태표, D-46). 동작 중이 아니므로 안전정지(S11)로 보내지 않는다
            self.init.on_error(msg)
            return
        if sev >= 3:                       # 위험 → 안전정지
            if not self.fsm.fire("STOP"):
                self.fsm.force("S11", reason="SAFETY_STOP:" + str(msg.get("code")))
        elif sev == 2:                     # 경고 → 오류 화면
            self.fsm.fire("ERROR")

    # ── 레이더 ──────────────────────────────────────────────
    def radar_snapshot(self):
        """UI 로 보내는 레이더 상태. 측정 못 했거나 오래된 값은 키를 빼고 보낸다."""
        if self.radar is None:
            return {"type": "radar", "enabled": False}
        out = self.radar.snapshot(CONFIG.get("radar_floor_mm"))
        out["type"] = "radar"
        return out

    def current_eapd(self):
        """탐색에 쓸 EAPD(mm)와 출처. 레이더 값이 먼저이고, 없으면 시뮬레이션 설정의 sim_eapd_mm.
        둘 다 없으면 (None, None) → 탐색은 EAPD_MISSING 으로 시작하지 않는다(대체 상수 없음)."""
        if self.radar is not None:
            e = self.radar.snapshot(CONFIG.get("radar_floor_mm")).get("eapd_mm")
            if e is not None:
                return e, "radar"
        sim = CONFIG.get("sim_eapd_mm")
        if sim is not None:
            return sim, "sim"
        return None, None

    def _unit_eapd(self):
        """구동 시연이 탐색을 시작하는 순간의 EAPD. X_SEARCH 의 탐색 시작과 같은 출처 순서로 정하고 기록한다."""
        self.eapd_mm, src = self.current_eapd()
        self.eapd_sim = src == "sim"
        self.log.event("sys", "EAPD:%s:%s" % (src, self.eapd_mm))
        return self.eapd_mm

    def config_msg(self):
        """UI 로 보내는 설정. 비어 있는 목표값(0, None)은 null 로 보낸다(화면은 '미설정'으로 표시)."""
        def val(k):
            v = CONFIG.get(k)
            return v if v else None
        return {"type": "config", "mode": "unit_demo" if self.unit_demo else "operate",
                "aed_pause_warn_s": CONFIG.get("aed_pause_warn_s"),
                "target_bpm": val("target_bpm"), "target_depth_mm": val("target_depth_mm"),
                "force_max_N": val("force_max_N"), "vacuum_target_kPa": val("vacuum_target_kPa")}

    def unit_msg(self, msg):
        """UI 의 유닛 요청(결선 시연 모드). 거부하면 UI 에 보낼 응답을, 받아들이면 None 을 돌려준다."""
        unit, act = msg.get("unit"), msg.get("action")
        self.log.event("ui", "UNIT_%s:%s" % (str(act).upper(), unit))
        if not self.unit_demo:
            return {"type": "rejected", "name": "UNIT", "reason": "not_unit_demo"}
        ok, why = self.units.request(unit, act)
        if ok:
            return None
        return {"type": "rejected", "name": "UNIT", "unit": unit, "action": act, "reason": why}

    def _on_radar_link(self, connected, port):
        self.log.event("sys", "RADAR_UP" if connected else "RADAR_DOWN")
        self.push(self.radar_snapshot())

    def tick_radar(self):
        """RADAR_PUSH_S 마다 UI 로 보내고, 수신 중이면 기록한다."""
        if self.radar is None or time.monotonic() - self._radar_at < self.RADAR_PUSH_S:
            return
        self._radar_at = time.monotonic()
        snap = self.radar_snapshot()
        self.push(snap)
        if snap.get("receiving"):
            self.log.radar(snap)

    def log_link(self):
        """링크 품질 누적 횟수를 세션 기록에 한 줄 남긴다(10/11 모터 없이, 10/12 모터 구동 비교용)."""
        self._link_logged_at = time.monotonic()
        self.log.link(self.link.link_stats())

    def link_msg(self):
        """UI 로 보내는 링크 상태. fw_ok: None=펌웨어 판 확인 전, True=v1.1, False=판 불일치."""
        up = bool(self.link.connected)
        m = {"type": "link", "connected": up, "port": self._link_port,
             "proto": self.link.proto if up else None, "fw_ok": self.link.fw_ok if up else None}
        fw = (self.link.info or {}).get("fw")
        if fw:
            m["fw"] = fw
        return m

    def _on_proto(self, proto, info):
        self.log.event("sys", "FW:%s:%s" % (proto, (info or {}).get("fw", "")))
        self.push(self.link_msg())

    def _on_link(self, connected, port):
        self.log.event("sys", "LINK_UP" if connected else "LINK_DOWN")
        if not connected:
            self.log_link()
        self._link_port = port
        self.push(dict(self.link_msg(), connected=connected, port=port))
        if self.unit_demo:
            running = self.units.unit == "compress" and self.units.phase == "running"
            self.units.on_link(connected)
            if not connected and running:
                self.push({"type": "error", "code": "3-CM-010", "sev": 2,
                           "why": "COMM_LOST", "desc": P.ERR["3-CM-010"]})
            return
        self.init.on_link(connected)
        if not connected and self.fsm.is_running():
            # 압박 중 두절: Teensy 가 T_comm 까지 유지한다. UI 에는 경고만.
            self.push({"type": "error", "code": "3-CM-010", "sev": 2,
                       "why": "COMM_LOST", "desc": P.ERR["3-CM-010"]})

    # ── UI 연결 ─────────────────────────────────────────────
    async def handler(self, ws, path=None):
        self.clients.add(ws)
        if not self.unit_demo:                  # 시연 모드는 상태머신 흐름을 돌리지 않는다(UI 전체 흐름은 모형)
            await ws.send(json.dumps(dict(self.fsm.snapshot(), type="state", event="SYNC")))
        await ws.send(json.dumps(self.link_msg()))
        await ws.send(json.dumps(self.config_msg()))
        await ws.send(json.dumps(self.radar_snapshot(), ensure_ascii=False))
        if self.unit_demo:
            await ws.send(json.dumps(self.units.snapshot()))
        try:
            async for raw in ws:
                try:
                    msg = json.loads(raw)
                except ValueError:
                    continue
                t = msg.get("type")
                name = msg.get("name", "")
                if t in ("intent", "event") and name in DEVICE_OWNED and self.link.connected:
                    await ws.send(json.dumps({"type": "rejected", "name": name, "node": self.fsm.node,
                                              "allowed": self.fsm.allowed(), "reason": "device_owned"}))
                elif t in ("intent", "event") and name and self.link.connected and hands_on_blocked(self.fsm, name):
                    self.log.event("ui", name + "_BLOCKED")
                    await ws.send(json.dumps({"type": "rejected", "name": name, "node": self.fsm.node,
                                              "allowed": self.fsm.allowed(),
                                              "reason": hands_on_block_reason(self.fsm, name)}))
                elif t in ("intent", "event") and name:
                    ok = self.fsm.fire(name, msg.get("payload"))
                    self.log.event("ui", name)
                    if not ok:
                        r = {"type": "rejected", "name": name, "node": self.fsm.node,
                             "allowed": self.fsm.allowed()}
                        if self.fsm.blocked_reason:
                            b = self.fsm.blocked_reason
                            r["reason"] = "device_not_ready"
                            r["mcu"] = b["mcu"]
                            r["need"] = b["need"]
                        await ws.send(json.dumps(r))
                elif t == "unit":
                    r = self.unit_msg(msg)
                    if r:
                        await ws.send(json.dumps(r))
                elif t == "search" and self.unit_demo:
                    # 시연 모드의 탐색은 구동 유닛 요청(unit drive)으로만 시작한다
                    await ws.send(json.dumps({"type": "rejected", "name": "SEARCH_START",
                                              "reason": "unit_demo"}))
                elif t == "search":
                    act = msg.get("action")
                    self.log.event("ui", "SEARCH_" + str(act).upper())
                    if act == "start" and self.fsm.node == "X_SEARCH":
                        # 시작하는 순간의 EAPD: 레이더(radar_floor_mm − 거리) → 없으면 시뮬레이션 설정값
                        self.eapd_mm, src = self.current_eapd()
                        self.eapd_sim = src == "sim"
                        self.log.event("sys", "EAPD:%s:%s" % (src, self.eapd_mm))
                        self.search.start(self.eapd_mm)
                    elif act == "stop":
                        self.search.cancel("USER_STOP")
                    else:
                        await ws.send(json.dumps({"type": "rejected", "name": "SEARCH_START",
                                                  "node": self.fsm.node, "reason": "wrong_node"}))
                elif t == "force" and msg.get("node"):       # 시연 패널 전용
                    self.fsm.force(msg["node"])
                elif t == "cmd" and msg.get("cmd") in P.CMD:  # 진단용 직접 명령
                    self.link.send(msg["cmd"], **(msg.get("args") or {}))
        except Exception:
            pass
        finally:
            self.clients.discard(ws)
            if self.unit_demo and not self.clients:
                self.log.event("sys", "UI_GONE")
                self.units.ui_gone()            # 화면이 모두 닫히면 진행 중인 시연 동작을 멈춘다

    # ── 주기 작업 ───────────────────────────────────────────
    async def ticker(self):
        while True:
            await asyncio.sleep(0.1)
            if self.unit_demo:
                self.units.tick()
            else:
                ev = self.fsm.auto_due()
                if ev:
                    self.fsm.fire(ev)
                self.init.tick()
            self.search.tick()
            if self.link.connected and self.link.is_stale() and self.link.last_tel_at:
                self.push({"type": "telemetry", "stale": True})
            if time.monotonic() - self._link_logged_at >= self.LINK_LOG_S:
                self.log_link()
            self.tick_radar()

    async def main(self):
        self.loop = asyncio.get_event_loop()
        self.link.start()
        if self.radar is not None:
            self.radar.start()
            print("[radar] %s" % self.radar.port)
        if self.unit_demo:
            # 상태머신 흐름(S1 자동 초기화: ARM, HOME)을 시작하지 않는다. 장치는 유닛 화면의 [시작]으로만 움직인다
            self.log.event("sys", "MODE_UNIT_DEMO")
            print("[mode] 결선 시연 모드(유닛별). 상태머신 흐름 없음, 장치는 유닛 화면의 [시작]으로만 움직임")
        else:
            self.fsm.enter_initial()
        print("[ws]  ws://0.0.0.0:%d" % self.args.port)
        if not self.unit_demo:
            print("[fsm] %s · %s" % (self.fsm.node, self.fsm.cur.get("name")))
        print("[log] %s" % self.log.path)
        server = websockets.serve(self.handler, "0.0.0.0", self.args.port)
        await server
        await self.ticker()


def static_handler(directory, legacy=None):
    """UI 정적 파일 처리기.

    Python 3.7 이상은 directory 인자를 쓴다. 3.6(젯슨 나노)에는 이 인자가 없어
    partial(directory=) 이 요청마다 TypeError 를 내고, chdir 은 상대 경로(--logdir 등)를
    바꾸므로, 3.6 에서는 경로 변환만 directory 기준으로 바꾼다."""
    from http.server import SimpleHTTPRequestHandler
    root = os.path.abspath(directory)
    if legacy is None:
        legacy = sys.version_info < (3, 7)
    if not legacy:
        return functools.partial(SimpleHTTPRequestHandler, directory=root)

    class Handler(SimpleHTTPRequestHandler):
        def translate_path(self, path):
            # 기본 처리(.. 제거)를 거친 현재 폴더 기준 경로를 root 기준으로 옮긴다
            p = SimpleHTTPRequestHandler.translate_path(self, path)
            return os.path.join(root, os.path.relpath(p, os.getcwd()))
    return Handler


def serve_static(directory, port):
    from http.server import HTTPServer
    srv = HTTPServer(("0.0.0.0", port), static_handler(directory))
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    print("[http] %s → http://0.0.0.0:%d" % (directory, port))


def main():
    ap = argparse.ArgumentParser(description="SAVER Jetson 브리지")
    ap.add_argument("--port", type=int, default=8765)
    ap.add_argument("--serial", default=None, help="/dev/ttyACM0 또는 /tmp/ttyFAKE")
    ap.add_argument("--serve", default=None, help="UI dist 디렉터리")
    ap.add_argument("--http-port", type=int, default=8080)
    ap.add_argument("--logdir", default="logs")
    ap.add_argument("--spec", default=None, help="state_machine_v2.json 경로")
    ap.add_argument("--config", default=None, help="설정 파일 (config.sim.json 등)")
    ap.add_argument("--radar", default=None,
                    help="MR60BHA2 레이더 포트(/dev/ttyTHS1 등). 없으면 레이더를 쓰지 않는다")
    ap.add_argument("--radar-baud", type=int, default=115200)
    ap.add_argument("--unit-demo", action="store_true",
                    help="결선 시연 모드(유닛별 시연, D-51): 상태머신 흐름을 돌리지 않고 유닛 화면의 시작·정지만 보낸다")
    a = ap.parse_args()

    load_config(a.config)
    warn_unset()
    if a.serve:
        serve_static(a.serve, a.http_port)

    b = Bridge(a)
    try:
        if sys.version_info >= (3, 7):
            # 실행 중인 루프 없이 get_event_loop() 를 부르면 3.12~3.13 은 경고, 3.14 부터는 RuntimeError.
            # 노트북 대체 실행(새 Python)에서도 뜨도록 루프를 직접 만든다. 3.6(젯슨) 경로는 그대로 둔다
            loop = asyncio.new_event_loop()
            asyncio.set_event_loop(loop)
            loop.run_until_complete(b.main())
            loop.run_forever()
        else:
            loop = asyncio.get_event_loop()
            loop.run_until_complete(b.main())
    except KeyboardInterrupt:
        print("\n종료")
    finally:
        b.link.stop()
        if b.radar is not None:
            b.radar.stop()
        b.log_link()
        b.log.close()


if __name__ == "__main__":
    main()
