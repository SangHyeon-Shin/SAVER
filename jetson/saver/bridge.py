#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""bridge.py — SAVER Jetson 통합 진입점

    React UI  ←ws://0.0.0.0:8765→  bridge  ←USB Serial→  Teensy 4.1

이 프로세스가 상태머신의 주인입니다. UI 는 화면일 뿐입니다.
  UI → {"type":"intent","name":"CPR_START"}      전이 요청
  UI ← {"type":"state", ...}                     확정된 상태 (UI 는 그대로 그림)
  UI ← {"type":"telemetry", ...}                 실측값 (stale 이면 stale:true)
  UI ← {"type":"error", ...}                     오류

실행
  python3 -m saver.bridge                                  # 시리얼 없이 (UI 시험)
  python3 -m saver.bridge --serial /tmp/ttyFAKE            # 가짜 Teensy 연결
  python3 -m saver.bridge --serial /dev/ttyACM0 --serve dist --http-port 8080
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
else:
    from . import protocol as P
    from .state_machine import StateMachine
    from .teensy_serial import TeensyLink
    from .logger import CsvLogger


# ─────────────────────────────────────────────────────────────
# ⚠ 미확정 설정값 (명세 §11). 0 이면 해당 파라미터를 보내지 않고,
#   Teensy 는 목표 깊이가 없으면 START_CPR 을 거부합니다.
#   추측으로 채우지 말고 확정된 근거가 나온 뒤에만 넣으세요.
CONFIG = {
    "target_bpm": 110,            # 확정
    "target_depth_mm": 0,         # TODO 한성윤 (가이드라인 근거)
    "force_max_N": 0,             # TODO 한성윤 / 서정준
    "vacuum_target_kPa": 0,       # TODO 서정준 + 한건민
}


def load_config(path):
    """설정 파일로 미확정 값을 채웁니다.
    config.sim.json 은 시뮬레이션 전용이며 의학적 근거가 없습니다."""
    if not path:
        return
    with open(path) as f:
        data = json.load(f)
    sim = bool(data.pop("_simulation_only", False))
    CONFIG.update({k: v for k, v in data.items() if not k.startswith("_")})
    if sim:
        print("=" * 62)
        print(" ⚠ 시뮬레이션 설정을 불러왔습니다 (%s)" % path)
        print("   이 값들은 의학적 근거가 없습니다. 실제 장비 구동에 쓰지 마세요.")
        print("=" * 62)


def warn_unset():
    miss = [k for k, v in CONFIG.items() if not v]
    if miss:
        print("[주의] 미확정 설정값: %s" % ", ".join(miss))
        print("       확정 전까지 압박 개시(START_CPR)는 Teensy가 거부합니다. 정상입니다.")


