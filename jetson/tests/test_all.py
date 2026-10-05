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
def _body(n):
    head, tail = '{"t":"tel","pad":"', '"}'
    return head + "x" * (n - len(head) - len(tail)) + tail
ck("본문 511 byte 수신 허용 (v1.1 §3.4)", len(_body(511)) == 511 and P.decode(_body(511).encode()) is not None)
ck("본문 512 byte 수신 거부", len(_body(512)) == 512 and P.decode(_body(512).encode()) is None)
a = P.AidCounter(P.AID_MAX - 1); ck("aid 순환 없음, 한도 뒤 None", [a.next(), a.next(), a.next()] == [P.AID_MAX - 1, P.AID_MAX, None])
ck("명령 분류: 조회 4, 정지 4", len(P.QUERY_CMDS) == 4 and len(P.STOP_CMDS) == 4 and set(P.QUERY_CMDS + P.STOP_CMDS) <= set(P.CMD))

print("\n== 2. 상태머신 ==")
fired = []
fsm = StateMachine(on_command=lambda c: fired.append(c))
path = ["INIT_DONE","WHEELS_DONE","MOVE_DONE","START_PRESSED","CLOTHES_OK","AED_PADS_ON",
        "POSITION_OK","PPG_ATTACHED","SEARCH_DONE","PIN_RELEASED","VACUUM_OK","CPR_START"]
ok = all(fsm.fire(e) for e in path)
ck("정상 흐름 S1→S7", ok and fsm.node == "S7", fsm.node)
ck("S7 running=True", fsm.is_running())
ck("진입 명령 발행", "START_CPR" in fired and "VACUUM_ON" in fired, ",".join(fired))
seen = [l["to"] for l in fsm.log]
ck("탐색 → 핀 해제 → 고정 → 압박 준비 순서 (D-11)",
   seen.index("X_SEARCH") < seen.index("X_PIN") < seen.index("S5") < seen.index("S6"), "→".join(seen))
ck("탐색 노드는 Teensy 명령을 보내지 않음 (STIFFNESS_SCAN 미사용)", "STIFFNESS_SCAN" not in fired)
ck("고정 뒤 목표 설정, 압박 접촉 영점 (v2.2: 영점 하강 중 BUSY 거부 방지)",
   fired.index("VACUUM_ON") < fired.index("SET_TARGET") < fired.index("SET_CP_POINT") < fired.index("START_CPR"),
   ",".join(fired))
ck("허용 안 된 이벤트 무시", fsm.fire("RELEASE_DONE") is False and fsm.node == "S7")
fsm.fire("ROSC_SUSPECT"); ck("ROSC 분기", fsm.node == "S9")
fsm.fire("RESUME_CPR");  ck("압박 재개", fsm.node == "S7")

# 인공호흡 안내 차단 (D-44, D-45): 장치 연결 중에는 X_VENT 에 PAUSE_CPR 이 있고,
# PAUSE_CPR 로 압박 모터가 실제로 멈추는 것을 확인(pause_stop_verified)한 뒤에만 들어간다
from saver import bridge as B
fv = StateMachine(); fv.force("S7")
ck("v2.3: X_VENT 진입 명령에 PAUSE_CPR", fv.nodes["X_VENT"].get("enter_cmds") == ["PAUSE_CPR"])
ck("인공호흡 안내: 압박 정지 실기 확인 전에는 차단(pause_unverified)",
   B.hands_on_block_reason(fv, "VENT_GUIDE", {"pause_stop_verified": False}) == "pause_unverified"
   and B.hands_on_blocked(fv, "VENT_GUIDE", {}) and B.CONFIG.get("pause_stop_verified") is False)
ck("인공호흡 안내: 확인 뒤에는 허용, 다른 이벤트는 검사 대상 아님",
   B.hands_on_block_reason(fv, "VENT_GUIDE", {"pause_stop_verified": True}) is None
   and not B.hands_on_blocked(fv, "PAUSE", {}) and not B.hands_on_blocked(fv, "AED_PAUSE", {}))
fv2 = StateMachine(); fv2.nodes = json.loads(json.dumps(fv2.nodes)); fv2.force("S7")
fv2.nodes["X_VENT"]["enter_cmds"] = []
ck("인공호흡 안내: 진입 명령에 PAUSE_CPR 이 없으면 확인값과 관계없이 차단(no_pause_cmd)",
   B.hands_on_block_reason(fv2, "VENT_GUIDE", {"pause_stop_verified": True}) == "no_pause_cmd")

# v2.3 (한건민 합의 10/5): 재개 명령 순서. 처음 S6→S7 은 START_CPR, 일시정지 노드에서 재개는
# 장치가 CPR_PAUSED 일 때만 받고 RESUME_CPR 를 보낸다
mcu_v = {"s": "CPR_READY"}
sent_v = []
fz = StateMachine(mcu_state=lambda: mcu_v["s"],
                  on_command=lambda c: sent_v.append(P.resolve_enter_cmd(c, mcu_v["s"])))
fz.force("S6")
ck("v2.3: 처음 S6→S7 은 START_CPR", fz.fire("CPR_START") and fz.node == "S7" and sent_v == ["START_CPR"], str(sent_v))
mcu_v["s"] = "CPR_RUNNING"
for ev_in, node_in, ev_back in (("VENT_GUIDE", "X_VENT", "VENT_DONE"), ("PAUSE", "S8", "RESUME"),
                                ("ROSC_SUSPECT", "S9", "RESUME_CPR"), ("AED_PAUSE", "X_AED", "AED_RESUME")):
    del sent_v[:]
    mcu_v["s"] = "CPR_RUNNING"; fz.force("S7")
    ok_in = fz.fire(ev_in) and fz.node == node_in and sent_v == ["PAUSE_CPR"]
    blocked = fz.fire(ev_back) is False and fz.node == node_in and fz.blocked_reason \
        and fz.blocked_reason.get("need") == ["CPR_PAUSED"]
    mcu_v["s"] = "CPR_PAUSED"
    ok_back = fz.fire(ev_back) and fz.node == "S7" and sent_v == ["PAUSE_CPR", "RESUME_CPR"]
    ck("v2.3: %s → PAUSE_CPR, CPR_PAUSED 확인 전 %s 거부, 확인 뒤 RESUME_CPR" % (node_in, ev_back),
       ok_in and blocked and ok_back, str(sent_v))
