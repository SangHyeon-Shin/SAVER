#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""SAVER Jetson 자동시험 — 하드웨어 없이 전부 통과해야 합니다."""
import os, sys, time, json, random, subprocess, threading
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from saver import protocol as P
from saver.state_machine import StateMachine
from saver.teensy_serial import TeensyLink

P_ = {"pass": 0, "fail": 0}
def ck(name, ok, detail=""):
    print(("  OK   " if ok else "  FAIL ") + name + (("  <%s>" % detail) if detail else ""))
    P_["pass" if ok else "fail"] += 1

print("\n== 1. 프로토콜 ==")
ck("encode/decode 왕복", P.decode(P.encode({"t":"cmd","seq":1,"cmd":"PING"}))["cmd"] == "PING")
ck("깨진 JSON → None", P.decode(b'{"broken') is None)
ck("빈 줄 → None", P.decode(b"\n") is None)
ck("512 byte 초과 거부", P.decode(b"x"*600) is None)
try:
    P.encode({"t":"cmd","seq":1,"cmd":"X","pad":"y"*600}); ck("송신 길이 초과 예외", False)
except P.ProtocolError: ck("송신 길이 초과 예외", True)
ck("심각도 추출", P.severity("3-CU-021") == 3 and P.severity("bad") == 1)
s = P.SeqCounter(65534); ck("seq 순환", [s.next(), s.next(), s.next()] == [65534, 65535, 1])

print("\n== 2. 상태머신 ==")
fired = []
fsm = StateMachine(on_command=lambda c: fired.append(c))
path = ["INIT_DONE","WHEELS_DONE","MOVE_DONE","START_PRESSED","CLOTHES_OK",
        "POSITION_OK","PPG_ATTACHED","SEARCH_DONE","PIN_RELEASED","VACUUM_OK","CPR_START"]
ok = all(fsm.fire(e) for e in path)
ck("정상 흐름 S1→S7", ok and fsm.node == "S7", fsm.node)
ck("S7 running=True", fsm.is_running())
ck("진입 명령 발행", "START_CPR" in fired and "VACUUM_ON" in fired, ",".join(fired))
seen = [l["to"] for l in fsm.log]
ck("탐색 → 핀 해제 → 고정 → 압박 준비 순서 (D-11)",
   seen.index("X_SEARCH") < seen.index("X_PIN") < seen.index("S5") < seen.index("S6"), "→".join(seen))
ck("탐색 노드는 Teensy 명령을 보내지 않음 (STIFFNESS_SCAN 미사용)", "STIFFNESS_SCAN" not in fired)
ck("고정 뒤 압박 접촉 영점, 목표 설정",
   fired.index("VACUUM_ON") < fired.index("SET_CP_POINT") < fired.index("SET_TARGET") < fired.index("START_CPR"),
   ",".join(fired))
ck("허용 안 된 이벤트 무시", fsm.fire("RELEASE_DONE") is False and fsm.node == "S7")
fsm.fire("ROSC_SUSPECT"); ck("ROSC 분기", fsm.node == "S9")
fsm.fire("RESUME_CPR");  ck("압박 재개", fsm.node == "S7")

f2 = StateMachine(); f2.force("S5")
f2.fire("VACUUM_ERROR")
ck("오류 진입", f2.node == "S10" and f2.error_cause == "vacuum")
f2.fire("RECHECK"); ck("RECHECK 복귀 S10→S5", f2.node == "S5")

# 장치 상태 조건 (requires_mcu)
mcu = {"s": "ARMED"}
f3 = StateMachine(mcu_state=lambda: mcu["s"]); f3.force("X_PPG")
ck("HOMED 아니면 탐색 진입 거부", f3.fire("PPG_ATTACHED") is False and f3.node == "X_PPG"
   and f3.blocked_reason["need"] == ["HOMED"], str(f3.blocked_reason))
mcu["s"] = "HOMED"
ck("HOMED 면 탐색 진입", f3.fire("PPG_ATTACHED") and f3.node == "X_SEARCH")
ck("탐색 완료 → 핀 해제", f3.fire("SEARCH_DONE") and f3.node == "X_PIN")
ck("핀 해제 → 음압 고정", f3.fire("PIN_RELEASED") and f3.node == "S5")
ck("음압 고정 전(HOMED)에는 압박 준비 거부", f3.fire("VACUUM_OK") is False and f3.node == "S5")
mcu["s"] = "VACUUM_HOLD"
ck("VACUUM_HOLD 면 압박 준비 진입", f3.fire("VACUUM_OK") and f3.node == "S6")
ck("CPR_READY 전에는 압박 시작 거부", f3.fire("CPR_START") is False and f3.node == "S6")
mcu["s"] = "CPR_READY"
ck("CPR_READY 면 압박 시작", f3.fire("CPR_START") and f3.node == "S7")