class Bridge(object):

    def __init__(self, args):
        self.args = args
        self.clients = set()
        self.loop = None
        self.log = CsvLogger(args.logdir)

        # 탐색으로 압박점 위로 이동을 마쳤는지. 3단계 탐색 모듈이 켠다.
        # 꺼져 있으면 SET_CP_POINT 를 보내지 않는다(압박점을 임의로 정하지 않는다).
        self.cp_point_ready = False
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
                               logger=self.log)

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

    def _on_command(self, cmd):
        """상태 진입 시 정의된 Teensy 명령 (state_machine_v2.json 의 enter_cmds)"""
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
        out = dict(msg)
        out["type"] = "telemetry"
        out["stale"] = False
        self.push(out)

    def _on_evt(self, msg):
        name = msg.get("evt", "")
        self.log.event("mcu", name)
        # 장비가 UI 상태를 바꿔야 하는 경우만 상태머신에 주입
        mapping = {"VACUUM_OK": "VACUUM_OK", "VACUUM_FAIL": "VACUUM_ERROR"}
        if name in mapping:
            self.fsm.fire(mapping[name])
        out = dict(msg)
        out["type"] = "device_event"
        self.push(out)

    def _on_err(self, msg):
        self.log.error(msg)
        sev = msg.get("sev") or P.severity(msg.get("code"))
        out = {"type": "error", "code": msg.get("code", ""), "sev": sev,
               "why": msg.get("why", ""), "latched": bool(msg.get("latched")),
               "desc": P.ERR.get(msg.get("code", ""), "")}
        self.push(out)
        if sev >= 3:                       # 위험 → 안전정지
            if not self.fsm.fire("STOP"):
                self.fsm.force("S11", reason="SAFETY_STOP:" + str(msg.get("code")))
        elif sev == 2:                     # 경고 → 오류 화면
            self.fsm.fire("ERROR")

    def _on_link(self, connected, port):
        self.log.event("sys", "LINK_UP" if connected else "LINK_DOWN")
        self.push({"type": "link", "connected": connected, "port": port})
        if not connected and self.fsm.is_running():
            # 압박 중 두절: Teensy 가 T_comm 까지 유지한다. UI 에는 경고만.
            self.push({"type": "error", "code": "3-CM-010", "sev": 2,
                       "why": "COMM_LOST", "desc": P.ERR["3-CM-010"]})

    # ── UI 연결 ─────────────────────────────────────────────
    async def handler(self, ws, path=None):
        self.clients.add(ws)
        await ws.send(json.dumps(dict(self.fsm.snapshot(), type="state", event="SYNC")))
        await ws.send(json.dumps({"type": "link", "connected": self.link.connected}))
        try:
            async for raw in ws:
                try:
                    msg = json.loads(raw)
                except ValueError:
                    continue
                t = msg.get("type")
                name = msg.get("name", "")
                if t in ("intent", "event") and name:
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
                elif t == "force" and msg.get("node"):       # 시연 패널 전용
                    self.fsm.force(msg["node"])
                elif t == "cmd" and msg.get("cmd") in P.CMD:  # 진단용 직접 명령
                    self.link.send(msg["cmd"], **(msg.get("args") or {}))
        except Exception:
            pass
        finally:
            self.clients.discard(ws)

    # ── 주기 작업 ───────────────────────────────────────────
    async def ticker(self):
        while True:
            await asyncio.sleep(0.1)
            ev = self.fsm.auto_due()
            if ev:
                self.fsm.fire(ev)
            if self.link.connected and self.link.is_stale() and self.link.last_tel_at:
                self.push({"type": "telemetry", "stale": True})

    async def main(self):
        self.loop = asyncio.get_event_loop()
        self.link.start()
        self.fsm.enter_initial()
        print("[ws]  ws://0.0.0.0:%d" % self.args.port)
        print("[fsm] %s · %s" % (self.fsm.node, self.fsm.cur.get("name")))
        print("[log] %s" % self.log.path)
        server = websockets.serve(self.handler, "0.0.0.0", self.args.port)
        await server
        await self.ticker()


def serve_static(directory, port):
    from http.server import SimpleHTTPRequestHandler, HTTPServer
    try:
        h = functools.partial(SimpleHTTPRequestHandler, directory=directory)
    except TypeError:                      # Python 3.6 은 directory 인자 없음
        os.chdir(directory)
        h = SimpleHTTPRequestHandler
    srv = HTTPServer(("0.0.0.0", port), h)
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
    a = ap.parse_args()

    load_config(a.config)
    warn_unset()
    if a.serve:
        serve_static(a.serve, a.http_port)

    b = Bridge(a)
    try:
        if sys.version_info >= (3, 7):
            asyncio.get_event_loop().run_until_complete(b.main())
            asyncio.get_event_loop().run_forever()
        else:
            loop = asyncio.get_event_loop()
            loop.run_until_complete(b.main())
    except KeyboardInterrupt:
        print("\n종료")
    finally:
        b.link.stop()
        b.log.close()


if __name__ == "__main__":
    main()