mcu_v["s"] = "CPR_RUNNING"; fz.force("X_AED")
ck("v2.3: 정지 방향(STOP)은 장치 상태와 관계없이 받음", fz.fire("STOP") and fz.node == "S11")
fo = StateMachine(); fo.force("X_POSOUT")
ck("v2.3: 위치 이탈은 정지만(REALIGN_DONE 없음)", fo.allowed() == ["STOP"] and "REALIGN_DONE" not in fo.events)
s1_cmds = []
fs1 = StateMachine(on_command=lambda c: s1_cmds.append(c)); fs1.force("S1")
del s1_cmds[:]
ck("v2.3: S1 오류 확인 뒤 S1 로 돌아가 초기 점검을 다시 함",
   fs1.fire("INIT_ERROR") and fs1.node == "S10" and fs1.fire("RECHECK") and fs1.node == "S1"
   and s1_cmds == ["PING", "GET_STATUS", "ARM", "HOME"], str(s1_cmds))

# AED 병행 (v2.2)
ck("겉옷 뒤 AED 패드 확인 → 정렬", "X_AED_PAD" in seen and seen.index("X_CLOTHES") < seen.index("X_AED_PAD") < seen.index("S3S4"))
fa = StateMachine(); fa.force("X_AED_PAD")
ck("AED 없음도 정렬로 진행", fa.fire("AED_NONE") and fa.node == "S3S4")
fired_a = []
fa = StateMachine(on_command=lambda c: fired_a.append(c)); fa.force("S7")
ck("압박 중 AED 버튼 → AED 화면, 압박 일시정지 명령", fa.fire("AED_PAUSE") and fa.node == "X_AED" and fired_a == ["PAUSE_CPR"])
ck("AED 화면은 자동 전이 없음", fa.auto_due() is None and fa.cur.get("auto") is None)
ck("AED 화면에서는 재개·정지만 (ROSC·오류·인공호흡 이벤트 무시)",
   fa.allowed() == ["AED_RESUME", "STOP"] and not fa.fire("ROSC_SUSPECT") and not fa.fire("VENT_GUIDE") and fa.node == "X_AED")
ck("구조자 재개 → 압박 화면", fa.fire("AED_RESUME") and fa.node == "S7")
fa.force("S8"); ck("일시정지 중에도 AED 화면으로", fa.fire("AED_PAUSE") and fa.node == "X_AED")
mcu_a = {"s": "CPR_PAUSED"}
fb = StateMachine(mcu_state=lambda: mcu_a["s"]); fb.force("X_AED")
ck("장치 CPR_PAUSED 에서 재개 허용 (v2.1 결함 수정)", fb.fire("AED_RESUME") and fb.node == "S7")
fb.force("S8"); ck("일시정지 재개도 허용", fb.fire("RESUME") and fb.node == "S7")
mcu_a["s"] = "HOMED"; fb.force("S6")
ck("장치 준비 전 압박 시작은 여전히 거부", fb.fire("CPR_START") is False and fb.node == "S6")
ck("진입 명령: 일시정지 뒤 START_CPR → RESUME_CPR", P.resolve_enter_cmd("START_CPR", "CPR_PAUSED") == "RESUME_CPR")
ck("진입 명령: 압박 중이면 START_CPR 안 보냄", P.resolve_enter_cmd("START_CPR", "CPR_RUNNING") is None)
ck("진입 명령: 처음 시작·불명은 그대로", P.resolve_enter_cmd("START_CPR", "CPR_READY") == "START_CPR"
   and P.resolve_enter_cmd("START_CPR", None) == "START_CPR")
ck("진입 명령: 정지 방향은 상태와 관계없이 그대로", all(P.resolve_enter_cmd("PAUSE_CPR", st_) == "PAUSE_CPR"
   for st_ in ("CPR_PAUSED", "CPR_RUNNING", None)))

f2 = StateMachine(); f2.force("S5")
f2.fire("VACUUM_ERROR")
ck("오류 진입", f2.node == "S10" and f2.error_cause == "vacuum")
f2.fire("RECHECK"); ck("RECHECK 복귀 S10→S5", f2.node == "S5")
f2.force("X_SEARCH"); f2.fire("SEARCH_FAIL", {"cause": "search"})
ck("탐색 실패 → S10, 원인 search, RECHECK 로 탐색 화면 복귀",
   f2.node == "S10" and f2.error_cause == "search" and f2.fire("RECHECK") and f2.node == "X_SEARCH")

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

# 장치 이벤트가 그 상태를 실은 tel 보다 먼저 오는 경우 (VACUUM_OK 직후 tel 은 아직 HOMED)
mcu5 = {"s": "HOMED"}
f5 = StateMachine(mcu_state=lambda: mcu5["s"]); f5.force("S5")
ck("장치 이벤트가 상태 조건에 막히면 보류", f5.fire_device("VACUUM_OK", 0.5) is False and f5.node == "S5")
ck("보류 중 tel 상태 그대로 → 전이 안 함", f5.retry_device() is None and f5.node == "S5")
mcu5["s"] = "VACUUM_HOLD"
ck("다음 tel 에서 상태 확인 → S6", f5.retry_device() == ("fired", "VACUUM_OK") and f5.node == "S6")
f5.force("S5"); mcu5["s"] = "HOMED"; f5.fire_device("VACUUM_OK", 0.0); time.sleep(0.01); mcu5["s"] = "VACUUM_HOLD"
ck("보류 마감 지나면 버림(오래된 이벤트로 전이 안 함)", f5.retry_device() == ("expired", "VACUUM_OK") and f5.node == "S5")
f5.force("S5"); mcu5["s"] = "HOMED"; f5.fire_device("VACUUM_OK", 0.5); f5.fire("CANCEL"); mcu5["s"] = "VACUUM_HOLD"
ck("보류 중 노드가 바뀌면 버림", f5.retry_device() == ("expired", "VACUUM_OK") and f5.node == "S2")

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
ck("v1.0 펌웨어 판별(GET_INFO 미지원)", link.proto == "1.0" and link.mcu_flags is None, str(link.proto))
ck("GET_INFO 거부를 오류로 보고하지 않음", not any(e.get("why") == "UNKNOWN_COMMAND" for e in errs))

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