f5 = StateMachine(mcu_state=lambda: mcu["s"]); mcu["s"] = "HOMED"; f5.force("X_SEARCH")
f5.fire("SEARCH_FAIL")
ck("탐색 실패 → 오류", f5.node == "S10" and f5.error_return == "X_SEARCH", f5.node)
mcu["s"] = "FAULT"
ck("복귀 대상의 장치 조건 검사(@return, FAULT면 탐색 복귀 거부)",
   f5.fire("RECHECK") is False and f5.node == "S10", str(f5.blocked_reason))
mcu["s"] = "HOMED"
ck("HOMED 복구 뒤 탐색 복귀", f5.fire("RECHECK") and f5.node == "X_SEARCH")

# S13 조건: 음압이 남아 있으면 S2 대신 S12 (v1.1 합의 12번)
flags = {"v": None}
f4 = StateMachine(mcu_flags=lambda: flags["v"]); f4.force("S13")
ck("플래그 미지원(v1.0)이면 S13 → S2", f4.fire("BACK_TO_IDLE") and f4.node == "S2")
f4.force("S13"); flags["v"] = {"rearm_lock": True, "vac_released": False}
ck("rearm_lock 이면 S13 → S12", f4.fire("BACK_TO_IDLE") and f4.node == "S12" and f4.log[-1].get("redirect"))
ck("S12 해제 뒤 S13", f4.fire("RELEASE_DONE") and f4.node == "S13")
flags["v"] = {"rearm_lock": False, "vac_released": True}
ck("해제 확인되면 S13 → S2", f4.fire("BACK_TO_IDLE") and f4.node == "S2")
for desc, fv in (("vac_released 없음", {"rearm_lock": False}),
                 ("rearm_lock 값 없음(오래됨)", {"rearm_lock": None, "vac_released": True}),
                 ("vac_released false", {"rearm_lock": False, "vac_released": False})):
    f4.force("S13"); flags["v"] = fv
    ck("%s → S12" % desc, f4.fire("BACK_TO_IDLE") and f4.node == "S12", f4.node)
f4.force("S13"); flags["v"] = {"rearm_lock": True}
ck("전원 끄기는 redirect 대상 아님", f4.fire("POWER_OFF") and f4.node == "S0")

spec = json.load(open(StateMachine().spec and os.path.join(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "..", "protocol", "state_machine_v2.json")))
bad = [(n, e, t) for n, d in spec["nodes"].items()
       for e, t in d.get("on", {}).items() if t != "@return" and t not in spec["nodes"]]
ck("노드 참조 무결성", not bad, str(bad[:2]))
unknown = [(n, e) for n, d in spec["nodes"].items()
           for e in d.get("on", {}) if e not in spec["events"]]
ck("이벤트 정의 무결성", not unknown, str(unknown[:2]))
bad_rd = [(n, e) for n, d in spec["nodes"].items() for e, r in d.get("redirect", {}).items()
          if e not in d.get("on", {}) or r.get("to") not in spec["nodes"]]
ck("redirect 참조 무결성", not bad_rd, str(bad_rd[:2]))
bad_rq = [(n, m) for n, d in spec["nodes"].items() for m in d.get("requires_mcu", []) if m not in P.MCU_STATES]
ck("requires_mcu 가 Teensy 내부 상태인지", not bad_rq, str(bad_rq[:2]))
cmds = set(c for d in spec["nodes"].values() for c in d.get("enter_cmds", []))
ck("enter_cmds 가 명세 명령인지", cmds <= set(P.CMD), str(sorted(cmds - set(P.CMD))))

print("\n== 3. 가짜 Teensy 실물 연결 ==")
sim = subprocess.Popen([sys.executable, "-u",
    os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "simulators", "fake_teensy.py"),
    "--port", "/tmp/ttyFAKE_TEST", "--hz", "20", "--seed", "1", "--quiet"],
    stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
time.sleep(1.2)
ck("가상 포트 생성", os.path.exists("/tmp/ttyFAKE_TEST"))

tel, evts, errs, links = [], [], [], []
link = TeensyLink(port="/tmp/ttyFAKE_TEST",
                  on_telemetry=tel.append, on_event=evts.append,
                  on_error=errs.append, on_link=lambda c, p: links.append(c)).start()
time.sleep(1.0)
ck("링크 연결", link.connected)
ck("텔레메트리 수신", len(tel) > 5, "%d건" % len(tel))
ck("stale 아님", not link.is_stale())
ck("MCU 상태 수신", link.mcu_state == "IDLE", str(link.mcu_state))

link.send("ARM"); time.sleep(0.4)
ck("ARM 반영", link.mcu_state == "ARMED", str(link.mcu_state))
link.send("HOME"); time.sleep(1.3)
ck("HOMING_DONE 이벤트", any(e.get("evt") == "HOMING_DONE" for e in evts))
link.send("VACUUM_ON"); time.sleep(1.8)
ck("VACUUM_OK 이벤트", any(e.get("evt") == "VACUUM_OK" for e in evts))

n0 = len(errs)
link.send("START_CPR"); time.sleep(0.4)
ck("잘못된 상태 명령 거부", len(errs) > n0 and errs[-1]["code"] == "2-CU-011", str(errs[-1] if errs else ""))