print("\n== 7. 모의 Teensy v1.1 (주행, 제동, 헤드 높이, bid·aid) ==")
SIMDIR = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "simulators")
from fake_teensy_v11 import FakeTeensy11, Phantom
SIM = dict((k, v) for k, v in json.load(open(os.path.join(SIMDIR, "teensy_settings.sim.json"))).items()
           if not k.startswith("_"))
_opened = []

class V11(object):
    """FakeTeensy11 을 수동 완료 모드로 직접 구동한다. 출력은 out 에 쌓인다."""
    def __init__(self, settings=None, **kw):
        self.out = []
        self.ft = FakeTeensy11(seed=3, settings=SIM if settings is None else settings,
                               auto=False, outbox=self.out, boot_id=777, **kw)
        _opened.append(self.ft)
        self.seq, self.aid = 0, 0
    def run(self, m):
        n = len(self.out); self.ft.handle(m); return self.out[n:]
    def q(self, cmd, **kw):
        self.seq += 1
        return self.run(dict({"v": 1, "t": "cmd", "seq": self.seq, "cmd": cmd}, **kw))
    def c(self, cmd, aid=None, bid=777, seq=None, **kw):
        if aid is None:
            self.aid += 1; aid = self.aid
        if seq is None:
            self.seq += 1; seq = self.seq
        m = {"v": 1, "t": "cmd", "seq": seq, "cmd": cmd}
        if aid is not False: m["aid"] = aid
        if bid is not False: m["bid"] = bid
        m.update(kw)
        return self.run(m)
    def finish(self):
        n = len(self.out); self.ft.finish_job(); return self.out[n:]
    def tick(self, sec):
        n = len(self.out)
        for _ in range(int(sec / 0.05)):
            self.ft.tick(0.05)
        return self.out[n:]
    @property
    def mot(self):
        return self.ft.mot_msg()

def ack(msgs):
    a = [m for m in msgs if m.get("t") == "ack"]
    return a[0] if a else {}
def ev(msgs, name):
    e = [m for m in msgs if m.get("t") == "evt" and m.get("evt") == name]
    return e[0] if e else None

d = V11()
r = d.q("GET_INFO"); info = ev(r, "INFO")
ck("GET_INFO: INFO 이벤트, caps, boot_id", bool(info) and {"MOT", "AID", "DRIVE", "HEAD_HOLD"} <= set(info["caps"])
   and info["boot_id"] == 777 and "last_aid" not in info, str(info))
ck("bid·aid 없는 ARM → AID_REQUIRED, 기록 없음",
   ack(d.c("ARM", aid=False, bid=False)).get("why") == "AID_REQUIRED" and d.ft.aid_max == 0)
ck("다른 부팅의 bid → BOOT_MISMATCH", ack(d.c("ARM", bid=1)).get("why") == "BOOT_MISMATCH")
ck("부팅 직후 ARM → VACUUM_NOT_RELEASED (§4.6)", ack(d.c("ARM")).get("why") == "VACUUM_NOT_RELEASED"
   and d.ft.state == "IDLE")
d.tick(0.5)
ck("음압 해제 확인(vac_released) 성립", d.mot["vac_released"] is True and d.mot["rearm_lock"] is False)
arm_seq, arm_aid = d.seq + 1, d.aid + 1
ck("ARM 수락", ack(d.c("ARM")).get("ok") and d.ft.state == "ARMED")
d.ft.state = "IDLE"
ck("같은 seq·aid 재전송 → 원래 ACK, 재실행 안 함",
   ack(d.c("ARM", aid=arm_aid, seq=arm_seq)).get("ok") and d.ft.state == "IDLE")
d.ft.state = "ARMED"
ck("같은 aid 다른 명령 → SEQ_CONFLICT", ack(d.c("HOME", aid=arm_aid, seq=arm_seq)).get("why") == "SEQ_CONFLICT")
ck("이미 평가한 aid 보다 작은 aid → STALE_SEQ", ack(d.c("HOME", aid=1)).get("why") == "STALE_SEQ")
info2 = ev(d.q("GET_INFO"), "INFO")
ck("GET_INFO last_seq·last_aid (조회·기록 없는 거부는 바꾸지 않음)",
   info2.get("last_aid") == arm_aid and info2.get("last_seq") == arm_seq, str(info2))
ck("HOME 전 HEAD_HOLD → NOT_HOMED", ack(d.c("HEAD_HOLD", press_mm=10.0)).get("why") == "NOT_HOMED")
home_seq, home_aid = d.seq + 1, d.aid + 1
ck("HOME 수락, busy=HOMING", ack(d.c("HOME")).get("ok") and d.mot["busy"] == "HOMING"
   and d.mot.get("busy_aid") == home_aid)
ck("동작 중 다른 동작 → BUSY", ack(d.c("DRIVE_STEP", dist_mm=10.0)).get("why") == "BUSY")
r = d.finish()
ck("HOMING_DONE OK, HOMED", (ev(r, "HOMING_DONE") or {}).get("result") == "OK" and d.ft.state == "HOMED"
   and d.mot["busy"] == "NONE")
r = d.c("HOME", aid=home_aid, seq=home_seq)
ck("끝난 동작 재전송 → 원래 ACK와 저장된 완료 이벤트", ack(r).get("ok") and bool(ev(r, "HOMING_DONE"))
   and d.ft.head_pos == 0.0)
hold_aid = d.aid + 1
d.c("HEAD_HOLD", press_mm=10.0, drv_zero=True); r = d.finish(); hh = ev(r, "HEAD_HELD") or {}
ck("HEAD_HOLD → HEAD_HELD OK, 접촉점과 반력", hh.get("result") == "OK" and hh.get("aid") == hold_aid
   and abs(hh.get("head_contact_mm", 0) - 35.0) < 0.01 and hh.get("cp_force_N", 0) > 30, str(hh))
ck("mot: 헤드 유지, 주행 원점 0", d.mot["head_hold_active"] and d.mot.get("drv_pos_mm") == 0.0
   and d.mot.get("head_pos_mm") == 45.0, str(d.mot))
ck("유지 중 새 HEAD_HOLD → ALREADY_HOLDING", ack(d.c("HEAD_HOLD", press_mm=10.0)).get("why") == "ALREADY_HOLDING")
ck("1회 이동 상한 초과 → DIST_RANGE", ack(d.c("DRIVE_STEP", dist_mm=25.0)).get("why") == "DIST_RANGE")
d.c("DRIVE_STEP", dist_mm=10.0); dd = ev(d.finish(), "DRIVE_DONE") or {}
ck("DRIVE_STEP → DRIVE_DONE OK, 이동거리·위치·반력 평균", dd.get("result") == "OK" and dd.get("drive_mm") == 10.0
   and dd.get("drv_pos_mm") == 10.0 and "cp_force_N" in dd and dd.get("force_n") == 16, str(dd))
prof = [dd["cp_force_N"]]
for _ in range(7):
    d.c("DRIVE_STEP", dist_mm=10.0); prof.append((ev(d.finish(), "DRIVE_DONE") or {}).get("cp_force_N", 0))
ck("모의 반력: 경질부 끝(60 mm)을 지나면 30 % 넘게 감소", prof[-1] < 0.7 * prof[0],
   " ".join("%.1f" % f for f in prof))
step_seq, step_aid = d.seq + 1, d.aid + 1
d.c("DRIVE_STEP", dist_mm=-10.0)
r = d.c("DRIVE_STOP"); dd = ev(r, "DRIVE_DONE") or {}
ck("DRIVE_STOP: 원래 seq·aid 로 STOPPED, STOP 은 ACK", dd.get("result") == "STOPPED" and dd.get("seq") == step_seq
   and dd.get("aid") == step_aid and ack(r).get("ok"), str(dd))
d.c("DRIVE_STEP", dist_mm=-10.0); amax = d.ft.aid_max
r = d.c("DRIVE_STOP", aid=False, bid=False)
ck("aid 없는 DRIVE_STOP 도 정지(정지 방향 예외), 기록 불변",
   (ev(r, "DRIVE_DONE") or {}).get("result") == "STOPPED" and ack(r).get("ok") and d.ft.aid_max == amax)
d.c("HOME"); d.finish()
ck("HOME 으로 헤드 유지 해제", not d.mot["head_hold_active"] and d.ft.head_pos == 0.0)
d.c("VACUUM_ON"); r = d.tick(2.0)
ck("VACUUM_ON → VACUUM_OK, VACUUM_HOLD", bool(ev(r, "VACUUM_OK")) and d.ft.state == "VACUUM_HOLD"
   and d.mot["vac_released"] is False)
cp = ev((d.c("SET_CP_POINT", offset_mm=0.0), d.finish())[1], "CP_ZERO_SET") or {}
ck("SET_CP_POINT(offset 0) → CP_ZERO_SET, CPR_READY", cp.get("result") == "OK" and d.ft.state == "CPR_READY")
ck("목표 미설정 START_CPR 거부", ack(d.c("START_CPR")).get("why") == "TARGET_NOT_SET")
d.c("SET_TARGET", bpm=110, depth_mm=50.0)
ck("목표 설정 뒤 압박 개시", ack(d.c("START_CPR")).get("ok") and d.ft.state == "CPR_RUNNING")
ck("음압 켜진 채 DISARM → VACUUM_HOLD + rearm_lock (§2.2)",
   ack(d.c("DISARM")).get("ok") and d.ft.state == "VACUUM_HOLD" and d.mot["rearm_lock"] is True)
ck("재가동 금지 중 ARM → REARM_LOCKED", ack(d.c("ARM")).get("why") == "REARM_LOCKED")
off_seq, off_aid = d.seq + 1, d.aid + 1
d.c("VACUUM_OFF"); r = d.tick(2.0); rel = ev(r, "VACUUM_RELEASED") or {}
ck("VACUUM_OFF → 해제 확인 뒤 VACUUM_RELEASED(그 seq·aid), IDLE",
   rel.get("seq") == off_seq and rel.get("aid") == off_aid and d.ft.state == "IDLE" and not d.mot["rearm_lock"], str(rel))
ck("해제 뒤 ARM 가능", ack(d.c("ARM")).get("ok"))
d.ft.telemetry(); tl, mt = d.out[-2], d.out[-1]
ck("tel·mot 같은 tn·ms 짝", tl["t"] == "tel" and mt["t"] == "mot" and tl["tn"] == mt["tn"] and tl["ms"] == mt["ms"]
   and tl["boot_id"] == mt["boot_id"] == 777)

e = V11(settings={})
e.tick(0.5); e.ft.vac_released = True; e.c("ARM")
ck("상단 리미트 미검증 → HOME_UNAVAILABLE", ack(e.c("HOME")).get("why") == "HOME_UNAVAILABLE")
e2 = V11(settings={"limit_top_ok": 1}); e2.ft.vac_released = True
e2.c("ARM"); e2.c("HOME"); e2.finish(); a2 = ack(e2.c("HEAD_HOLD", press_mm=10.0))
ck("헤드 설정값 미설정 → LIMIT_UNSET (2-TU-008)", a2.get("why") == "LIMIT_UNSET" and a2.get("err") == "2-TU-008")
f = V11(settings=dict(SIM, scan_force_max_N=20)); f.ft.vac_released = True
f.c("ARM"); f.c("HOME"); f.finish(); f.c("HEAD_HOLD", press_mm=10.0); r = f.finish()
errs3 = [m for m in r if m.get("t") == "err"]
ck("반력 상한 초과 → 3-TU-003 먼저 보고, HEAD_HELD FAULT",
   bool(errs3) and errs3[0]["code"] == "3-TU-003" and (ev(r, "HEAD_HELD") or {}).get("result") == "FAULT"
   and f.ft.state == "FAULT" and not f.mot["head_hold_active"])
g = V11(settings=dict(SIM, drv_avg_min_n=100)); g.ft.vac_released = True
g.c("ARM"); g.c("HOME"); g.finish(); g.c("HEAD_HOLD", press_mm=10.0, drv_zero=True); g.finish()
g.c("DRIVE_STEP", dist_mm=10.0); dd = ev(g.finish(), "DRIVE_DONE") or {}
ck("반력 샘플 부족 → OK 이고 cp_force_N 생략(0 으로 대신하지 않음)", dd.get("result") == "OK" and "cp_force_N" not in dd)
h = V11(settings=dict(SIM, vac_release_timeout_ms=50), vac_residual_kPa=-5.0); h.ft.vac_released = True
h.c("ARM"); h.c("HOME"); h.finish(); h.c("VACUUM_ON"); h.tick(2.0); h.c("VACUUM_OFF")
time.sleep(0.1); r = h.tick(0.3) + h.tick(0.3)
to = [m for m in r if m.get("t") == "err" and m.get("code") == "2-VU-008"]
ck("해제 시간 초과 → 2-VU-008 한 번, VACUUM_HOLD 유지", len(to) == 1 and h.ft.state == "VACUUM_HOLD")
h.ft.inject("estop")
ck("ESTOP 중 DRIVE_STOP 은 ACK만, ARM 은 거부",
   ack(h.c("DRIVE_STOP")).get("ok") and ack(h.c("ARM")).get("why") == "ESTOP_ACTIVE")