link.send("STIFFNESS_SCAN"); time.sleep(2.0)
scan = [e for e in evts if e.get("evt") == "SCAN_RESULT"]
ck("강성 스캔 결과", bool(scan) and len(scan[0].get("stiff", [])) == 8)
link.send("SET_CP_POINT", idx=scan[0]["best_idx"] if scan else 0); time.sleep(0.4)
n1 = len(errs)
link.send("START_CPR"); time.sleep(0.4)
ck("목표 미설정 시 구동 거부", len(errs) > n1 and errs[-1]["code"] == "2-CU-012", str(errs[-1]["code"] if errs else ""))
link.send("SET_TARGET", bpm=110, depth_mm=50.0); time.sleep(0.3)
link.send("START_CPR"); time.sleep(0.6)
ck("압박 개시", link.mcu_state == "CPR_RUNNING", str(link.mcu_state))
last = tel[-1]
ck("압박 데이터 수신", "cp_depth_mm" in last and last["cp_depth_mm"] > 40, str(last.get("cp_depth_mm")))
ck("음압 음수 부호", last.get("vac_kPa", 0) < 0, str(last.get("vac_kPa")))

print("\n== 4. 고장 주입 ==")
sim.stdin.write(b"oc\n"); sim.stdin.flush(); time.sleep(0.6)
oc = [e for e in errs if e.get("code") == "3-CU-021"]
ck("과전류 보고", bool(oc) and oc[-1]["sev"] == 3)
ck("FAULT 상태 전이", link.mcu_state == "FAULT", str(link.mcu_state))
n2 = len(errs); link.send("RESET_FAULT"); time.sleep(0.4)
ck("원인 미해소 시 RESET 거부", len(errs) > n2 and errs[-1]["why"] == "FAULT_PERSISTS")

sim.stdin.write(b"ok\n"); sim.stdin.flush(); time.sleep(0.4)
sim.stdin.write(b"drop\n"); sim.stdin.flush(); time.sleep(1.2)
ck("송신 중단 → stale 판정", link.is_stale())
n3 = len(errs); time.sleep(0.8)
ck("ACK 미수신 → 2-CM-002", any(e.get("code") == "2-CM-002" for e in errs[n3:]))
sim.stdin.write(b"ok\n"); sim.stdin.flush(); time.sleep(0.8)
ck("복구 후 재수신", not link.is_stale())

print("\n== 5. 재연결 ==")
sim.terminate(); sim.wait(); time.sleep(1.0)
ck("장치 소멸 감지", (not link.connected) or link.is_stale())
sim2 = subprocess.Popen([sys.executable, "-u",
    os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "simulators", "fake_teensy.py"),
    "--port", "/tmp/ttyFAKE_TEST", "--hz", "20", "--quiet"],
    stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
time.sleep(3.0)
ck("자동 재연결", link.connected and not link.is_stale())
link.stop(); sim2.terminate(); sim2.wait()
try: os.remove("/tmp/ttyFAKE_TEST")
except OSError: pass

print("\n== 6. Jetson 재시작, Teensy 전원 유지 ==")
# Teensy 를 별도 5 V 로 켜면(VIN, VUSB 분리) Jetson 재시작이 Teensy 를 리셋하지 않는다.
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "simulators"))
from fake_teensy import FakeTeensy

def session(ft, start, cmds):
    sc = P.SeqCounter(start)
    for c in cmds:
        ft.handle({"v": 1, "t": "cmd", "seq": sc.next(), "cmd": c})

ft = FakeTeensy(tel_hz=20, seed=1)
session(ft, 1, ["GET_STATUS", "PING", "ARM"])
session(ft, 1, ["GET_STATUS", "PING", "DISARM"])
ck("같은 seq 재사용 시 실행 안 됨 (명세 §4.1, 시작 seq 고정의 위험)", ft.state == "ARMED", ft.state)
ft2 = FakeTeensy(tel_hz=20, seed=1)
session(ft2, 1, ["GET_STATUS", "PING", "ARM"])
session(ft2, P.initial_seq(random.Random(7)), ["GET_STATUS", "PING", "DISARM"])
ck("무작위 시작 seq면 재시작 뒤 명령 실행", ft2.state == "IDLE", ft2.state)
for f in (ft, ft2):
    os.close(f.master); os.close(f.slave)
ck("시작 seq 범위 1~65535", all(1 <= P.initial_seq() <= 65535 for _ in range(2000)))
_orig = P.initial_seq
P.initial_seq = lambda rng=None: 4242
try:
    ck("TeensyLink 기본 시작 seq는 initial_seq 사용", TeensyLink(port="/nonexistent")._seq.next() == 4242)
finally:
    P.initial_seq = _orig
ck("seq_start 지정 시 그 값 사용", TeensyLink(port="/nonexistent", seq_start=10)._seq.next() == 10)

print("\n%s  (통과 %d / 실패 %d)\n" % ("전체 통과" if P_["fail"] == 0 else "실패 있음", P_["pass"], P_["fail"]))
sys.exit(0 if P_["fail"] == 0 else 1)