for ft_ in _opened:
    os.close(ft_.master); os.close(ft_.slave)
del _opened[:]

# 8차: 단일 엔코더 + 자이로 모드 (§4.7)
e1 = V11(); info_e = ev(e1.q("GET_INFO"), "INFO") or {}
ck("INFO: 엔코더 모드 미설정이면 2(좌우)", info_e.get("drv_enc_mode") == 2 and "drv_enc_side" not in info_e)
e2 = V11(settings=dict(SIM, drv_enc_mode=1, drv_enc_side="L")); e2.ft.vac_released = True
info_e2 = ev(e2.q("GET_INFO"), "INFO") or {}
e2.c("ARM"); e2.c("HOME"); e2.finish(); e2.c("HEAD_HOLD", press_mm=10.0, drv_zero=True); e2.finish()
ck("모드 1: INFO 에 모드와 엔코더 쪽", info_e2.get("drv_enc_mode") == 1 and info_e2.get("drv_enc_side") == "L")
ck("모드 1: 바퀴 간격 등 필수 설정 없으면 DRIVE_STEP 거부(LIMIT_UNSET)",
   ack(e2.c("DRIVE_STEP", dist_mm=5.0)).get("why") == "LIMIT_UNSET")
e3 = V11(settings=dict(SIM, drv_enc_mode=1, drv_enc_side="L", drv_track_mm=400, drv_settle_delta_yaw_deg=0.5,
                       drv_imu_timeout_ms=100, drv_yaw_max_deg=5)); e3.ft.vac_released = True
e3.c("ARM"); e3.c("HOME"); e3.finish(); e3.c("HEAD_HOLD", press_mm=10.0, drv_zero=True); e3.finish()
e3.c("DRIVE_STEP", dist_mm=5.0); dd3 = ev(e3.finish(), "DRIVE_DONE") or {}
ck("모드 1: 필수 설정이 있으면 주행, DRIVE_DONE 에 모드 1 기록", dd3.get("result") == "OK" and dd3.get("drv_enc_mode") == 1)
lk_e = TeensyLink(port="/nonexistent")
mode_none = lk_e.drv_enc_mode
lk_e.proto, lk_e.info = "1.1", {"drv_enc_mode": 1, "drv_enc_side": "L"}
mode_1 = lk_e.drv_enc_mode
lk_e.info = {}
ck("링크: v1.1 전 None, INFO 의 모드, 없으면 2", mode_none is None and mode_1 == 1 and lk_e.drv_enc_mode == 2)

nl = TeensyLink(port="/nonexistent")
ck("GET_INFO 확인 전: 비조회 명령은 보류, 조회·정지는 즉시",
   nl.send("ARM") is None and len(nl._deferred) == 1 and nl.send("PING") is not None
   and nl.send("STOP_CPR") is not None)

print("\n== 8. v1.1 실물 연결 (가상 포트) ==")
sim3 = subprocess.Popen([sys.executable, "-u", os.path.join(SIMDIR, "fake_teensy.py"),
    "--port", "/tmp/ttyFAKE_V11", "--hz", "20", "--seed", "2", "--quiet",
    "--fw", "1.1", "--sim-settings", "--time-scale", "0.05"],
    stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
time.sleep(1.2)
t3, e3, r3, m3 = [], [], [], []
l3 = TeensyLink(port="/tmp/ttyFAKE_V11", on_telemetry=t3.append, on_event=e3.append,
                on_error=r3.append, on_mot=m3.append).start()
time.sleep(1.2)
ck("v1.1 펌웨어 판별, boot_id", l3.proto == "1.1" and l3.boot_id is not None and "MOT" in l3.caps, str(l3.proto))
ck("mot 수신", len(m3) > 5)
ck("장치 플래그: 해제 확인, 재가동 금지 없음", l3.mcu_flags == {"rearm_lock": False, "vac_released": True},
   str(l3.mcu_flags))

def wait_evt(name, timeout=3.0):
    end = time.time() + timeout
    while time.time() < end:
        hit = [x for x in e3 if x.get("evt") == name]
        if hit:
            return hit[-1]
        time.sleep(0.02)
    return None

l3.send("ARM"); time.sleep(0.3)
ck("ARM(bid·aid 부착) 반영", l3.mcu_state == "ARMED", str(l3.mcu_state))
l3.send("HOME")
ck("HOMING_DONE", (wait_evt("HOMING_DONE") or {}).get("result") == "OK")
l3.send("HEAD_HOLD", press_mm=10.0, drv_zero=True)
hh = wait_evt("HEAD_HELD") or {}
ck("HEAD_HELD OK (aid 포함)", hh.get("result") == "OK" and isinstance(hh.get("aid"), int), str(hh))
l3.send("DRIVE_STEP", dist_mm=10.0)
dd = wait_evt("DRIVE_DONE") or {}
ck("DRIVE_DONE OK, 반력 평균", dd.get("result") == "OK" and "cp_force_N" in dd, str(dd))
ck("동작 오류 없음", not [x for x in r3 if x.get("code") not in ("",)], str(r3[:2]))
last_aid = dd.get("aid", 0)
l3.stop(); time.sleep(0.3)

e4 = []
l4 = TeensyLink(port="/tmp/ttyFAKE_V11", on_event=e4.append).start()
time.sleep(1.2)
ck("Jetson 재시작: INFO last_aid 다음부터 aid", l4.proto == "1.1" and l4._aid.next() == last_aid + 1,
   "last_aid=%s" % last_aid)
l4.stop(); sim3.terminate(); sim3.wait()
try: os.remove("/tmp/ttyFAKE_V11")
except OSError: pass

print("\n== 9. 압박점 탐색 판정과 지휘 (search.py) ==")
from saver import search as S
SCFG = dict((k, v) for k, v in json.load(open(os.path.join(os.path.dirname(SIMDIR), "config.sim.json"))).items()
            if not k.startswith("_"))
pp, bad = S.check_params({})
ck("설정값 없음 → 탐색 키 전부 미설정", bad == list(S.PARAM_KEYS), ",".join(bad[:3]))
pp, bad = S.check_params(SCFG)
ck("시뮬레이션 설정은 통과", bad == [], ",".join(bad))
ck("방향 부호 0, 판정 기준 1 이상, 간격 > ROI 는 거부",
   S.check_params(dict(SCFG, scan_dir_sign=0))[1] == ["scan_dir_sign"]
   and S.check_params(dict(SCFG, tip_drop_ratio=1.2))[1] == ["tip_drop_ratio"]
   and S.check_params(dict(SCFG, scan_step_mm=200))[1] == ["scan_step_mm"]
   and S.check_params(dict(SCFG, tip_ref_n=2.5))[1] == ["tip_ref_n"]
   and S.check_params(dict(SCFG, press_mm=True))[1] == ["press_mm"])
ck("D = 89.7 × EAPD / 205.8 (식 7)", abs(S.cp_distance(205.8, pp) - 89.7) < 1e-9
   and abs(S.cp_distance(180.0, pp) - 89.7 * 180.0 / 205.8) < 1e-9)
ck("D 상한 95 mm", S.cp_distance(250.0, pp) == 95.0)
ck("EAPD 없음·0·음수·문자·NaN → None (대체 상수 없음)",
   all(S.cp_distance(v, pp) is None for v in (None, 0, -10.0, "205.8", float("nan"), True)))

xs = [5.0 * (i + 1) for i in range(16)]
sharp = [40.0] * 8 + [12.0] * 8
d1 = S.detect_tip(xs, sharp, 3, 0.3)
ck("급감 → 처음 30 % 이상 감소한 점이 말단", d1["tip_index"] == 8 and d1["tip_x"] == 45.0, str(d1["tip_x"]))
ck("판정식: r = 1 - F/Fref, Fref = 직전 N 점 평균", abs(d1["r"][8] - 0.7) < 1e-9 and d1["ref"][8] == 40.0
   and d1["r"][2] is None)
grad = [40.0 - 28.0 * i / 15.0 for i in range(16)]
ck("완만한 감소는 판정하지 않음", S.detect_tip(xs, grad, 3, 0.3)["tip_index"] is None)
rng = random.Random(5)
noisy = [f + rng.gauss(0, 0.4) for f in sharp]
ck("잡음 0.4 N 에서도 같은 점", S.detect_tip(xs, noisy, 3, 0.3)["tip_index"] == 8)
ck("감소 없음 → 말단 없음", S.detect_tip(xs, [40.0] * 16, 3, 0.3)["tip_index"] is None)
ck("샘플 부족(N 점 이하) → 판정 안 함", S.detect_tip(xs[:3], [40.0, 40.0, 12.0], 3, 0.3)["tip_index"] is None)
ck("시작 N 점 안의 급감은 N 번째 점에서 늦게 잡힘(위치 오차, 시작점은 말단보다 N 간격 이상 머리 쪽)",
   S.detect_tip(xs[:6], [40.0, 12.0, 12.0, 12.0, 12.0, 12.0], 3, 0.3)["tip_index"] == 3)
two = [40.0] * 5 + [12.0] + [40.0] * 5 + [12.0] * 5
d2 = S.detect_tip(xs, two, 3, 0.3)
ck("후보 두 묶음 → 첫 후보를 쓰고 묶음 수를 남김", d2["tip_index"] == 5 and d2["groups"] == 2, str(d2["candidates"]))
ck("반력 0(접촉 없음)은 판정에서 뺌", S.detect_tip(xs[:6], [0.0] * 6, 3, 0.3)["tip_index"] is None)


class Link11(object):
    """SearchRunner 시험용 링크. FakeTeensy11(수동 완료)에 직접 연결한다. pump() 로 응답을 넘긴다."""
    def __init__(self, dev, proto="1.1"):
        self.d, self.proto = dev, proto
        self.caps = set(dev.ft.caps) if proto == "1.1" else set()
        self.boot_id = dev.ft.boot_id
        self.fresh, self.runner, self.log = True, None, []
        self._sent, self._pair, self._tel, self.last_mot, self.drop, self.evts = {}, None, {}, {}, [], []
    def mot_fresh(self): return self.fresh
    @property
    def drv_enc_mode(self): return self.d.ft._enc_mode() if self.proto == "1.1" else None
    def status_pair(self): return self._pair
    def sent(self, seq): return dict(self._sent[seq]) if seq in self._sent else None
    def send(self, cmd, **kw):
        d = self.d
        d.seq += 1
        m = {"v": 1, "t": "cmd", "seq": d.seq, "cmd": cmd}
        if cmd not in P.QUERY_CMDS:
            d.aid += 1
            m.update(bid=self.boot_id, aid=d.aid)
        m.update(kw)
        if cmd not in P.QUERY_CMDS:
            self._sent[d.seq] = m
        self.log.append(dict(m))
        d.ft.handle(dict(m))
        return d.seq
    def resend(self, seq):
        if seq not in self._sent: return False
        self.log.append(dict(self._sent[seq], resent=True))
        self.d.ft.handle(dict(self._sent[seq]))
        return True
    def pump(self):
        while self.d.out:
            m = self.d.out.pop(0)
            t = m.get("t")
            if t == "ack":
                self.runner.on_ack(m)
            elif t == "evt":
                if m.get("evt") in self.drop:
                    self.drop.remove(m.get("evt")); continue
                self.evts.append(m)
                self.runner.on_event(m)
            elif t == "tel":
                self._tel = m
            elif t == "mot":
                self.last_mot = m
                if self._tel.get("tn") == m.get("tn"):
                    self._pair = (self._tel, m, 0.0)
    def cmds(self, name=None):
        return [m for m in self.log if name is None or m["cmd"] == name]

def rig(settings=None, cfg=None, proto="1.1", phantom=None, clock=None):
    dv = V11(settings=settings, phantom=phantom) if phantom else V11(settings=settings)
    dv.ft.vac_released = True
    dv.c("ARM"); del dv.out[:]
    lk = Link11(dv, proto)
    done = []
    kw = {"clock": clock} if clock else {}
    rn = S.SearchRunner(lk, SCFG if cfg is None else cfg, on_done=lambda ok, s: done.append((ok, s)), **kw)
    lk.runner = rn
    return dv, lk, rn, done

def drive(lk, rn, limit=500):
    for _ in range(limit):
        lk.pump()
        if not rn.active:
            break
        if lk.d.ft.job:
            lk.d.ft.finish_job()
        else:
            rn.tick()
    lk.pump()

dv, lk, rn, done = rig()
ok = rn.start(205.8); drive(lk, rn)
snap = rn.snapshot()
names = [m["cmd"] for m in lk.log]
ck("정상 탐색 → 완료", ok and done and done[-1][0] is True and snap["phase"] == "done", str(snap.get("reason")))
ck("순서: HOME → HEAD_HOLD(10 mm, drv_zero) → DRIVE_STEP → HOME → 압박점 이동",
   names[0] == "HOME" and names[1] == "HEAD_HOLD" and lk.log[1].get("drv_zero") is True
   and lk.log[1].get("press_mm") == 10.0 and names[2] == "DRIVE_STEP"
   and names.index("HOME", 1) == 2 + 24 and names[-1] == "DRIVE_STEP", "→".join(names[:4]))
ck("ROI 끝까지 취득(첫 급감에서 멈추지 않음): 24 점", len(snap["points"]) == 24 and snap["points"][-1]["x"] == 120.0)
ck("모의 말단 60 mm 를 찾음(간격 5 mm)", snap["tip_x"] is not None and abs(snap["tip_x"] - 60.0) <= 5.0, str(snap["tip_x"]))
ck("압박점 = 말단 - D (머리 쪽, D = 89.7)", snap["D_mm"] == 89.7 and abs(snap["target_x"] - (snap["tip_x"] - 89.7)) < 0.01,
   "target=%s" % snap["target_x"])
ck("압박점 도달(허용 오차 안), 이동 1회 ≤ 20 mm",
   abs(snap["pos_x"] - snap["target_x"]) <= 0.5
   and all(abs(m["dist_mm"]) <= 20.0 for m in lk.log[2 + 24 + 1:]), "pos=%s" % snap["pos_x"])
ck("이동 중 헤드는 올라가 있음", dv.ft.head_pos == 0.0 and not dv.ft.head_hold_active)
ck("탐색은 Teensy 출력값을 지정하지 않음(목표 거리만)",
   all(set(m) <= set(["v", "t", "seq", "cmd", "bid", "aid", "dist_mm", "press_mm", "drv_zero"]) for m in lk.log))

ck("탐색 기록에 엔코더 모드(기본 2)", snap.get("enc_mode") == 2)
dv, lk, rn, done = rig(settings=dict(SIM, drv_enc_mode=1, drv_enc_side="L", drv_track_mm=400, drv_settle_delta_yaw_deg=0.5,
                                     drv_imu_timeout_ms=100, drv_yaw_max_deg=5))
rn.start(205.8); drive(lk, rn)
ck("단일 엔코더 모드에서도 탐색 완료, 기록에 모드 1", rn.phase == "done" and rn.snapshot()["enc_mode"] == 1)

dv, lk, rn, done = rig()
ck("EAPD 없음 → 시작 안 함, 명령 없음", rn.start(None) is False and done[-1][1]["reason"] == "EAPD_MISSING"
   and lk.log == [] and done[-1][1]["moved"] is False)
dv, lk, rn, done = rig(cfg=dict(SCFG, tip_ref_n=None))
ck("탐색 설정값 하나라도 없음 → 시작 안 함", rn.start(205.8) is False and done[-1][1]["reason"] == "PARAM_UNSET"
   and "tip_ref_n" in done[-1][1]["detail"] and lk.log == [])
dv, lk, rn, done = rig(proto="1.0")
ck("v1.0 펌웨어 → 탐색 끔", rn.start(205.8) is False and done[-1][1]["reason"] == "UNSUPPORTED" and lk.log == [])
dv, lk, rn, done = rig()
rn.start(250.0); drive(lk, rn)
ck("EAPD 250 → D 상한 95", rn.snapshot()["D_mm"] == 95.0 and rn.phase == "done")

dv, lk, rn, done = rig(settings=dict(SIM, drv_avg_min_n=100))
rn.start(205.8); drive(lk, rn)
ck("반력 샘플 부족 → 실패, 0 으로 대신하지 않음", rn.snapshot()["reason"] == "FORCE_SAMPLES_SHORT"
   and len(lk.cmds("DRIVE_STEP")) == 1 and rn.snapshot()["points"] == [])
dv, lk, rn, done = rig(settings=dict(SIM, drv_speed_mm_s=None))
rn.start(205.8); drive(lk, rn)
ck("Teensy 거부(설정값 없음) → 실패, 사유 보존", rn.snapshot()["reason"] == "REJECTED"
   and "LIMIT_UNSET" in rn.snapshot()["detail"], str(rn.snapshot()["detail"]))
dv, lk, rn, done = rig(phantom=Phantom(k_hard=1.2, k_soft=1.2))
rn.start(205.8); drive(lk, rn)
ck("말단 없음 → ROI 끝까지 본 뒤 헤드 올리고 실패", rn.snapshot()["reason"] == "TIP_NOT_FOUND"
   and lk.log[-1]["cmd"] == "HOME" and dv.ft.head_pos == 0.0 and not dv.ft.head_hold_active)

dv, lk, rn, done = rig()
rn.start(205.8)
for _ in range(8):
    lk.pump(); dv.ft.finish_job()
lk.pump()
n_before = len(lk.log)
rn.cancel("USER_STOP"); lk.pump()
ck("사용자 중지 → DRIVE_STOP, STOPPED 받고 실패, 다음 명령 없음",
   lk.log[n_before]["cmd"] == "DRIVE_STOP" and len(lk.log) == n_before + 1 and rn.phase == "failed"
   and rn.snapshot()["reason"] == "USER_STOP" and "STOPPED" in str(rn.snapshot()["detail"]))

T = [100.0]
dv, lk, rn, done = rig(clock=lambda: T[0])
lk.fresh = False
rn.start(205.8)
ck("mot 오래됨 → 동작 명령 보류", lk.log == [] and rn.active)
T[0] += 1.0; rn.tick(); lk.fresh = True; rn.tick(); lk.pump()
ck("mot 회복 → 보류한 명령 송신", [m["cmd"] for m in lk.log] == ["HOME"])
dv, lk, rn, done = rig(clock=lambda: T[0])
lk.fresh = False; rn.start(205.8); T[0] += 2.5; rn.tick()
ck("mot 오래됨 2 s 초과 → 실패", rn.snapshot()["reason"] == "MOT_STALE" and lk.log == [])

# 완료 이벤트 유실(§4.4)
dv, lk, rn, done = rig(clock=lambda: T[0])
rn.start(205.8)
for _ in range(3):
    lk.pump(); dv.ft.finish_job()
lk.pump()                                     # HOME, HEAD_HOLD, DRIVE_STEP 1 완료 → DRIVE_STEP 2 송신
lk.drop.append("DRIVE_DONE"); dv.ft.finish_job(); lk.pump()
lost = lk.log[-1]; x_before = dv.ft._x
T[0] += 4.0; rn.tick()
ck("한도 전에는 기다림", lk.log[-1] is lost)
T[0] += 2.0; rn.tick(); lk.pump()
ck("한도 초과 → GET_STATUS", lk.log[-1]["cmd"] == "GET_STATUS")
rn.tick(); lk.pump()
rs = [m for m in lk.log if m.get("resent")]
ck("mot_last_aid 일치 → 같은 seq·aid·파라미터로 재요청, 재실행 없음",
   len(rs) == 1 and rs[0]["seq"] == lost["seq"] and rs[0]["aid"] == lost["aid"]
   and rs[0]["dist_mm"] == lost["dist_mm"] and dv.ft._x == x_before)
ck("저장된 완료 이벤트로 이어서 진행", len(rn.snapshot()["points"]) == 2 and lk.log[-1]["cmd"] == "DRIVE_STEP"
   and lk.log[-1]["seq"] != lost["seq"])
pts, nlog = len(rn.snapshot()["points"]), len(lk.log)
replay = [m for m in lk.evts if m.get("seq") == lost["seq"]]
rn.on_event(dict(replay[-1])); lk.pump()
ck("이미 처리한 완료 이벤트 중복 → 무시", replay and len(rn.snapshot()["points"]) == pts and len(lk.log) == nlog)

dv, lk, rn, done = rig(clock=lambda: T[0])
rn.start(205.8)
for _ in range(2):
    lk.pump(); dv.ft.finish_job()
lk.pump()                                     # DRIVE_STEP 1 진행 중(완료 안 함)
T[0] += 6.0; rn.tick(); lk.pump(); rn.tick(); lk.pump()
ck("busy_aid 일치 → 한 번 더 기다림", rn.active and lk.log[-1]["cmd"] == "GET_STATUS")
T[0] += 6.0; rn.tick(); lk.pump(); rn.tick(); lk.pump()
ck("그래도 안 끝남 → DRIVE_STOP, STOPPED 받고 실패", rn.snapshot()["reason"] == "TIMEOUT"
   and lk.cmds("DRIVE_STOP") and "STOPPED" in str(rn.snapshot()["detail"]))

dv, lk, rn, done = rig(clock=lambda: T[0])
rn.start(205.8)
for _ in range(3):
    lk.pump(); dv.ft.finish_job()
lk.pump()
lk.drop.append("DRIVE_DONE"); dv.ft.finish_job(); lk.pump()
dv.ft.act_cur = None
T[0] += 6.0; rn.tick(); lk.pump(); rn.tick()
ck("동작 기록에 없음 → 상태 불일치로 실패", rn.snapshot()["reason"] == "STATE_MISMATCH")

dv, lk, rn, done = rig(clock=lambda: T[0])
rn.start(205.8); lk.pump(); lk.boot_id = 999; rn.tick()
ck("재부팅(boot_id 변화) → 결과 불명, 재전송 없이 실패", rn.snapshot()["reason"] == "REBOOTED"
   and not [m for m in lk.log if m.get("resent")])
for ft_ in _opened:
    try:
        os.close(ft_.master); os.close(ft_.slave)
    except OSError:
        pass

# 가상 포트: 실제 TeensyLink 와 모의 Teensy 프로세스
sim5 = subprocess.Popen([sys.executable, "-u", os.path.join(SIMDIR, "fake_teensy.py"),
    "--port", "/tmp/ttyFAKE_SRCH", "--hz", "20", "--seed", "4", "--quiet",
    "--fw", "1.1", "--sim-settings", "--time-scale", "0.05"],
    stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
time.sleep(1.2)
holder, r5, d5 = {}, [], []
l5 = TeensyLink(port="/tmp/ttyFAKE_SRCH", on_error=r5.append,
                on_event=lambda m: holder["r"].on_event(m), on_ack=lambda m: holder["r"].on_ack(m))
rn5 = S.SearchRunner(l5, SCFG, on_done=lambda ok, s: d5.append((ok, s)))
holder["r"] = rn5
l5.start(); time.sleep(1.2)
l5.send("ARM"); time.sleep(0.3)
rn5.start(180.0)
end = time.monotonic() + 20.0
while rn5.active and time.monotonic() < end:
    rn5.tick(); time.sleep(0.02)
s5 = rn5.snapshot()
ck("가상 포트: 탐색 완료, 말단 60 mm 부근, 압박점 도달",
   s5["phase"] == "done" and abs(s5["tip_x"] - 60.0) <= 5.0 and abs(s5["pos_x"] - s5["target_x"]) <= 0.5,
   "%s tip=%s target=%s pos=%s" % (s5["phase"], s5["tip_x"], s5["target_x"], s5["pos_x"]))
ck("가상 포트: EAPD 180 → D = 78.45", abs(s5["D_mm"] - 78.45) < 0.01, str(s5["D_mm"]))
ck("가상 포트: 오류 없음", not r5, str(r5[:2]))
l5.stop(); sim5.terminate(); sim5.wait()
try: os.remove("/tmp/ttyFAKE_SRCH")
except OSError: pass

print("\n%s  (통과 %d / 실패 %d)\n" % ("전체 통과" if P_["fail"] == 0 else "실패 있음", P_["pass"], P_["fail"]))
sys.exit(0 if P_["fail"] == 0 else 1)
