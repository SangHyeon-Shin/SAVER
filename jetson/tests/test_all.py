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
_EXD = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))), "protocol", "examples")
_ex_bad = []
for _fn in ("commands.jsonl", "telemetry.jsonl", "v11_worst_case.jsonl"):
    for _ln in open(os.path.join(_EXD, _fn), "rb").read().split(b"\n"):
        if _ln and (P.decode(_ln) is None or len(_ln) > P.MAX_BODY):
            _ex_bad.append((_fn, _ln[:40]))
_exc = [P.decode(l) for l in open(os.path.join(_EXD, "commands.jsonl"), "rb").read().split(b"\n") if l]
ck("명세 예제(protocol/examples) 모든 줄 수신 가능, 본문 511 byte 이하", not _ex_bad, str(_ex_bad[:2]))
_wc = [P.decode(l) for l in open(os.path.join(_EXD, "v11_worst_case.jsonl"), "rb").read().split(b"\n") if l]
ck("최악값 줄(Teensy 생성 함수): tel·mot·INFO 포함, 모두 수신",
   len(_wc) >= 14 and all(_wc) and {"tel", "mot"} <= set(m.get("t") for m in _wc)
   and any(m.get("evt") == "INFO" for m in _wc), str(len(_wc)))
ck("명세 예제: 첫 명령은 GET_INFO, 앞쪽 비조회 명령은 bid·aid 를 싣는다(v1.1)",
   _exc[0]["cmd"] == "GET_INFO" and all("bid" in m and "aid" in m for m in _exc[:20] if m["cmd"] not in P.QUERY_CMDS))

print("\n== 2. 상태머신 ==")
fired = []
fsm = StateMachine(on_command=lambda c: fired.append(c))
path = ["INIT_DONE","MOVE_DONE","START_PRESSED","CLOTHES_OK","AED_PADS_ON",
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
ck("v2.3·v2.4: S1 오류 확인 뒤 S1 로 돌아감(명령은 진입 명령이 아니라 초기화 절차가 보냄)",
   fs1.fire("INIT_ERROR") and fs1.node == "S10" and fs1.fire("RECHECK") and fs1.node == "S1"
   and s1_cmds == [] and fs1.procedure() == "init", str(s1_cmds))

# v2.4 (한건민 합의 10/5): X_WHEELS 삭제, S1 결과 기반 전이, X_VENT 정지
fw = StateMachine()
ck("v2.4: X_WHEELS·WHEELS_DONE 삭제, S1 INIT_DONE → X_MOVE",
   "X_WHEELS" not in fw.nodes and "WHEELS_DONE" not in fw.events and fw.cur["on"]["INIT_DONE"] == "X_MOVE")
ck("v2.4: 표시 이름만 정리(X_MOVE, X_PIN), 안전핀 화면의 장치 조건은 그대로",
   fw.nodes["X_MOVE"]["name"] == "환자에게 이동" and fw.nodes["X_PIN"]["name"] == "안전핀 해제"
   and fw.nodes["X_PIN"].get("requires_mcu") == ["HOMED"] and fw.nodes["X_SEARCH"].get("requires_mcu") == ["HOMED"])
ck("v2.4: S1 은 타이머 없이 초기화 절차로(Jetson 자동 전이 없음, offline_auto 는 오프라인 시연 전용)",
   fw.cur.get("auto") is None and fw.auto_due() is None and fw.procedure() == "init"
   and fw.cur.get("offline_auto", {}).get("event") == "INIT_DONE" and not fw.cur.get("enter_cmds"))
bad_ie = []
for nd in ("X_MOVE", "S2", "X_CLOTHES", "X_AED_PAD", "S3S4", "X_PPG"):
    fi = StateMachine(); fi.force(nd)
    if not (fi.fire("INIT_ERROR") and fi.node == "S10" and fi.error_cause == "init" and fi.error_return == "S1"
            and fi.fire("RECHECK") and fi.node == "S1"):
        bad_ie.append(nd)
ck("v2.4: 원점 완료 전 오류는 탐색 전 노드 어디서나 INIT_ERROR → S10 → RECHECK → S1", not bad_ie, str(bad_ie))
ck("v2.4: 탐색부터는 INIT_ERROR 를 받지 않음(그 전에 HOMED 확인)",
   all("INIT_ERROR" not in fw.nodes[n]["on"] for n in ("X_SEARCH", "X_PIN", "S5", "S6", "S7")))
fp = StateMachine(); fp.force("S3S4"); fp.fire("POSITION_ERROR")
ck("v2.4: 다른 오류의 복귀 노드는 그대로(정렬 오류 → S3S4)", fp.error_return == "S3S4" and fp.error_cause == "position")
sent_x, mcu_x = [], {"s": "CPR_RUNNING"}
fx = StateMachine(mcu_state=lambda: mcu_x["s"], on_command=sent_x.append); fx.force("S7")
ck("v2.4: 인공호흡 안내에서 장치가 멈췄다고 알리기 전에도 [정지] → S11, STOP_CPR",
   fx.fire("VENT_GUIDE") and fx.node == "X_VENT" and fx.fire("STOP") and fx.node == "S11"
   and sent_x == ["PAUSE_CPR", "STOP_CPR"], str(sent_x))
fxv = StateMachine(); fxv.force("X_VENT")
ck("v2.4: 인공호흡 안내의 정지는 브리지 차단 대상이 아님(확인값과 관계없이)",
   B.hands_on_block_reason(fxv, "STOP", {"pause_stop_verified": False}) is None)
ck("v2.4: 안전정지에서 압박 재개 경로 없음", "S7" not in fw.nodes["S11"]["on"].values()
   and sorted(fw.nodes["S11"]["on"]) == ["END", "MANUAL_RELEASE"])

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

# S13 조건: 음압이 남아 있으면 S12 (v1.1 합의 12번). 아니면 S1 에서 초기화를 다시 한다(v2.5, D-47)
flags = {"v": None}
f4 = StateMachine(mcu_flags=lambda: flags["v"]); f4.force("S13")
ck("플래그 미지원(v1.0)이면 S13 → S1(다시 초기화)", f4.fire("BACK_TO_IDLE") and f4.node == "S1")
f4.force("S13"); flags["v"] = {"rearm_lock": True, "vac_released": False}
ck("rearm_lock 이면 S13 → S12", f4.fire("BACK_TO_IDLE") and f4.node == "S12" and f4.log[-1].get("redirect"))
ck("S12 해제 뒤 S13", f4.fire("RELEASE_DONE") and f4.node == "S13")
flags["v"] = {"rearm_lock": False, "vac_released": True}
ck("해제 확인되면 S13 → S1(다시 초기화)", f4.fire("BACK_TO_IDLE") and f4.node == "S1")
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

print("\n== 3. 가짜 Teensy 실물 연결 (v1.1 기본 모의 장치) ==")
sim = subprocess.Popen([sys.executable, "-u",
    os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "simulators", "fake_teensy.py"),
    "--port", "/tmp/ttyFAKE_TEST", "--hz", "20", "--seed", "1", "--quiet", "--time-scale", "0.05"],
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
ck("v1.1 펌웨어 판별(GET_INFO: proto 1.1, caps AID·MOT·FAULTS)", link.proto == "1.1" and link.fw_ok is True
   and {"AID", "MOT", "FAULTS"} <= link.caps and link.mcu_flags is not None, str(link.proto))
ck("판 불일치 오류 없음", not any(e.get("code") == "3-CM-001" for e in errs))

link.send("ARM"); time.sleep(0.4)
ck("ARM 반영", link.mcu_state == "ARMED", str(link.mcu_state))
link.send("HOME"); time.sleep(1.3)
ck("HOMING_DONE 이벤트", any(e.get("evt") == "HOMING_DONE" for e in evts))
link.send("VACUUM_ON"); time.sleep(1.8)
ck("VACUUM_OK 이벤트", any(e.get("evt") == "VACUUM_OK" for e in evts))

n0 = len(errs)
link.send("START_CPR"); time.sleep(0.4)
ck("잘못된 상태 명령 거부", len(errs) > n0 and errs[-1]["code"] == "2-CU-011", str(errs[-1] if errs else ""))

link.send("SET_CP_POINT", offset_mm=0); time.sleep(0.6)
cpz = [e for e in evts if e.get("evt") == "CP_ZERO_SET"]
ck("압박 접촉 영점(SET_CP_POINT offset 0) → CP_ZERO_SET OK, CPR_READY",
   bool(cpz) and cpz[-1].get("result") == "OK" and link.mcu_state == "CPR_READY", str(cpz[-1:]))
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
ls = link.link_stats()
ck("링크 품질: 고장 주입·송신 중단·재연결이 누적 횟수에 남음",
   ls["link_up"] >= 2 and ls["link_down"] >= 1 and ls["stale"] >= 1 and ls["ack_timeout"] >= 1
   and ls["fault"] >= 1 and ls["mcu_err"] >= 1 and ls["tel_rx"] > 20, str(dict(ls)))
link.stop(); sim2.terminate(); sim2.wait()
try: os.remove("/tmp/ttyFAKE_TEST")
except OSError: pass

print("\n== 6. Jetson 재시작, Teensy 전원 유지 ==")
# Teensy 를 별도 5 V 로 켜면(VIN, VUSB 분리) Jetson 재시작이 Teensy 를 리셋하지 않는다.
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "simulators"))
from fake_teensy import FakeTeensy
from fake_teensy_v11 import FakeTeensy11

# v1.1: 오래된 요청은 seq 가 아니라 aid 로 가린다(§2.5). 젯슨만 재시작하면 GET_INFO 의 last_aid 다음부터 쓴다
_rs = []
ft = FakeTeensy11(seed=1, outbox=_rs, boot_id=500, settings={})
ft.vac_released = True
def _c6(seq, aid, cmd, **kw):
    n = len(_rs)
    ft.handle(dict({"v": 1, "t": "cmd", "seq": seq, "cmd": cmd, "bid": 500, "aid": aid}, **kw))
    return [m for m in _rs[n:] if m.get("t") == "ack"][-1]
_c6(7, 1, "ARM")
a6 = _c6(1, 1, "SET_TARGET", depth_mm=50.0)
ck("젯슨만 재시작하고 aid 를 1부터 다시 쓰면 실행 안 됨(SEQ_CONFLICT)", a6.get("why") == "SEQ_CONFLICT"
   and ft.target_depth == 0, str(a6))
n6 = len(_rs); ft.handle({"v": 1, "t": "cmd", "seq": 2, "cmd": "GET_INFO"})
i6 = [m for m in _rs[n6:] if m.get("evt") == "INFO"][0]
a6 = _c6(i6["last_seq"] + 1, i6["last_aid"] + 1, "SET_TARGET", depth_mm=50.0)
ck("GET_INFO 의 last_seq·last_aid 다음부터 이어 쓰면 실행", a6.get("ok") and ft.target_depth == 50.0
   and i6["last_aid"] == 1 and i6["last_seq"] == 7, str(i6))
os.close(ft.master); os.close(ft.slave)
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

# 링크 품질 기록(노이즈 대책): 버린 줄, 재전송, 재부팅 등을 센다
class _RW(object):
    def __init__(self, chunks): self.chunks = list(chunks); self.out = []
    def read(self, n): return self.chunks.pop(0) if self.chunks else b""
    def write(self, b): self.out.append(P.decode(b))
    def close(self): pass
def _lk():
    k = TeensyLink(port="/nonexistent"); k.proto = "1.1"; k.caps = set(["AID", "MOT"]); k.boot_id = 7
    k._aid = P.AidCounter(1); k.connected = True; k._ser = _RW([])
    return k
lq = _lk()
lq._ser.chunks = [b'{"broken\n', b'\n',
                  b'{"v":1,"t":"tel","tn":5,"boot_id":7,"state":"IDLE"}\n',
                  b'{"v":1,"t":"tel","tn":3,"boot_id":7,"state":"IDLE"}\n',
                  b'{"v":1,"t":"tel","tn":6,"boot_id":7,"state":"FAULT"}\n',
                  b'{"v":1,"t":"tel","tn":7,"boot_id":7,"state":"FAULT"}\n',
                  b'{"v":1,"t":"err","code":"3-CU-021","sev":3}\n',
                  b"x" * 600]
for _ in range(10): lq._read_once()
st = lq.link_stats()
ck("링크 품질: 깨진 줄 1(빈 줄은 세지 않음), tn 역전 1, 넘침 1",
   st["bad_line"] == 1 and st["out_of_order"] == 1 and st["overflow"] == 1, str(dict(st)))
ck("링크 품질: 텔레메트리 3, FAULT 전환 1(연속 FAULT 는 1회), 장치 오류 1",
   st["tel_rx"] == 3 and st["fault"] == 1 and st["mcu_err"] == 1, str(dict(st)))
lq.boot_id = 7
lq._ser.chunks = [b'{"v":1,"t":"tel","tn":1,"boot_id":8,"state":"IDLE"}\n']
lq._read_once()
ck("링크 품질: boot_id 변화 → 재부팅 1", lq.link_stats()["reboot"] == 1, str(lq.link_stats()["reboot"]))
lr = _lk(); sq = lr.send("ARM")
for _ in range(3):
    lr._pending[sq][1] -= 1.0
    lr._tick()
lr.resend(sq)
st = lr.link_stats()
ck("링크 품질: ACK 재전송 2회 뒤 통신 오류 1, 완료 이벤트 재전송 1",
   st["ack_retry"] == 2 and st["ack_timeout"] == 1 and st["resend"] == 1, str(dict(st)))
ls3 = _lk()
ls3.last_tel_at = time.monotonic() - 1.0; ls3._tick(); ls3._tick()
n_a = ls3.link_stats()["stale"]
ls3.last_tel_at = time.monotonic(); ls3._tick()
ls3.last_tel_at = time.monotonic() - 1.0; ls3._tick()
ck("링크 품질: stale 은 전환만 셈(이어지는 동안은 1회)", n_a == 1 and ls3.link_stats()["stale"] == 2,
   "%d %d" % (n_a, ls3.link_stats()["stale"]))
ck("링크 품질: 항목 순서 고정", list(lq.link_stats())[:3] == ["link_up", "link_down", "bad_line"])

nl = TeensyLink(port="/nonexistent")
ck("GET_INFO 확인 전: 비조회 명령은 보류, 조회·정지는 즉시",
   nl.send("ARM") is None and len(nl._deferred) == 1 and nl.send("PING") is not None
   and nl.send("STOP_CPR") is not None)

# 판 확인: v1.1 만 받는다. v1.0 펌웨어는 GET_INFO 를 UNKNOWN_COMMAND 로 거부한다
def _fwlink():
    k = TeensyLink(port="/nonexistent"); k.connected = True; k._ser = _RW([])
    e_, p_ = [], []
    k.on_error, k.on_proto = e_.append, (lambda pr, inf: p_.append(pr))
    k._handshake()
    return k, e_, p_
lm, em, pm = _fwlink()
lm.send("ARM")
lm._handle({"t": "ack", "seq": lm._info_seq, "ok": False, "err": "1-CM-005", "why": "UNKNOWN_COMMAND"})
ck("v1.0 펌웨어(GET_INFO 거부) → 판 불일치 3-CM-001 한 번, fw_ok False, 판 알림",
   lm.proto == "1.0" and lm.fw_ok is False and [e["code"] for e in em].count("3-CM-001") == 1
   and pm == ["1.0"] and lm.fw_mismatch == "FW_NOT_V11:1.0", str(em))
ck("판 불일치: 보류했던 ARM 은 보내지 않고(NOT_SENT) 새 ARM 도 보내지 않음",
   "NOT_SENT:ARM" in [e.get("why") for e in em] and lm.send("ARM") is None
   and "ARM" not in [m.get("cmd") for m in lm._ser.out])
sp_, ss_ = lm.send("PING"), lm.send("STOP_CPR")
ck("판 불일치: 조회와 정지 방향 명령은 보냄(bid·aid 없이)", sp_ is not None and ss_ is not None
   and lm._ser.out[-1].get("cmd") == "STOP_CPR" and "aid" not in lm._ser.out[-1])
lm2, em2, pm2 = _fwlink()
lm2._handle({"t": "evt", "evt": "INFO", "seq": lm2._info_seq, "proto": "1.1", "caps": ["MOT"], "boot_id": 3})
lm3, em3, pm3 = _fwlink()
lm3._handle({"t": "evt", "evt": "INFO", "seq": lm3._info_seq, "proto": "1.1", "caps": ["AID", "MOT"], "boot_id": 3,
             "last_seq": 40, "last_aid": 9})
ck("INFO 에 AID 가 없으면 판 불일치, proto 1.1 과 AID 면 v1.1(seq·aid 이어 감)",
   lm2.fw_ok is False and lm2.proto == "1.1-noAID" and pm2 == ["1.1-noAID"]
   and [e["code"] for e in em2] == ["3-CM-001"]
   and lm3.fw_ok is True and lm3._seq.next() == 41 and lm3._aid.next() == 10 and em3 == [], "%s %s" % (lm2.proto, em3))

# GET_FAULTS(§3.5): err 를 못 받은 코드나 faults_more 면 전체 목록을 묻는다
lf = TeensyLink(port="/nonexistent"); lf.connected = True; lf._ser = _RW([])
lf.proto, lf.caps, lf.boot_id, lf._aid = "1.1", set(["AID", "MOT", "FAULTS"]), 9, P.AidCounter(1)
ef = []; lf.on_error = ef.append
def _gf(): return [m for m in lf._ser.out if m and m.get("cmd") == "GET_FAULTS"]
lf._handle({"t": "tel", "tn": 1, "boot_id": 9, "state": "FAULT", "faults": ["3-CU-021"]})
g1 = _gf()
lf._handle({"t": "ack", "seq": g1[0]["seq"] if g1 else -1, "ok": True, "state": "FAULT"})
lf._handle({"t": "err", "code": "3-CU-021", "sev": 3, "latched": True, "why": "FAULT_ACTIVE"})
lf._handle({"t": "evt", "evt": "FAULTS_END", "seq": g1[0]["seq"] if g1 else -1, "n": 1})
ck("err 를 못 받은 고장 코드 → GET_FAULTS, 처음 받는 코드는 평소처럼 처리(replay 아님), n 일치",
   len(g1) == 1 and ef and not ef[-1].get("replay") and lf._faults_seq is None and lf.faults_full == ["3-CU-021"],
   str(ef[-1:]))
lf._faults_at -= 5
lf._handle({"t": "tel", "tn": 2, "boot_id": 9, "state": "FAULT", "faults": ["3-CU-021"]})
ck("GET_FAULTS: 같은 목록이면 다시 묻지 않음", len(_gf()) == 1)
lf._faults_at -= 5
lf._handle({"t": "tel", "tn": 3, "boot_id": 9, "state": "FAULT", "faults": ["3-CU-021", "2-VU-005"], "faults_more": True})
g2 = _gf()
lf._handle({"t": "err", "code": "3-CU-021", "sev": 3, "latched": True, "why": "FAULT_ACTIVE"})
rp = ef[-1].get("replay") is True
lf._handle({"t": "evt", "evt": "FAULTS_END", "seq": g2[-1]["seq"], "n": 3})
ck("faults_more → GET_FAULTS, 이미 받은 코드는 replay 표시, n 불일치면 다음 텔레메트리에서 다시 묻기",
   len(g2) == 2 and rp and lf._faults_sig is None and lf._faults_seq is None)
lf._faults_at -= 5
lf._handle({"t": "tel", "tn": 4, "boot_id": 9, "state": "FAULT", "faults": ["3-CU-021", "2-VU-005"], "faults_more": True})
ck("GET_FAULTS: 불일치 뒤 다시 요청", len(_gf()) == 3)

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

print("\n== 8-2. v1.0 펌웨어 연결 → 판 불일치 (가상 포트) ==")
sim10 = subprocess.Popen([sys.executable, "-u", os.path.join(SIMDIR, "fake_teensy.py"),
    "--port", "/tmp/ttyFAKE_V10", "--hz", "20", "--seed", "4", "--quiet", "--fw", "1.0"],
    stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
time.sleep(1.2)
t10, r10, p10 = [], [], []
l10 = TeensyLink(port="/tmp/ttyFAKE_V10", on_telemetry=t10.append, on_error=r10.append,
                 on_proto=lambda pr, inf: p10.append(pr)).start()
time.sleep(1.5)
ck("v1.0 펌웨어: 판 불일치(3-CM-001 FW_NOT_V11:1.0) 한 번, 텔레메트리는 계속 받음",
   l10.proto == "1.0" and l10.fw_ok is False and p10 == ["1.0"] and len(t10) > 5
   and [e.get("why") for e in r10 if e.get("code") == "3-CM-001"] == ["FW_NOT_V11:1.0"], str(r10[:3]))
ck("v1.0 펌웨어: ARM 은 보내지 않아 장치 상태 그대로(IDLE)", l10.send("ARM") is None and (time.sleep(0.4) or True)
   and l10.mcu_state == "IDLE", str(l10.mcu_state))
ck("v1.0 펌웨어: 하트비트(PING) 응답이 와서 ACK 시간 초과 없음", not any(e.get("code") == "2-CM-002" for e in r10))
l10.stop(); sim10.terminate(); sim10.wait()
try: os.remove("/tmp/ttyFAKE_V10")
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
ck("v1.1 이 아닌 펌웨어 → 탐색 끔(FW_VERSION), 명령 없음",
   rn.start(205.8) is False and done[-1][1]["reason"] == "FW_VERSION" and lk.log == [])
dv, lk, rn, done = rig()
lk.caps = set(["MOT", "AID", "FAULTS"])
ck("v1.1 이지만 주행·헤드 기능 없음(caps) → 탐색 끔(UNSUPPORTED, 빠진 기능), 명령 없음",
   rn.start(205.8) is False and done[-1][1]["reason"] == "UNSUPPORTED"
   and done[-1][1]["detail"] == "DRIVE,HEAD_HOLD" and lk.log == [], str(done[-1][1]))
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

print("\n== 10. S1 초기화 절차 (v2.4, init_seq.py) ==")
from saver.init_seq import InitRunner


class _FL(object):
    """명령을 기록만 하는 링크. ACK·텔레메트리는 시험이 직접 넣는다."""
    def __init__(self):
        self.connected, self.proto, self.sent, self.n = True, "1.1", [], 100
    def send(self, cmd, **kw):
        self.n += 1
        self.sent.append((cmd, self.n))
        return self.n


def irig(cfg=None, ready=True):
    lk, evs, clk = _FL(), [], {"t": 0.0}
    if not ready:
        lk.proto = None
    rn = InitRunner(lk, fire=lambda e, p=None: (evs.append((e, p)), True)[1], cfg=cfg or {}, clock=lambda: clk["t"])
    return lk, rn, evs, clk


def cmds(lk):
    return [c for c, _ in lk.sent]


def iack(rn, lk, ok=True, why=None):
    rn.on_ack({"t": "ack", "seq": lk.sent[-1][1], "ok": ok, "err": "" if ok else "2-CU-011", "why": why or ""})


def itel(rn, state, estop=False, faults=None):
    rn.on_tel({"t": "tel", "state": state, "estop": estop, "faults": faults or []})


def until_status(rn, lk):
    rn.start(); iack(rn, lk); iack(rn, lk)


lk, rn, evs, clk = irig()
rn.start(); a1 = cmds(lk) == ["PING"]
iack(rn, lk); a2 = cmds(lk) == ["PING", "GET_STATUS"]
iack(rn, lk); itel(rn, "IDLE"); a3 = cmds(lk)[-1] == "ARM"
iack(rn, lk); a4 = cmds(lk)[-1] == "HOME" and evs == []
iack(rn, lk); a5 = evs == [("INIT_DONE", None)] and rn.step == "homing" and not rn.homed
itel(rn, "ARMED"); a6 = rn.step == "homing"
rn.on_event({"evt": "HOMING_DONE", "result": "OK"}); a7 = rn.step == "done" and rn.homed
ck("초기화: IDLE → PING, GET_STATUS, ARM, HOME 차례, HOME ACK 뒤에야 INIT_DONE, 원점 완료는 뒤에서",
   all([a1, a2, a3, a4, a5, a6, a7]), "%s %s" % (cmds(lk), evs))

lk, rn, evs, clk = irig(); until_status(rn, lk); itel(rn, "ARMED"); iack(rn, lk)
ck("초기화: ARMED → ARM 생략, HOME 만", cmds(lk) == ["PING", "GET_STATUS", "HOME"] and evs == [("INIT_DONE", None)], str(cmds(lk)))
lk, rn, evs, clk = irig(); until_status(rn, lk); itel(rn, "HOMED")
ck("초기화: HOMED → ARM·HOME 생략, 원점 완료로 인정", cmds(lk) == ["PING", "GET_STATUS"] and evs == [("INIT_DONE", None)]
   and rn.step == "done" and rn.homed, str(cmds(lk)))

def err_case(state, estop=False, faults=None):
    lk, rn, evs, clk = irig(); until_status(rn, lk); itel(rn, state, estop, faults)
    return cmds(lk) == ["PING", "GET_STATUS"] and len(evs) == 1 and evs[0][0] == "INIT_ERROR" \
        and evs[0][1]["cause"] == "init", (rn.reason, cmds(lk))
r_f, d_f = err_case("FAULT"); r_e, d_e = err_case("ESTOP", estop=True); r_e2, d_e2 = err_case("IDLE", estop=True)
r_fl, d_fl = err_case("IDLE", faults=["3-CU-021"])
ck("초기화: FAULT, ESTOP, 비상정지 표시, 활성 고장 → INIT_ERROR, ARM 안 보냄",
   r_f and r_e and r_e2 and r_fl, str([d_f, d_e, d_e2, d_fl]))
bad_u = [st for st in ("VACUUM_HOLD", "CPR_READY", "CPR_RUNNING", "CPR_PAUSED") if not err_case(st)[0]]
lk, rn, evs, clk = irig(); until_status(rn, lk); itel(rn, "CPR_RUNNING")
ck("초기화: VACUUM_HOLD·CPR_* → UNEXPECTED_MCU_STATE, 자동 DISARM 안 함",
   not bad_u and "DISARM" not in cmds(lk) and str(rn.reason).startswith("UNEXPECTED_MCU_STATE"), str(bad_u))

lk, rn, evs, clk = irig(); until_status(rn, lk); itel(rn, "IDLE"); iack(rn, lk, ok=False, why="ESTOP_ACTIVE")
ck("초기화: ARM 거부 → INIT_ERROR, HOME 안 보냄", "HOME" not in cmds(lk) and evs and evs[-1][0] == "INIT_ERROR"
   and "ESTOP_ACTIVE" in rn.reason, str(rn.reason))
lk, rn, evs, clk = irig(); until_status(rn, lk); itel(rn, "ARMED"); iack(rn, lk, ok=False, why="NOT_ARMED")
ck("초기화: HOME 거부 → INIT_ERROR, 화면 진행(INIT_DONE) 없음", [e for e, _ in evs] == ["INIT_ERROR"], str(evs))
lk, rn, evs, clk = irig(); rn.start(); s_ping = lk.sent[-1][1]
own = rn.owns(s_ping) and not rn.owns(9999)
rn.on_error({"code": "2-CM-002", "seq": s_ping, "why": "ACK_TIMEOUT:PING"})
ck("초기화: 응답 없음(2-CM-002) → INIT_ERROR, 절차가 보낸 명령을 알아봄", own and [e for e, _ in evs] == ["INIT_ERROR"], str(rn.reason))
lk, rn, evs, clk = irig(); rn.start(); iack(rn, lk); rn.on_error({"code": "2-CU-011", "seq": lk.sent[-1][1], "why": "X"})
ck("초기화: 거부 오류(ok=false)는 ACK 쪽에서만 처리(이중 처리 없음)", evs == [] and rn.step == "status")
lk, rn, evs, clk = irig(); until_status(rn, lk); clk["t"] += P.STALE_S + 0.1; rn.tick()
ck("초기화: GET_STATUS 뒤 텔레메트리가 없으면 INIT_ERROR(NO_STATUS)", rn.reason == "NO_STATUS" and evs[-1][0] == "INIT_ERROR")

def homing_rig(cfg=None):
    lk, rn, evs, clk = irig(cfg); until_status(rn, lk); itel(rn, "ARMED"); iack(rn, lk)
    return lk, rn, evs, clk
lk, rn, evs, clk = homing_rig(); itel(rn, "FAULT")
ck("원점 대기: FAULT → INIT_ERROR", [e for e, _ in evs] == ["INIT_DONE", "INIT_ERROR"] and rn.reason == "HOMING_LOST:FAULT", str(rn.reason))
lk, rn, evs, clk = homing_rig(); rn.on_event({"evt": "HOMING_DONE", "result": "TIMEOUT"})
ck("원점 대기: HOMING_DONE 결과가 OK 아님 → INIT_ERROR", evs[-1][0] == "INIT_ERROR" and rn.reason == "HOMING_TIMEOUT")
lk, rn, evs, clk = homing_rig(); itel(rn, "IDLE")
ck("원점 대기: 장치가 IDLE(재부팅 등) → INIT_ERROR", evs[-1][0] == "INIT_ERROR" and rn.reason == "HOMING_LOST:IDLE")
lk, rn, evs, clk = homing_rig(); clk["t"] += 10000; rn.tick()
ok_nt = rn.step == "homing" and evs[-1][0] == "INIT_DONE"
lk, rn, evs, clk = homing_rig({"home_timeout_s": 5}); clk["t"] += 4; rn.tick(); ok_t1 = rn.step == "homing"
clk["t"] += 2; rn.tick()
ck("원점 대기: 시간 제한은 설정이 있을 때만(비어 있으면 검사 안 함, 10/12 실측 뒤)",
   ok_nt and ok_t1 and rn.reason == "HOME_TIMEOUT" and evs[-1][0] == "INIT_ERROR")
lk, rn, evs, clk = irig(); rn.start(); iack(rn, lk); lk.connected = False; rn.on_link(False)
w = rn.step == "wait_link"; lk.connected = True; rn.on_link(True)
ck("초기화: INIT_DONE 전 링크 끊김 → 다시 연결되면 PING 부터", w and cmds(lk) == ["PING", "GET_STATUS", "PING"], str(cmds(lk)))
lk, rn, evs, clk = irig(ready=False); rn.start(); n0 = len(lk.sent); rn.tick(); w0 = lk.sent == [] and rn.step == "wait_link"
lk.proto = "1.1"; rn.tick()
ck("초기화: 펌웨어 판 확인 전에는 보내지 않고 기다림, v1.1 확인 뒤 PING", n0 == 0 and w0 and cmds(lk) == ["PING"])
lk, rn, evs, clk = irig(ready=False); rn.start(); lk.proto = "1.0"; rn.tick()
ck("초기화: v1.1 이 아닌 펌웨어 → 기다리지 않고 INIT_ERROR(FW_VERSION:1.0), 명령 없음",
   lk.sent == [] and evs and evs[-1][0] == "INIT_ERROR" and rn.reason == "FW_VERSION:1.0", str(rn.reason))
lk, rn, evs, clk = irig(ready=False); rn.start(); lk.proto = "1.0"
_fm = {"code": "3-CM-001", "sev": 3, "why": "FW_NOT_V11:1.0"}
h0 = rn.handles(_fm); rn.on_error(_fm)
ck("초기화: 연결 대기 중 링크의 판 불일치 오류는 절차가 받아 INIT_ERROR(브리지의 안전정지 경로로 가지 않음)",
   h0 and rn.reason == "FW_VERSION:1.0" and not rn.handles(_fm))

# 상태머신과 함께: 원점 대기 중 오류는 X_MOVE 에서도 S10, 확인 뒤 S1 처음부터
fi = StateMachine(); lk, _, _, clk = irig()
rni = InitRunner(lk, fire=lambda e, p=None: fi.fire(e, p), clock=lambda: clk["t"])
rni.start(); iack(rni, lk); iack(rni, lk); itel(rni, "ARMED"); iack(rni, lk)
at_move = fi.node == "X_MOVE"
itel(rni, "ESTOP", estop=True)
ck("상태머신 연동: HOME ACK → X_MOVE, 원점 대기 중 비상정지 → S10(원인 init) → RECHECK → S1",
   at_move and fi.node == "S10" and fi.error_cause == "init" and fi.fire("RECHECK") and fi.node == "S1", fi.node)

# 링크: 정지 명령은 앞 명령의 ACK 를 기다리지 않고 바로 나간다
class _W(object):
    def __init__(self): self.out = []
    def write(self, b): self.out.append(P.decode(b)["cmd"])
lw = TeensyLink(port="/nonexistent"); lw._ser = _W(); lw.proto = "1.1"; lw.caps = set(["AID"]); lw.boot_id = 3
lw._aid = P.AidCounter(1); lw.connected = True
lw.send("PAUSE_CPR"); lw.send("STOP_CPR")
ck("링크: PAUSE_CPR 응답을 기다리지 않고 STOP_CPR 송신", lw._ser.out == ["PAUSE_CPR", "STOP_CPR"], str(lw._ser.out))

# 브리지 + 가상 포트(모의 Teensy v1.1, 시뮬레이션 설정)
import argparse, tempfile
sim6 = subprocess.Popen([sys.executable, "-u", os.path.join(SIMDIR, "fake_teensy.py"),
    "--port", "/tmp/ttyFAKE_INIT", "--hz", "20", "--seed", "6", "--quiet", "--time-scale", "0.05"],
    stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
time.sleep(1.2)
br = B.Bridge(argparse.Namespace(serial="/tmp/ttyFAKE_INIT", logdir=tempfile.mkdtemp(), spec=None, port=0))
sent6 = []
_orig_send = br.link.send
def _rec(cmd, **kw):
    sent6.append(cmd)
    return _orig_send(cmd, **kw)
br.link.send = _rec
br.link.start(); br.fsm.enter_initial()

def wait_for(cond, limit):
    end = time.monotonic() + limit
    while time.monotonic() < end and not cond():
        br.init.tick(); time.sleep(0.05)
    return cond()
ok6 = wait_for(lambda: br.fsm.node == "X_MOVE" and br.init.homed, 8.0)
ck("브리지: 부팅 → 장치 응답으로 S1 → X_MOVE, 원점 완료 확인",
   ok6 and "ARM" in sent6 and "HOME" in sent6 and sent6.index("ARM") < sent6.index("HOME")
   and any(l["from"] == "S1" and l["event"] == "INIT_DONE" for l in br.fsm.log), "%s %s" % (br.fsm.node, sent6))
sim6.stdin.write(b"estop\n"); sim6.stdin.flush(); time.sleep(0.5)
br.fsm.force("S10"); br.fsm.error_return = "S1"
del sent6[:]
br.fsm.fire("RECHECK")
ok6b = wait_for(lambda: br.fsm.node == "S10", 3.0)
ck("브리지: 비상정지가 눌린 채 다시 점검 → S10(원인 init), ARM 안 보냄",
   ok6b and br.fsm.error_cause == "init" and "ARM" not in sent6 and str(br.init.reason) == "ESTOP", "%s %s" % (br.init.reason, sent6))
sim6.stdin.write(b"ok\n"); sim6.stdin.flush(); time.sleep(0.4)
br.fsm.fire("RECHECK")
ck("브리지: 비상정지 해제 뒤 재확인 → 다시 X_MOVE", wait_for(lambda: br.fsm.node == "X_MOVE", 5.0), br.fsm.node)
# v2.5(D-47): 종료(S13)는 DISARM 한다. [대기로] 뒤 S1 에서 ARM, HOME 을 다시 보내 원점까지 확인해야 다음 탐색이 열린다
br.link.send("DISARM"); time.sleep(0.5)
br.fsm.force("S13")
del sent6[:]
br.fsm.fire("BACK_TO_IDLE")
ok6c = wait_for(lambda: br.fsm.node == "X_MOVE" and br.init.homed, 8.0)
ck("브리지: 종료 뒤 [대기로] → S1 초기화(ARM, HOME 다시) → X_MOVE",
   ok6c and "ARM" in sent6 and "HOME" in sent6 and sent6.index("ARM") < sent6.index("HOME"), "%s %s" % (br.fsm.node, sent6))
br.log_link()
with open(br.log.path) as _f:
    _txt = _f.read()
ck("브리지: 세션 기록에 링크 품질 줄(LINK link_up=1 ...)", "LINK link_up=1 link_down=0 bad_line=" in _txt)
br.link.stop(); sim6.terminate(); sim6.wait()
try: os.remove("/tmp/ttyFAKE_INIT")
except OSError: pass

print("\n== 11. 레이더 MR60BHA2 (radar.py) ==")
import struct as _st
import tempfile as _tf
from saver import radar as R

_fb = R.encode_frame(R.T_BREATH, _st.pack("<f", 14.5), fid=7)
ck("프레임 머리: SOF, ID, LEN, TYPE (big-endian)", _fb[:7] == bytes([0x01, 0x00, 0x07, 0x00, 0x04, 0x0A, 0x14]))
ck("체크섬: XOR 의 비트 반전", R.checksum(b"\x01\x02") == (~(0x01 ^ 0x02)) & 0xFF and R.checksum(b"") == 0xFF)
_g = R.FrameParser().feed(_fb)
ck("프레임 하나 해석, 호흡수 float32 little-endian", len(_g) == 1 and _g[0][0] == R.T_BREATH and _g[0][2] == 7
   and abs(R.decode(_g[0][0], _g[0][1])["rr_bpm"] - 14.5) < 1e-6)
_stream = (_fb + R.encode_frame(R.T_HEART, _st.pack("<f", 72.0))
           + R.encode_frame(R.T_DISTANCE, _st.pack("<If", 1, 29.4)))
_p, _acc = R.FrameParser(), []
for _i in range(len(_stream)):
    _acc += _p.feed(_stream[_i:_i + 1])
ck("한 바이트씩 받아도 프레임 3개", [f[0] for f in _acc] == [R.T_BREATH, R.T_HEART, R.T_DISTANCE])
_bh, _bd = bytearray(_fb), bytearray(_fb)
_bh[7] ^= 0xFF
_bd[-1] ^= 0xFF
_p = R.FrameParser()
_g = _p.feed(b"\x00\x13\x01\x02\xff" + bytes(_bh) + bytes(_bd) + b"\x01\x00" + _stream)
ck("쓰레기, 머리·데이터 체크섬 오류를 건너뛰고 뒤 프레임 3개 복구",
   [f[0] for f in _g] == [R.T_BREATH, R.T_HEART, R.T_DISTANCE] and _p.bad >= 2, "bad=%d" % _p.bad)
_g = R.FrameParser().feed(R.encode_frame(0x0A99, b"\x00" * 600) + _fb)
ck("LEN 상한(512)을 넘는 머리는 버리고 다음 프레임", [f[0] for f in _g] == [R.T_BREATH])
ck("값 0 은 측정 못 함(None, 0 을 쓰지 않음)", R.decode(R.T_HEART, _st.pack("<f", 0.0)) == {"hr_bpm": None})
ck("NaN, 음수도 측정 못 함", R.decode(R.T_BREATH, _st.pack("<f", float("nan")))["rr_bpm"] is None
   and R.decode(R.T_BREATH, _st.pack("<f", -3.0))["rr_bpm"] is None)
ck("거리 cm → mm, 유효 표시 0 이면 None",
   abs(R.decode(R.T_DISTANCE, _st.pack("<If", 1, 29.4))["distance_mm"] - 294.0) < 1e-3
   and R.decode(R.T_DISTANCE, _st.pack("<If", 0, 29.4))["distance_mm"] is None)
ck("사람 감지, 짧은 데이터, 모르는 TYPE",
   R.decode(R.T_PRESENCE, b"\x00\x00") == {"presence": False} and R.decode(R.T_PRESENCE, b"\x01\x00") == {"presence": True}
   and R.decode(R.T_HEART, b"\x00") == {} and R.decode(R.T_PHASE, b"\x00" * 12) == {})

_T = [100.0]
_rs = R.RadarState(clock=lambda: _T[0])
_rs.apply({"rr_bpm": 14.0, "hr_bpm": 72.0})
for _d in (294.0, 296.0, 290.0):
    _rs.apply({"distance_mm": _d})
_s = _rs.snapshot(500)
ck("값과 EAPD = 레이더 높이 - 거리 중앙값(500 - 294)", _s.get("rr_bpm") == 14.0 and _s.get("hr_bpm") == 72.0
   and _s.get("eapd_mm") == 206.0 and _s["receiving"] and _s["floor_set"], str(_s))
ck("radar_floor_mm 이 비면 EAPD 없음(대체 상수 없음)",
   "eapd_mm" not in _rs.snapshot(None) and "eapd_mm" not in _rs.snapshot(0) and not _rs.snapshot(None)["floor_set"])
ck("EAPD 0 이하(거리 >= 레이더 높이)면 없음", "eapd_mm" not in _rs.snapshot(250))
_rs2 = R.RadarState(clock=lambda: _T[0])
_rs2.apply({"distance_mm": 294.0})
_rs2.apply({"distance_mm": 295.0})
ck("거리 표본 3개 미만이면 EAPD 없음", "eapd_mm" not in _rs2.snapshot(500))
_T[0] += R.FRESH_S + 0.5
_s = _rs.snapshot(500)
ck("오래된 값(5 s 넘음)은 보내지 않음", not any(k in _s for k in ("rr_bpm", "hr_bpm", "distance_mm", "eapd_mm"))
   and not _s["receiving"], str(_s))
_rs.apply({"rr_bpm": 14.0, "distance_mm": 294.0})
_rs.apply({"presence": False})
ck("사람 없음이면 이전 값을 지움", not any(k in _rs.snapshot(500) for k in ("rr_bpm", "distance_mm")))
_rs.apply({"hr_bpm": 70.0})
_rs.apply({"hr_bpm": None})
ck("측정 못 함(0) 프레임은 이전 값을 지움", "hr_bpm" not in _rs.snapshot(500))

# 가상 포트: 실제 RadarLink 와 가짜 레이더 프로세스(깨진 바이트 섞음)
_rp = "/tmp/ttyRADAR_T"
_fr = subprocess.Popen([sys.executable, "-u", os.path.join(SIMDIR, "fake_radar.py"), "--port", _rp, "--hz", "10",
                        "--seed", "3", "--garbage", "--quiet", "--duration", "30"],
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
time.sleep(1.0)
_links = []
_rl = R.RadarLink(_rp, on_link=lambda c, p: _links.append(c)).start()
_end, _s = time.monotonic() + 8.0, {}
while time.monotonic() < _end:
    _s = _rl.snapshot(500)
    if all(k in _s for k in ("eapd_mm", "rr_bpm", "hr_bpm")):
        break
    time.sleep(0.1)
ck("가상 포트: 가짜 레이더 값 수신(깨진 바이트 섞임)", _s.get("receiving") and abs(_s.get("rr_bpm", 0) - 14) <= 1
   and abs(_s.get("hr_bpm", 0) - 72) <= 2 and abs(_s.get("distance_mm", 0) - 294) <= 4 and _links[:1] == [True], str(_s))
ck("가상 포트: EAPD = 500 - 거리 ≈ 206", abs(_s.get("eapd_mm", 0) - 206) <= 4, str(_s.get("eapd_mm")))
_rl.stop()                      # 한 포트를 둘이 읽으면 바이트를 나눠 가지므로 브리지 시험 전에 닫는다
# 열린 직후 SETTLE_S 는 해석하지 않는다(10/15 실기: 키트에 쌓였던 묵은 자료가 와서 첫 1초에 빠짐 2356)
_rl2 = R.RadarLink(_rp).start()
time.sleep(0.6)
_s0 = _rl2.snapshot()
time.sleep(1.2)
_s1 = _rl2.snapshot()
_rl2.stop()
ck("열린 직후 1초는 받기만 하고 해석하지 않음(묵은 자료), 그 뒤 프레임 해석",
   R.SETTLE_S == 1.0 and _s0["rx_bytes"] > 0 and _s0["frames"] == 0 and _s1["frames"] > 0,
   "0.6초 받음 %s 프레임 %s / 1.8초 프레임 %s" % (_s0["rx_bytes"], _s0["frames"], _s1["frames"]))

# 브리지: 레이더 EAPD 가 탐색에 쓰이고, 레이더가 없으면 시뮬레이션 값 또는 없음(대체 상수 없음)
from saver import bridge as B
_keep = dict((k, B.CONFIG.get(k)) for k in ("radar_floor_mm", "sim_eapd_mm"))
_tmpr = _tf.mkdtemp(prefix="saver_radar_")
class _A(object):
    pass
def _bargs(radar):
    a_ = _A()
    a_.logdir, a_.spec, a_.serial, a_.radar, a_.radar_baud = _tmpr, None, None, radar, 115200
    return a_
B.CONFIG.update({"radar_floor_mm": 500, "sim_eapd_mm": None})
_b = B.Bridge(_bargs(_rp))
_b.radar.start()
_end = time.monotonic() + 8.0
while time.monotonic() < _end and _b.current_eapd()[1] != "radar":
    time.sleep(0.1)
_e, _src = _b.current_eapd()
ck("브리지: 탐색 EAPD 는 레이더 값", _src == "radar" and abs(_e - 206) <= 4, "%s %s" % (_e, _src))
_b._radar_at = 0.0
_b.tick_radar()
_b.log.fp.flush()
_rows = open(_b.log.path, encoding="utf-8").read()
ck("브리지: 레이더 기록 줄(호흡, 심박 칸과 RADAR 거리, EAPD)", "radar" in _rows and "RADAR distance_mm=" in _rows
   and "eapd_mm=" in _rows)
_rsn = _b.radar_snapshot()
ck("브리지: UI 로 보내는 레이더 상태", _rsn["type"] == "radar" and _rsn["enabled"] and _rsn["connected"]
   and "rr_bpm" in _rsn, str(sorted(_rsn)))
B.CONFIG["radar_floor_mm"] = None
ck("브리지: radar_floor_mm 이 비면 레이더가 있어도 EAPD 없음", _b.current_eapd() == (None, None))
_fr.terminate()
_fr.wait()
_end = time.monotonic() + R.RX_ALIVE_S + 2.0
while time.monotonic() < _end and _b.radar.snapshot(500)["receiving"]:
    time.sleep(0.1)
ck("가상 포트: 레이더가 멈추면 '수신 중' 해제", not _b.radar.snapshot(500)["receiving"])
_b.radar.stop()
_b.log.close()
B.CONFIG.update({"sim_eapd_mm": 205.8})
_b2 = B.Bridge(_bargs(None))
ck("브리지: 레이더 없음 → 시뮬레이션 설정 EAPD, 레이더 상태는 enabled false",
   _b2.current_eapd() == (205.8, "sim") and _b2.radar_snapshot() == {"type": "radar", "enabled": False})
B.CONFIG.update({"sim_eapd_mm": None})
ck("브리지: 레이더도 시뮬레이션 값도 없으면 (None, None)", _b2.current_eapd() == (None, None))
_b2.log.close()
B.CONFIG.update(_keep)
try:
    os.remove(_rp)
except OSError:
    pass

# 현장 진단: 포트 없음, 열기 실패, 무수신, 프레임이 아닌 자료(키트 기본 펌웨어의 글자 출력)를 가린다
class _E(Exception):
    pass
_ea = _E("denied")
_ea.errno = 13
ck("진단: 열기 실패 이유(권한 없음, 사용 중)", R._open_error(_ea) == "권한 없음" and R._open_error(OSError(16, "busy")) == "다른 프로그램이 사용 중")
_rl0 = R.RadarLink("/tmp/no_such_radar_port").start()
_end = time.monotonic() + 3.0
while time.monotonic() < _end and _rl0.snapshot().get("error") != "없음":
    time.sleep(0.05)
_s0 = _rl0.snapshot()
_rl0.stop()
ck("진단: 없는 포트는 error '없음', 연결 아님, 받은 바이트 0", _s0.get("error") == "없음" and not _s0["connected"]
   and _s0["rx_bytes"] == 0, str(_s0))
_droot = _tf.mkdtemp(prefix="saver_dev_")
os.makedirs(os.path.join(_droot, "dev", "serial", "by-id"))
for _n in ("ttyACM0", "ttyACM1", "ttyTHS1"):
    open(os.path.join(_droot, "dev", _n), "w").close()
os.symlink("../../ttyACM0", os.path.join(_droot, "dev", "serial", "by-id", "usb-Espressif_USB_JTAG_serial_debug_unit_AA-if00"))
os.symlink("ttyACM1", os.path.join(_droot, "dev", "saver-teensy"))
_lp = dict(R.list_ports(_droot))
ck("진단: 직렬 포트 목록에 by-id 이름과 /dev/saver-* 별칭, 40핀 UART 표시",
   "Espressif" in _lp.get("/dev/ttyACM0", "") and "saver-teensy" in _lp.get("/dev/ttyACM1", "")
   and "40핀" in _lp.get("/dev/ttyTHS1", ""), str(_lp))
_txt = b"\x1b[0;36m[D][sensor:094]: 'Real-time heart rate': Sending state 72.00000 bpm\x1b[0m\r\n"
ck("진단: 글자 판별(색 코드 포함 글자는 참, 레이더 프레임은 거짓), 앞부분 보기",
   R.looks_text(_txt) and not R.looks_text(_fb * 4) and not R.looks_text(b"")
   and R.preview(b"AB\x01")[0] == "AB." and R.preview(b"\x01\xff")[1] == "01 ff")
_base = {"connected": True, "rx_bytes": 0, "frames": 0}
_keys = lambda s_, sample=b"", t=10.0: [k for k, _ in R.diagnose(s_, sample, t, "/dev/ttyUSB0")]
ck("진단: 안내 종류(포트 없음, 열기 실패, 무수신 5초, 글자 자료, 정상은 안내 없음)",
   _keys({"connected": False, "error": "없음"}) == ["missing"]
   and _keys({"connected": False, "error": "권한 없음"}) == ["open"]
   and _keys(dict(_base)) == ["silent"] and _keys(dict(_base), t=3.0) == []
   and [k for k, _ in R.diagnose(dict(_base), b"", 30.0, "/dev/x", since_open=1.0)] == []
   and _keys(dict(_base, rx_bytes=500), _txt) == ["noframe"]
   and _keys(dict(_base, rx_bytes=500, frames=20), _txt) == [])
_dn = R.diagnose(dict(_base, rx_bytes=500), _txt, 10.0, "/dev/saver-radar")[0][1]
_db = R.diagnose(dict(_base, rx_bytes=500), b"\x55\xaa\x00\x10" * 20, 10.0, "/dev/saver-radar")[0][1]
ck("진단: 글자면 전달 펌웨어 안내, 이진 자료면 속도 확인 안내", "passthrough" in " ".join(_dn) and "--baud" in " ".join(_db))
# 명령 줄 도구 전체 흐름(가짜 레이더 글자 모드와 프레임 모드, 없는 포트)
_rt = "/tmp/ttyRADAR_TXT_T"
_ft = subprocess.Popen([sys.executable, "-u", os.path.join(SIMDIR, "fake_radar.py"), "--port", _rt, "--text", "--hz", "10",
                        "--quiet", "--duration", "20"], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
time.sleep(1.0)
def _cli(port, secs="4"):
    p_ = subprocess.Popen([sys.executable, "-m", "saver.radar", port, "--seconds", secs],
                          cwd=os.path.dirname(SIMDIR), env=dict(os.environ, PYTHONIOENCODING="utf-8"),
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    return p_.communicate(timeout=30)[0].decode("utf-8", "replace"), p_.returncode
_ot, _rct = _cli(_rt)
_ft.terminate()
_ft.wait()
ck("saver-radar: 글자 자료면 '포트 열림', 받은 바이트, 프레임 0, 종류(esphome)와 전달 펌웨어 안내", _rct == 0
   and "포트 열림" in _ot and "프레임 0" in _ot and "종류: esphome" in _ot and "mr60bha2_passthrough" in _ot,
   _ot.strip().splitlines()[-1][:120] if _ot.strip() else "출력 없음")
_ff = subprocess.Popen([sys.executable, "-u", os.path.join(SIMDIR, "fake_radar.py"), "--port", _rt, "--hz", "10",
                        "--quiet", "--duration", "20"], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
time.sleep(1.0)
_of, _rcf = _cli(_rt, "3")
_ff.terminate()
_ff.wait()
ck("saver-radar: 레이더 프레임이면 값이 보이고 안내 없음", _rcf == 0 and "호흡 1" in _of and "  !" not in _of,
   _of.strip().splitlines()[-1][:120] if _of.strip() else "출력 없음")
_on, _rcn = _cli("/tmp/no_such_radar_port", "3")
ck("saver-radar: 없는 포트면 '포트 없음'과 지금 있는 직렬 포트 목록", _rcn == 0 and "포트 없음" in _on
   and "지금 있는 직렬 포트" in _on, _on.strip().splitlines()[-1][:120] if _on.strip() else "출력 없음")
import re as _re
_tj = b'{"t":"tel","ms":1503,"state":"IDLE","vac_kPa":-0.31,"v":1}\n' * 3
_kinds = [R.classify(x)[0] for x in (_txt, _tj, b"breath_rate: 14.00\nheart_rate: 72.00\n",
                                     b"ESP-ROM:esp32c6-20220919\r\nrst:0x15 (USB_UART_HPSYS)", b"hello world " * 8,
                                     b"\x55\xaa\x00\x10" * 20, b"\x01\x02\x03\xff" * 20)]
ck("진단: 자료 종류 판별(ESPHome 기록, Teensy JSON, Seeed 예제 글자, ESP32 부팅, 그 밖의 글자, 0x01 없는 이진, 이진)",
   _kinds == ["esphome", "teensy", "seeed_example", "boot", "text", "binary_nosof", "binary"], str(_kinds))
# 10/15 실기 --dump: XIAO 의 다른 펌웨어가 값을 쉼표로 나눈 줄로 1초에 한 줄(앞부분은 줄 중간부터 잘릴 수 있다)
_csv = b"74.00, 6.00, 10.00, NORMAL, LOW, 45.92\r\n70.00, 4.00, 15.00, NORMAL, LOW, 45.92\r\n"
ck("진단: 쉼표로 나눈 값 줄은 csv(줄 중간부터 받아도), 쉼표 없는 글자와 Teensy JSON 은 csv 아님",
   R.classify(_csv)[0] == "csv" and R.classify(b".00, NORMAL, LOW, 45.92\r\n" + _csv)[0] == "csv"
   and R.classify(b"hello world " * 8)[0] == "text" and R.classify(_tj)[0] == "teensy"
   and "passthrough" in R.classify(_csv)[1],
   str([R.classify(x)[0] for x in (_csv, b"hello world " * 8, _tj)]))
os.symlink("../../ttyACM1", os.path.join(_droot, "dev", "serial", "by-id", "usb-Teensyduino_USB_Serial_123-if00"))
_pi = R.port_identity(os.path.join(_droot, "dev", "saver-teensy"), _droot)
ck("진단: 포트 정체(별칭 → 실제 장치, by-id 이름으로 Teensy 확인)",
   _pi[0].endswith("/dev/ttyACM1") and _pi[1] == ["usb-Teensyduino_USB_Serial_123-if00"], str(_pi))
_ff2 = subprocess.Popen([sys.executable, "-u", os.path.join(SIMDIR, "fake_radar.py"), "--port", _rt, "--hz", "10",
                         "--quiet", "--duration", "20"], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
time.sleep(1.0)
_pd = subprocess.Popen([sys.executable, "-m", "saver.radar", _rt, "--dump", "--seconds", "1.5"],
                       cwd=os.path.dirname(SIMDIR), env=dict(os.environ, PYTHONIOENCODING="utf-8"),
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
_od = _pd.communicate(timeout=30)[0].decode("utf-8", "replace")
_ff2.terminate()
_ff2.wait()
ck("saver-radar --dump: 16진수 줄과 합계(레이더 프레임 수, 종류 radar)",
   _pd.returncode == 0 and _re.search(r"^000000  01 ", _od, _re.M) is not None and "종류: radar" in _od,
   _od.strip().splitlines()[-1][:120] if _od.strip() else "출력 없음")
ck("saver-radar --dump: 다 받은 뒤 출력, 프레임 종류별 개수와 번호 빠짐 0(가짜 레이더는 ID 를 1씩 올림)",
   "받는 중" in _od and "프레임 종류: " in _od and "0A14 " in _od and _re.search(r"처음 1초 뒤: 레이더 프레임 [1-9]\d*개, 번호 빠짐 0개", _od),
   " / ".join(l for l in _od.splitlines() if l.startswith(("프레임 종류", "처음")))[:160])
# 10/15 실기 덤프 앞부분(레이더 부팅 글자와 프레임 50개. 호흡, 심박, 거리 값은 모두 0 인 구간만 담았다)
_real = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "data", "mr60bha2_boot_1015.bin"), "rb").read()
_rp2 = R.FrameParser()
_rf = _rp2.feed(_real)
_rtypes = set(t for t, _d, _i in _rf)
_rvals = [R.decode(t, d) for t, d, _i in _rf]
ck("실기 레이더 바이트(10/15): 부팅 글자는 건너뛰고 프레임 50개, 깨짐 0, 빠짐 0, 알려지지 않은 종류(0100 글자, 0A29, 0A17)는 넘김",
   len(_rf) == 50 and _rp2.bad == 0 and _rp2.lost == 0 and _rp2.skipped > 0
   and set([0x0100, 0x0A29, 0x0A17, 0x0A16, 0x0F09, 0x0A13, 0x0A14, 0x0A15]) <= _rtypes
   and b"Andar ADT6101P" in _rf[0][1],
   "프레임 %d 깨짐 %d 빠짐 %d 종류 %s" % (len(_rf), _rp2.bad, _rp2.lost, sorted("%04X" % t for t in _rtypes)))
ck("실기 레이더 바이트(10/15): 값 0 프레임은 측정 못 함(비움), 사람 감지 0 은 없음",
   all(v.get(k) is None for v in _rvals for k in ("rr_bpm", "hr_bpm", "distance_mm"))
   and [v["presence"] for v in _rvals if "presence" in v] and not any(v.get("presence") for v in _rvals), "")
# 실기 거리 프레임 하나(ID 0x0035): 유효 표시 1, float32 51.66 cm
_dfr = R.FrameParser().feed(bytes.fromhex("01003500080a16df01000000d7a34e4286"))
ck("실기 거리 프레임(10/15 원본 바이트) → 516.6 mm",
   len(_dfr) == 1 and abs(R.decode(_dfr[0][0], _dfr[0][1])["distance_mm"] - 516.6) < 0.1, str(_dfr))
# 번호 빠짐: ID 1, 2, 3, 10 이면 6개, 다시 0 부터(레이더 재시작)는 세지 않음
_lp2 = R.FrameParser()
_lp2.feed(b"".join(R.encode_frame(R.T_PRESENCE, b"\x00\x00", i) for i in (1, 2, 3, 10, 0, 1)))
ck("번호 빠짐: 건너뛴 ID 수만 센다(되돌아감은 제외)", _lp2.lost == 6 and _lp2.frames == 6, str(_lp2.lost))
ck("saver-radar: 연결되면 포트 정보 줄", "포트 정보: " + _rt in _ot, "")
# 끊김과 다시 연결: 끊기면 포트 목록을 다시, 다시 연결되면 포트 정보와 종류 안내를 다시 보인다
def _fake_txt(dur):
    return subprocess.Popen([sys.executable, "-u", os.path.join(SIMDIR, "fake_radar.py"), "--port", _rt, "--text",
                             "--hz", "10", "--quiet", "--duration", dur], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
_f1 = _fake_txt("6")
time.sleep(1.0)
_pc = subprocess.Popen([sys.executable, "-m", "saver.radar", _rt, "--seconds", "16"], cwd=os.path.dirname(SIMDIR),
                       env=dict(os.environ, PYTHONIOENCODING="utf-8"), stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
_f1.wait()
time.sleep(2.0)
_f2 = _fake_txt("12")
_oc = _pc.communicate(timeout=40)[0].decode("utf-8", "replace")
_f2.terminate()
_f2.wait()
_i1 = _oc.find("포트 정보: ")
ck("saver-radar: 끊기면 포트 목록을 다시, 다시 연결되면 포트 정보와 종류 안내를 다시",
   _oc.count("포트 정보: ") == 2 and _oc.count("레이더 프레임이 없습니다(종류: esphome)") == 2
   and _i1 >= 0 and _oc.find("가 없습니다", _i1) > _i1,
   "포트 정보 %d, 종류 안내 %d, 끊긴 뒤 목록 %s" % (_oc.count("포트 정보: "), _oc.count("종류: esphome"),
                                           _oc.find("가 없습니다", max(_i1, 0)) > _i1))
import shutil as _shr
_shr.rmtree(_droot, ignore_errors=True)
for _p in (_rt,):
    try:
        os.remove(_p)
    except OSError:
        pass

print("\n== 12. 젯슨 냉각 팬 (setup/saver_fan.py, 가짜 sysfs) ==")
import importlib.util as _ilu
import shutil as _sh
import signal as _sg
_fanpy = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "setup", "saver_fan.py")
_spec = _ilu.spec_from_file_location("saver_fan", _fanpy)
F = _ilu.module_from_spec(_spec)
_spec.loader.exec_module(F)
_fan_tmp = []

def _fanroot(zones, tc=None, pwm="0"):
    """가짜 sysfs: pwm-fan(target_pwm, cur_pwm, 선택 temp_control)과 thermal_zone0.. (종류, 밀리도)."""
    r_ = _tf.mkdtemp(prefix="saver_fan_")
    _fan_tmp.append(r_)
    d_ = os.path.join(r_, "sys", "devices", "pwm-fan")
    os.makedirs(d_)
    files_ = [("target_pwm", pwm), ("cur_pwm", pwm)] + ([("temp_control", tc)] if tc is not None else [])
    for f_, v_ in files_:
        with open(os.path.join(d_, f_), "w") as fp_:
            fp_.write(v_ + "\n")
    for i_, (kind_, milli_) in enumerate(zones):
        z_ = os.path.join(r_, "sys", "devices", "virtual", "thermal", "thermal_zone%d" % i_)
        os.makedirs(z_)
        with open(os.path.join(z_, "type"), "w") as fp_:
            fp_.write(kind_ + "\n")
        with open(os.path.join(z_, "temp"), "w") as fp_:
            fp_.write(str(milli_) + "\n")
    return r_

def _fread(r_, name):
    try:
        with open(os.path.join(r_, "sys", "devices", "pwm-fan", name)) as fp_:
            return fp_.read().strip()
    except OSError:
        return None

def _zone_temp(r_, i_, milli_):
    with open(os.path.join(r_, "sys", "devices", "virtual", "thermal", "thermal_zone%d" % i_, "temp"), "w") as fp_:
        fp_.write(str(milli_) + "\n")

def _wait_pwm(r_, want, limit=5.0):
    end_ = time.monotonic() + limit
    while time.monotonic() < end_:
        if _fread(r_, "target_pwm") == want:
            return True
        time.sleep(0.05)
    return False

def _fan_proc(r_, cfg_, extra=()):
    # 로캘 C, 출력 인코딩 ASCII 에서도 한글 기록 때문에 멈추지 않는지 함께 본다(서비스 환경)
    env_ = dict(os.environ, LC_ALL="C", LANG="C", PYTHONIOENCODING="ascii")
    return subprocess.Popen([sys.executable, "-u", _fanpy, "--root", r_, "--config", cfg_] + list(extra), env=env_,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)

ck("곡선: 20 °C 이하 0, 50 °C 이상 255, 사이는 비례(35 °C → 127)",
   [F.fan_curve(t_, 20.0, 50.0) for t_ in (10, 20, 35, 50, 70)] == [0, 0, 127, 255, 255])
ck("곡선: 온도 모름(None, NaN)이나 뒤집힌 설정이면 최대 255",
   F.fan_curve(None, 20.0, 50.0) == 255 and F.fan_curve(float("nan"), 20.0, 50.0) == 255
   and F.fan_curve(30.0, 50.0, 20.0) == 255)
_NANO = [("AO-therm", 38000), ("CPU-therm", 41500), ("GPU-therm", 43000), ("PLL-therm", 39000),
         ("PMIC-Die", 100000), ("thermal-fan-est", 40500)]
_r1 = _fanroot(_NANO + [("broken", "abc")], tc="1")
_z = F.read_zones(_r1)
ck("온도: CPU, GPU 최댓값(43.0 °C). PMIC 고정값 100 °C 는 판단에서 빼고, 읽을 수 없는 영역은 버림",
   F.pick_temp(_z) == 43.0 and len(_z) == 6, str(_z))
ck("온도: CPU, GPU 영역이 없으면 thermal_zone0, 아무것도 없으면 None",
   F.pick_temp([("thermal_zone0", "AO-therm", 37.0), ("thermal_zone4", "PMIC-Die", 100.0)]) == 37.0
   and F.pick_temp([]) is None)

_cdir = _tf.mkdtemp(prefix="saver_fancfg_")
_fan_tmp.append(_cdir)
def _cfg(name, text):
    p_ = os.path.join(_cdir, name)
    with open(p_, "w", encoding="utf-8") as fp_:
        fp_.write(text)
    return p_
_nocfg = os.path.join(_cdir, "none.json")
_c, _bad = F.load_config(_nocfg)
ck("설정: 파일이 없으면 기본값 20 °C, 50 °C, 2 s", _c == F.DEFAULTS and _bad == [])
_c, _bad = F.load_config(_cfg("a.json", u'{"_설명": "x", "fan_off_temp_C": 30, "fan_max_temp_C": 60, "interval_s": 1}'))
ck("설정: 파일 값을 쓰고 설명 칸은 무시",
   (_c["fan_off_temp_C"], _c["fan_max_temp_C"], _c["interval_s"]) == (30.0, 60.0, 1.0) and _bad == [])
_c, _bad = F.load_config(_cfg("b.json", '{"fan_off_temp_C": "hot", "fan_max_temp_C": 10, "interval_s": 0}'))
ck("설정: 잘못된 항목(글자, 뒤집힌 온도, 범위 밖 주기)은 기본값, 무시한 항목 3개를 알림",
   _c == F.DEFAULTS and len(_bad) == 3, str(_bad))
_c, _bad = F.load_config(_cfg("c.json", '{"fan_off_temp_C": 2'))
_c2, _bad2 = F.load_config(_cfg("d.json", "[20, 50]"))
ck("설정: 깨진 JSON, 객체가 아닌 JSON 은 모두 기본값", _c == F.DEFAULTS and _bad and _c2 == F.DEFAULTS and _bad2)

_fc = F.FanControl(_r1, _nocfg)
_st = _fc.step()
ck("한 주기: 43 °C → PWM 195 를 target_pwm 에 씀", _st[1] == 195 and _fread(_r1, "target_pwm") == "195")
with open(os.path.join(_r1, "sys", "devices", "pwm-fan", "target_pwm"), "w") as _f:
    _f.write("7\n")
_po = _fan_proc(_r1, _nocfg, ["--show"])
_so = _po.communicate(timeout=10)[0].decode("utf-8", "replace")
ck("--show: 온도, PWM, 설정 3줄만 보이고 쓰지 않음(로캘 C)", _po.returncode == 0 and _fread(_r1, "target_pwm") == "7"
   and "곡선 195" in _so and "커널 자동 제어 켜짐" in _so and len(_so.strip().splitlines()) == 3, _so.strip()[-80:])

_r2 = _fanroot(_NANO, tc="1")
_svc = _cfg("svc.json", '{"interval_s": 0.5}')
_p2 = _fan_proc(_r2, _svc)
ck("서비스: 커널 자동 제어를 끄고(temp_control 0) 43 °C → PWM 195",
   _wait_pwm(_r2, "195") and _fread(_r2, "temp_control") == "0", "%s %s" % (_fread(_r2, "target_pwm"), _fread(_r2, "temp_control")))
_zone_temp(_r2, 1, 61000)
ck("서비스: CPU 61 °C 로 오르면 다음 주기에 255", _wait_pwm(_r2, "255"))
_zone_temp(_r2, 1, 41500)
_cfg("svc.json", '{"interval_s": 0.5, "fan_max_temp_C": 70}')
ck("서비스: 설정을 고치면 재시작 없이 반영(43 °C, 20~70 °C → 117)", _wait_pwm(_r2, "117"))
for _i in (1, 2):
    _zone_temp(_r2, _i, "x")
ck("서비스: CPU, GPU 온도를 못 읽으면 thermal_zone0 기준(38 °C, 20~70 °C → 91)", _wait_pwm(_r2, "91"))
for _i in range(len(_NANO)):
    _zone_temp(_r2, _i, "x")
ck("서비스: 온도를 하나도 못 읽으면 최대 255", _wait_pwm(_r2, "255"))
_p2.send_signal(_sg.SIGTERM)
try:
    _rc2 = _p2.wait(timeout=5)
except subprocess.TimeoutExpired:
    _p2.kill(); _rc2 = _p2.wait()
_o2 = _p2.stdout.read().decode("utf-8", "replace")
ck("서비스: SIGTERM 으로 끝나면 커널 자동 제어를 되돌림(temp_control 1), 종료 코드 0",
   _rc2 == 0 and _fread(_r2, "temp_control") == "1", "rc=%s tc=%s" % (_rc2, _fread(_r2, "temp_control")))
ck("서비스: 로캘 C 에서도 한글 기록(온도 → PWM)", u"43.0 °C → PWM 195" in _o2 and u"모름(최대) → PWM 255" in _o2,
   _o2.strip().splitlines()[-1] if _o2.strip() else "출력 없음")

_r3 = _fanroot(_NANO)
_p3 = _fan_proc(_r3, _nocfg)
_ok3 = _wait_pwm(_r3, "195")
_p3.send_signal(_sg.SIGTERM)
_p3.wait(timeout=5)
ck("서비스: 커널 자동 제어(temp_control)가 없는 장치면 끝날 때 최대 255", _ok3 and _fread(_r3, "target_pwm") == "255")
_r4 = _fanroot(_NANO, tc="0", pwm="40")
_r5 = _fanroot(_NANO, pwm="40")
_rc45 = [subprocess.call([sys.executable, _fanpy, "--root", r_, "--release"]) for r_ in (_r4, _r5)]
ck("--release(서비스가 죽은 뒤 ExecStopPost): 커널 자동 제어 켬, 그 기능이 없으면 255",
   _rc45 == [0, 0] and _fread(_r4, "temp_control") == "1" and _fread(_r5, "target_pwm") == "255")
_r6 = _tf.mkdtemp(prefix="saver_fan_none_")
_fan_tmp.append(_r6)
_t6 = time.monotonic()
_p6 = _fan_proc(_r6, _nocfg, ["--wait-s", "1.5"])
_o6 = _p6.communicate(timeout=10)[0].decode("utf-8", "replace")
ck("팬 PWM 파일이 없으면(젯슨 나노가 아님) 잠시(--wait-s) 기다린 뒤 0 으로 끝나 재시작을 되풀이하지 않고 아무것도 만들지 않음",
   _p6.returncode == 0 and not os.path.exists(os.path.join(_r6, "sys")) and 1.0 <= time.monotonic() - _t6 < 8.0, _o6.strip())
for _d in _fan_tmp:
    _sh.rmtree(_d, ignore_errors=True)

print("\n== 정적 UI 서빙 (--serve, 젯슨 Python 3.6 경로 포함) ==")
import socket, tempfile, urllib.request, urllib.error
from http.server import HTTPServer
def _free_port():
    s_ = socket.socket(); s_.bind(("127.0.0.1", 0)); p_ = s_.getsockname()[1]; s_.close(); return p_
def _get(port, path):
    try:
        r_ = urllib.request.urlopen("http://127.0.0.1:%d%s" % (port, path), timeout=3)
        return r_.getcode(), r_.read()
    except urllib.error.HTTPError as e_:
        return e_.code, b""
    except Exception as e_:
        return None, str(e_).encode()
_top = tempfile.mkdtemp(); _ui = os.path.join(_top, "ui"); os.makedirs(os.path.join(_ui, "assets"))
with open(os.path.join(_ui, "index.html"), "w") as _f: _f.write("<p>saver-ui</p>")
with open(os.path.join(_ui, "assets", "a.js"), "w") as _f: _f.write("var a=1;")
with open(os.path.join(_top, "secret.txt"), "w") as _f: _f.write("secret")
_cwd = os.getcwd()
_p1 = _free_port(); B.serve_static(_ui, _p1); time.sleep(0.2)
_c1, _b1 = _get(_p1, "/")
ck("정적 UI: / 요청에 index.html, 작업 폴더 그대로(로그 경로 보존)", _c1 == 200 and b"saver-ui" in _b1 and os.getcwd() == _cwd,
   "%s %s" % (_c1, _b1[:40]))
_srv36 = HTTPServer(("127.0.0.1", 0), B.static_handler(_ui, legacy=True))
threading.Thread(target=_srv36.serve_forever, daemon=True).start()
_p2 = _srv36.server_address[1]
_c2, _b2 = _get(_p2, "/"); _c3, _b3 = _get(_p2, "/assets/a.js"); _c4, _ = _get(_p2, "/../secret.txt")
ck("정적 UI(3.6 처리기): /, /assets 응답, 상위 폴더 접근 404, 작업 폴더 그대로",
   _c2 == 200 and b"saver-ui" in _b2 and _c3 == 200 and _b3 == b"var a=1;" and _c4 == 404 and os.getcwd() == _cwd,
   "%s %s %s" % (_c2, _c3, _c4))
_srv36.shutdown()

print("\n== 레이더 시험 화면 (saver-radar --ui, radar_view.py) ==")
from saver import radar_view as RV
ck("거리 중앙값: 홀수, 짝수, 표본 부족(3개 미만), 5초 넘은 표본 제외",
   R.distance_median([(0, 300.0), (1, 310.0), (2, 290.0)], 2.0) == 300.0
   and R.distance_median([(0, 300.0), (1, 310.0), (2, 290.0), (2, 320.0)], 2.0) == 305.0
   and R.distance_median([(0, 300.0), (1, 310.0)], 2.0) is None
   and R.distance_median([(0, 300.0), (10, 310.0), (11, 290.0), (12, 320.0)], 12.0) == 310.0)
_vh = RV.History(seconds=3)
for _i in range(5):
    _vh.add(1760500000.0 + _i, {"rr_bpm": 14.0, "hr_bpm": None, "presence": True, "distance_mm": 294.0, "eapd_mm": 206.0})
_vr = _vh.rows()
ck("시험 화면 기록: 최근 것만 남기고 거리, EAPD 는 cm, 측정 못 한 값은 None",
   len(_vr) == 3 and _vr[-1] == [1760500004.0, 14.0, None, True, 29.4, 20.6], str(_vr[-1]))
_vc = RV.to_csv(_vr).decode("utf-8").splitlines()
ck("시험 화면 CSV: 머리줄, UTC ISO 8601 시각, 빈 값은 비움, 사람 1/0",
   _vc[0] == "time_utc,rr_bpm,hr_bpm,distance_cm,eapd_cm,presence" and len(_vc) == 4
   and _vc[-1] == time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime(1760500004)) + ",14.0,,29.4,20.6,1", _vc[-1])
class _VL(object):
    port = "/dev/saver-radar"
    def __init__(self, s):
        self.s = s
    def snapshot(self, floor_mm=None):
        return dict(self.s)
_vs = RV.state(_VL({"connected": True, "receiving": True, "presence": True, "distance_mm": 294.0,
                    "distance_med_mm": 295.0, "eapd_mm": 205.0, "frames": 9}), _vh, 500, now=1760500010.0)
ck("시험 화면 JSON: 거리, 중앙값, EAPD, 높이는 cm, 중앙값 구간 길이와 기록, 측정 못 한 값은 None",
   _vs["distance_cm"] == 29.4 and _vs["distance_med_cm"] == 29.5 and _vs["eapd_cm"] == 20.5 and _vs["floor_cm"] == 50.0
   and _vs["eapd_window_s"] == R.EAPD_WINDOW_S and len(_vs["history"]) == 3 and _vs["rr_bpm"] is None
   and _vs["connected"] and _vs["frames"] == 9 and _vs["now"] == 1760500010.0, str(_vs)[:160])
ck("시험 화면 JSON: 높이가 없거나 0 이하이면 floor_cm 없음, 연결 정보가 없으면 끊김",
   all(RV.state(_VL({}), _vh, f_)["floor_cm"] is None for f_ in (None, 0, -5))
   and RV.state(_VL({}), _vh)["connected"] is False)
# 실제 실행: 가짜 레이더 + python -m saver.radar PORT --ui (브라우저 없이 HTTP 로 확인)
_ru = "/tmp/ttyRADAR_UI_T"
_fu = subprocess.Popen([sys.executable, "-u", os.path.join(SIMDIR, "fake_radar.py"), "--port", _ru, "--hz", "10",
                        "--quiet", "--duration", "25"], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
time.sleep(1.0)
_pu = _free_port()
# --open 도 함께: PATH 를 빈 폴더로 둬 Chromium 을 찾지 못하게 한다(젯슨에서 돌려도 화면에 창을 띄우지 않는다)
_vpath = _tf.mkdtemp(prefix="saver_nopath_")
_pv = subprocess.Popen([sys.executable, "-m", "saver.radar", _ru, "--ui", "--http-port", str(_pu), "--seconds", "9",
                        "--floor", "500", "--open"], cwd=os.path.dirname(SIMDIR),
                       env=dict(os.environ, PYTHONIOENCODING="utf-8", PATH=_vpath),
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
_vj, _end = {}, time.monotonic() + 8.0
while time.monotonic() < _end:
    _c, _b = _get(_pu, "/radar.json")
    if _c == 200:
        _vj = json.loads(_b.decode("utf-8"))
        if all(_vj.get(k) is not None for k in ("rr_bpm", "hr_bpm", "distance_cm", "distance_med_cm", "eapd_cm")) \
                and len(_vj.get("history", [])) >= 2:
            break
    time.sleep(0.3)
_ch, _bh = _get(_pu, "/")
_cc, _bc = _get(_pu, "/radar.csv")
_cn, _ = _get(_pu, "/nope")
_ov = _pv.communicate(timeout=30)[0].decode("utf-8", "replace")
_fu.terminate()
_fu.wait()
ck("saver-radar --ui: 화면(/)에 호흡수, 심박수, 거리, EAPD 칸", _ch == 200 and all(w_.encode("utf-8") in _bh
   for w_ in ("호흡수", "심박수", "거리", "EAPD", "radar.json")), str(_ch))
ck("saver-radar --ui: /radar.json 값(가짜 레이더 호흡 14, 심박 72, 거리 29.4 cm, 높이 50 cm → EAPD ≈ 20.6 cm)",
   _vj.get("connected") and _vj.get("receiving") and abs(_vj.get("rr_bpm", 0) - 14) <= 1
   and abs(_vj.get("hr_bpm", 0) - 72) <= 2 and abs(_vj.get("distance_cm", 0) - 29.4) <= 0.4
   and abs(_vj.get("distance_med_cm", 0) - 29.4) <= 0.4 and abs(_vj.get("eapd_cm", 0) - 20.6) <= 0.4
   and _vj.get("floor_cm") == 50.0 and len(_vj.get("history", [])) >= 2,
   str(dict((k, _vj.get(k)) for k in ("rr_bpm", "hr_bpm", "distance_cm", "eapd_cm", "floor_cm"))))
_vcl = _bc.decode("utf-8").splitlines()
ck("saver-radar --ui: /radar.csv 머리줄과 1초 기록, 없는 주소는 404",
   _cc == 200 and _vcl[:1] == ["time_utc,rr_bpm,hr_bpm,distance_cm,eapd_cm,presence"] and len(_vcl) >= 3
   and _vcl[-1].endswith(",1") and _cn == 404, "%s %d줄 %s" % (_cc, len(_vcl), _cn))
ck("saver-radar --ui --open: 주소 안내, Chromium 이 없으면 안내만, 터미널 상태 줄(거리, EAPD cm), --seconds 뒤 정상 종료",
   _pv.returncode == 0 and ("레이더 시험 화면: http://127.0.0.1:%d" % _pu) in _ov and "Chromium 을 찾지 못했습니다" in _ov
   and _re.search(r"거리 2\d\.\d+ cm EAPD 2\d\.\d+ cm 사람 있음", _ov) is not None,
   _ov.strip().splitlines()[-1][:120] if _ov.strip() else "출력 없음")
_busy = socket.socket()
_busy.bind(("127.0.0.1", 0))
_busy.listen(1)
_pz = subprocess.Popen([sys.executable, "-m", "saver.radar", "/tmp/no_such_radar_port", "--ui", "--http-port",
                        str(_busy.getsockname()[1]), "--seconds", "2"], cwd=os.path.dirname(SIMDIR),
                       env=dict(os.environ, PYTHONIOENCODING="utf-8"), stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
_oz = _pz.communicate(timeout=30)[0].decode("utf-8", "replace")
_busy.close()
ck("saver-radar --ui: 화면 포트를 이미 쓰고 있으면 이유와 --http-port 안내, 종료 코드 1",
   _pz.returncode == 1 and "화면 서버를 열지 못했습니다" in _oz and "--http-port" in _oz,
   _oz.strip().splitlines()[-1][:120] if _oz.strip() else "출력 없음")
try:
    os.remove(_ru)
except OSError:
    pass
os.rmdir(_vpath)

print("\n== 브리지 기동 (python -m saver.bridge, 이 Python %d.%d) ==" % sys.version_info[:2])
# 노트북 대체 실행(새 Python)에서도 main() 이 뜨는지 본다(3.14 는 get_event_loop() 가 RuntimeError).
# --serial 에 없는 경로를 줘 실제 장치를 찾지 않는다. 젯슨에서 saver-test 를 돌릴 때 Teensy 가 꽂혀 있어도
# 이 시험이 ARM, HOME 을 보내지 않게 한다
_jdir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
_tmpb = tempfile.mkdtemp()
_pb = _free_port()
_bp = subprocess.Popen([sys.executable, "-u", "-m", "saver.bridge", "--port", str(_pb),
                        "--serial", os.path.join(_tmpb, "no_such_tty"), "--logdir", os.path.join(_tmpb, "logs")],
                       cwd=_jdir, env=dict(os.environ, PYTHONIOENCODING="utf-8"),
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
_bl = []
def _rd():
    for _ln in iter(_bp.stdout.readline, b""):
        _bl.append(_ln.decode("utf-8", "replace").rstrip())
threading.Thread(target=_rd, daemon=True).start()
_up = False
_t0 = time.monotonic()
try:
    while time.monotonic() - _t0 < 10.0 and _bp.poll() is None:
        if any(l_.startswith("[ws]") for l_ in _bl):
            try:
                socket.create_connection(("127.0.0.1", _pb), timeout=0.5).close()
                _up = True
                break
            except OSError:
                pass
        time.sleep(0.1)
finally:
    _bp.terminate()
    try:
        _bp.wait(timeout=5)
    except subprocess.TimeoutExpired:
        _bp.kill(); _bp.wait()
_last = [l_ for l_ in _bl if l_.strip()]
ck("브리지 기동: WebSocket 접속을 받음, 없는 시리얼 경로라 실제 장치는 찾지 않음", _up,
   _last[-1] if _last else "출력 없음")

# 기본 인코딩이 UTF-8 이 아닌 환경(한국어 Windows 는 cp949)에서도 상태머신, 설정을 읽고 기록을 쓰는지 본다.
# 리눅스에서는 C 로캘에 UTF-8 모드를 끄면 같은 조건이 된다(3.6 은 C 로캘이면 그대로 ASCII)
_probe = ("import sys, os, locale\n"
          "sys.path.insert(0, sys.argv[1])\n"
          "from saver.state_machine import StateMachine\n"
          "from saver.logger import CsvLogger\n"
          "from saver import bridge as B\n"
          "StateMachine(on_command=lambda c: None)\n"
          "B.load_config(os.path.join(sys.argv[1], 'config.sim.json'))\n"
          "lg = CsvLogger(sys.argv[2]); lg.event('sys', u'\\uc2dc\\ud5d8'); lg.close()\n"
          "print('ENC_OK ' + locale.getpreferredencoding(False))\n")
_envc = dict(os.environ, LC_ALL="C", LANG="C", PYTHONUTF8="0", PYTHONCOERCECLOCALE="0", PYTHONIOENCODING="utf-8")
_pc = subprocess.Popen([sys.executable, "-c", _probe, _jdir, os.path.join(_tmpb, "enc_logs")], cwd=_jdir, env=_envc,
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
_oc = _pc.communicate(timeout=30)[0].decode("utf-8", "replace")
_lc = [l_ for l_ in _oc.splitlines() if l_.strip()]
ck("파일 인코딩: UTF-8 아닌 기본 인코딩에서 상태머신, 설정 읽기와 한글 기록 쓰기",
   _pc.returncode == 0 and "ENC_OK" in _oc, _lc[-1] if _lc else "출력 없음")

print("\n== 14. 결선 시연 모드: 유닛별 시연 (unit_demo.py, D-51) ==")
from saver.unit_demo import UnitDemo, STEP_TIMEOUT_S, STATUS_TEL_S


class _UL(object):
    """유닛 시연 시험용 링크. 명령을 기록하고 seq 를 돌려준다. 장치 응답은 시험이 넣는다."""
    def __init__(self, proto="1.1"):
        self.connected, self.proto, self.caps, self.mcu_state = True, proto, set(), None
        self.sent, self.n = [], 500
    def send(self, cmd, **kw):
        self.n += 1
        self.sent.append((cmd, kw, self.n))
        return self.n


class _SR(object):
    """탐색 모듈 자리. start, cancel 만 기록한다."""
    def __init__(self):
        self.calls, self.active = [], False
    def start(self, eapd):
        self.calls.append(("start", eapd)); self.active = True
    def cancel(self, reason):
        self.calls.append(("cancel", reason)); self.active = False


def urig(cfg=None, proto="1.1", eapd=None):
    lk, sr, clk, pushed = _UL(proto), _SR(), {"t": 0.0}, []
    c = {"target_bpm": 110, "target_depth_mm": 0, "force_max_N": 0, "vacuum_target_kPa": 0}
    c.update(cfg or {})
    ud = UnitDemo(lk, c, search=sr, eapd=lambda: eapd, push=pushed.append, clock=lambda: clk["t"])
    return lk, ud, sr, clk, pushed


def ucmds(lk):
    return [c for c, _, _ in lk.sent]


def uack(ud, lk, ok=True, why=None):
    ud.on_ack({"t": "ack", "seq": lk.sent[-1][2], "ok": ok, "err": "" if ok else "2-CU-011", "why": why or ""})


def utel(ud, lk, state, estop=False, faults=None):
    lk.mcu_state = state
    ud.on_tel({"t": "tel", "state": state, "estop": estop, "faults": faults or []})


CP = {"target_depth_mm": 50, "force_max_N": 600}


def cp_running(cfg=CP, state="IDLE"):
    """압박 시연을 running 까지 진행한 rig"""
    lk, ud, sr, clk, pushed = urig(cfg)
    ud.request("compress", "enter"); ud.request("compress", "start"); uack(ud, lk); utel(ud, lk, state)
    if state == "IDLE":
        uack(ud, lk)                                 # ARM
    if state in ("IDLE", "ARMED"):
        uack(ud, lk); utel(ud, lk, "ARMED"); ud.on_event({"evt": "HOMING_DONE", "result": "OK"})   # HOME
    uack(ud, lk)                                     # SET_TARGET
    uack(ud, lk)                                     # START_CPR
    utel(ud, lk, "CPR_RUNNING")
    return lk, ud, sr, clk, pushed


lk, ud, sr, clk, pushed = urig(CP)
r_en = ud.request("compress", "enter")
ck("시연 모드: 화면에 들어가기만 하면 명령 없음(자동 시작 없음), 화면 상태는 UI 로 알림",
   r_en == (True, None) and lk.sent == [] and ud.phase == "idle" and pushed and pushed[-1]["screen"] == "compress"
   and pushed[-1]["type"] == "unit", str(pushed[-1:]))
lk, ud, sr, clk, pushed = urig(CP)
ck("시연 모드: 보고 있지 않은 유닛은 시작 거부(NOT_ON_SCREEN), 명령 없음",
   ud.request("compress", "start") == (False, "NOT_ON_SCREEN") and lk.sent == [])
lk, ud, sr, clk, pushed = urig()
ud.request("compress", "enter")
ck("압박: 목표 깊이가 비어 있으면 아무 명령 없이 실패(TARGET_NOT_SET, 원점 복귀도 안 함)",
   ud.request("compress", "start") == (True, None) and ud.phase == "failed" and ud.reason == "TARGET_NOT_SET"
   and lk.sent == [], str(ucmds(lk)))

lk, ud, sr, clk, pushed = urig(CP)
ud.request("compress", "enter"); ud.request("compress", "start")
a1 = ucmds(lk) == ["GET_STATUS"]
uack(ud, lk); utel(ud, lk, "IDLE"); a2 = ucmds(lk)[-1] == "ARM"
uack(ud, lk); a3 = ucmds(lk)[-1] == "HOME"
uack(ud, lk); utel(ud, lk, "ARMED"); a4 = ucmds(lk)[-1] == "HOME" and ud.step == "HOME"   # 원점 완료 전에는 다음으로 안 감
ud.on_event({"evt": "HOMING_DONE", "result": "OK"}); a5 = ucmds(lk)[-1] == "SET_TARGET"
st_kw = lk.sent[-1][1]
uack(ud, lk); a6 = ucmds(lk)[-1] == "START_CPR" and ud.phase == "starting"
uack(ud, lk); a7 = ud.phase == "running"
ck("압박: IDLE → GET_STATUS, ARM, HOME(원점 완료 뒤), SET_TARGET, START_CPR 차례, ACK 뒤 running",
   all([a1, a2, a3, a4, a5, a6, a7]), str(ucmds(lk)))
ck("압박: SET_TARGET 은 설정 파일 값만(bpm, depth_mm, force_max_N)",
   st_kw == {"bpm": 110, "depth_mm": 50, "force_max_N": 600}, str(st_kw))
utel(ud, lk, "CPR_RUNNING")
ud.request("compress", "stop"); s_kw = lk.sent[-1][1]
a8 = ucmds(lk)[-1] == "STOP_CPR" and s_kw.get("reason") == "USER_STOP" and ud.phase == "stopping"
uack(ud, lk)
ck("압박 정지: STOP_CPR{reason:USER_STOP} → ACK 뒤 stopped", a8 and ud.phase == "stopped", "%s %s" % (ud.phase, s_kw))

lk, ud, sr, clk, pushed = urig({"target_depth_mm": 50})
ud.request("compress", "enter"); ud.request("compress", "start"); uack(ud, lk); utel(ud, lk, "HOMED")
ck("압박: HOMED 면 ARM, HOME 없이 SET_TARGET 부터, 비어 있는 힘 상한은 보내지 않음",
   ucmds(lk) == ["GET_STATUS", "SET_TARGET"] and "force_max_N" not in lk.sent[-1][1], str(lk.sent[-1]))
uack(ud, lk); uack(ud, lk, ok=False, why="NOT_READY")
ck("압박: 표준 펌웨어가 흡착 없이 START_CPR 을 거부(NOT_READY) → 실패, 사유 그대로, 정지 명령 추가 없음",
   ud.phase == "failed" and ud.reason == "NOT_READY" and ucmds(lk)[-1] == "START_CPR", str(ucmds(lk)))

lk, ud, sr, clk, pushed = urig(CP)
ud.request("compress", "enter"); ud.request("compress", "start"); uack(ud, lk); utel(ud, lk, "ARMED")
uack(ud, lk)                                         # HOME ACK 전에 완료 이벤트가 먼저 처리된 경우를 아래에서
lk2, ud2, _, _, _ = urig(CP)
ud2.request("compress", "enter"); ud2.request("compress", "start"); uack(ud2, lk2); utel(ud2, lk2, "ARMED")
ud2.on_event({"evt": "HOMING_DONE", "result": "OK"}); b1 = ucmds(lk2)[-1] == "HOME"
uack(ud2, lk2)
ck("압박: HOMING_DONE 이 HOME ACK 보다 먼저 와도 ACK 뒤 바로 다음 단계", b1 and ucmds(lk2)[-1] == "SET_TARGET", str(ucmds(lk2)))
ud.on_event({"evt": "HOMING_DONE", "result": "STALL"})
ck("압박: 원점 복귀 결과가 OK 아님 → 실패", ud.phase == "failed" and ud.reason == "HOMING_STALL", str(ud.reason))

lk, ud, sr, clk, pushed = cp_running()
utel(ud, lk, "HOMED")
ck("압박 중 장치가 스스로 멈춤(HOMED) → stopped(DEVICE_STOPPED)", ud.phase == "stopped" and ud.reason == "DEVICE_STOPPED")
lk, ud, sr, clk, pushed = cp_running()
utel(ud, lk, "ESTOP", estop=True)
ck("압박 중 비상정지 → 실패(ESTOP), 모터 정지 요청 STOP_CPR{SAFETY}",
   ud.phase == "failed" and ud.reason == "ESTOP" and lk.sent[-1][0] == "STOP_CPR" and lk.sent[-1][1]["reason"] == "SAFETY")
lk, ud, sr, clk, pushed = cp_running()
ud.on_error({"t": "err", "code": "3-CU-021", "sev": 3, "why": "OVER_CURRENT"})
ck("압박 중 고장(sev 3) → 실패(코드), STOP_CPR", ud.phase == "failed" and ud.reason == "3-CU-021" and ucmds(lk)[-1] == "STOP_CPR")
lk, ud, sr, clk, pushed = cp_running()
ud.on_error({"t": "err", "code": "2-MU-011", "sev": 2, "why": "OVER_TEMPERATURE"})
ck("압박 중 경고(sev 2)는 계속(장치가 정한다)", ud.phase == "running")
lk, ud, sr, clk, pushed = urig(CP)
ud.request("compress", "enter"); ud.request("compress", "start"); uack(ud, lk); utel(ud, lk, "HOMED"); uack(ud, lk)
ud.on_error({"code": "2-CM-002", "sev": 2, "seq": lk.sent[-1][2], "why": "ACK_TIMEOUT:START_CPR"})
ck("압박: START_CPR 응답 없음 → 실패(TIMEOUT), 혹시 돌고 있을 수 있어 STOP_CPR 요청",
   ud.phase == "failed" and ud.reason == "TIMEOUT" and ucmds(lk)[-1] == "STOP_CPR", str(ucmds(lk)))
lk, ud, sr, clk, pushed = cp_running()
ud.request(None, "leave")
ck("압박 중 화면을 나가면 STOP_CPR(USER_STOP), 사유 LEFT_SCREEN",
   ucmds(lk)[-1] == "STOP_CPR" and lk.sent[-1][1]["reason"] == "USER_STOP" and ud.reason == "LEFT_SCREEN" and ud.screen is None)
lk, ud, sr, clk, pushed = cp_running()
ud.ui_gone()
ck("압박 중 UI 연결이 모두 끊기면 STOP_CPR(SAFETY)", ucmds(lk)[-1] == "STOP_CPR" and lk.sent[-1][1]["reason"] == "SAFETY")
lk, ud, sr, clk, pushed = cp_running()
n_before = len(lk.sent); lk.connected = False; ud.on_link(False)
ck("압박 중 링크 두절 → 실패(LINK_DOWN), 보내지 않음(장치 통신 감시가 정지)",
   ud.phase == "failed" and ud.reason == "LINK_DOWN" and len(lk.sent) == n_before)
lk, ud, sr, clk, pushed = cp_running()
ck("압박 중 다시 시작 요청은 거부(BUSY)", ud.request("compress", "start") == (False, "BUSY"))

lk, ud, sr, clk, pushed = urig(CP)
ud.request("compress", "enter"); ud.request("compress", "start"); uack(ud, lk)
clk["t"] += STATUS_TEL_S + 0.1; ud.tick()
ck("시작 확인: GET_STATUS 뒤 텔레메트리 없음 → 실패(NO_STATUS)", ud.phase == "failed" and ud.reason == "NO_STATUS")
lk, ud, sr, clk, pushed = urig(CP)
ud.request("compress", "enter"); ud.request("compress", "start"); uack(ud, lk); utel(ud, lk, "ARMED"); uack(ud, lk)
clk["t"] += STEP_TIMEOUT_S + 1; ud.tick()
ck("원점 대기 한도 초과 → 실패(HOME_TIMEOUT)", ud.phase == "failed" and ud.reason == "HOME_TIMEOUT")
f_ok = []
for st_, est_, flt_ in (("FAULT", False, None), ("ESTOP", True, None), ("IDLE", True, None), ("IDLE", False, ["3-CU-021"])):
    lk, ud, sr, clk, pushed = urig(CP)
    ud.request("compress", "enter"); ud.request("compress", "start"); uack(ud, lk); utel(ud, lk, st_, est_, flt_)
    f_ok.append(ud.phase == "failed" and ucmds(lk) == ["GET_STATUS"])
ck("시작 확인: FAULT, ESTOP, 비상정지 표시, 활성 고장 → 실패, ARM 안 보냄", all(f_ok), str(f_ok))
lk, ud, sr, clk, pushed = urig(CP); lk.connected = False
ud.request("compress", "enter"); ud.request("compress", "start")
lk2, ud2, _, _, _ = urig(CP, proto=None)
ud2.request("compress", "enter"); ud2.request("compress", "start")
lk3, ud3, _, _, _ = urig(CP, proto="1.0")
ud3.request("compress", "enter"); ud3.request("compress", "start")
ck("장치 미연결 → NOT_CONNECTED, 펌웨어 판 확인 전 → LINK_CHECKING, v1.1 아님 → FW_VERSION (명령 없음)",
   ud.reason == "NOT_CONNECTED" and ud2.reason == "LINK_CHECKING" and ud3.reason == "FW_VERSION"
   and lk.sent == [] and lk2.sent == [] and lk3.sent == [])

VC = {"vacuum_target_kPa": -22.5}
lk, ud, sr, clk, pushed = urig(VC)
ud.request("vacuum", "enter"); ud.request("vacuum", "start"); uack(ud, lk); utel(ud, lk, "IDLE")
uack(ud, lk); uack(ud, lk); ud.on_event({"evt": "HOMING_DONE", "result": "OK"})
v1 = ucmds(lk) == ["GET_STATUS", "ARM", "HOME", "VACUUM_ON"] and lk.sent[-1][1] == {"target_kPa": -22.5}
uack(ud, lk); v2 = ud.phase == "starting"
ud.on_event({"evt": "VACUUM_OK"}); v3 = ud.phase == "running"
ck("흡착: ARM, HOME 뒤 VACUUM_ON(target_kPa), VACUUM_OK 뒤 running", v1 and v2 and v3, str(lk.sent[-1]))
ud.request("compress", "enter")
ck("흡착 중 다른 유닛 화면으로 가면 VACUUM_OFF", ucmds(lk)[-1] == "VACUUM_OFF" and ud.phase == "stopping")
uack(ud, lk)
ck("해제 ACK 뒤 stopped, 화면은 새 유닛", ud.phase == "stopped" and ud.screen == "compress")
lk, ud, sr, clk, pushed = urig()
ud.request("vacuum", "enter"); ud.request("vacuum", "start"); uack(ud, lk); utel(ud, lk, "HOMED")
ck("흡착: 목표 음압이 비어 있으면 target_kPa 없이 VACUUM_ON(장치 기본값)", lk.sent[-1][0] == "VACUUM_ON" and lk.sent[-1][1] == {})
uack(ud, lk); clk["t"] += STEP_TIMEOUT_S + 1; ud.tick()
ck("흡착 완료 대기 한도 초과 → 실패(VACUUM_TIMEOUT), 블로워를 끄도록 VACUUM_OFF",
   ud.phase == "failed" and ud.reason == "VACUUM_TIMEOUT" and ucmds(lk)[-1] == "VACUUM_OFF")
lk, ud, sr, clk, pushed = urig()
ud.request("vacuum", "enter"); ud.request("vacuum", "start"); uack(ud, lk); utel(ud, lk, "HOMED"); uack(ud, lk)
ud.on_event({"evt": "VACUUM_FAIL"})
ck("흡착: VACUUM_FAIL → 실패", ud.phase == "failed" and ud.reason == "VACUUM_FAIL")
lk, ud, sr, clk, pushed = urig()
ud.request("vacuum", "enter"); ud.request("vacuum", "start"); uack(ud, lk); utel(ud, lk, "VACUUM_HOLD")
ud.request("vacuum", "stop"); uack(ud, lk)
ck("흡착: 이미 고정 중이면 바로 running, 해제는 VACUUM_OFF", ucmds(lk) == ["GET_STATUS", "VACUUM_OFF"] and ud.phase == "stopped")

lk, ud, sr, clk, pushed = urig()
ud.request("drive", "enter"); ud.request("drive", "start"); uack(ud, lk); utel(ud, lk, "IDLE")
d1 = sr.calls == [("start", None)] and "ARM" not in ucmds(lk)
ud.on_search_done(False, {"phase": "failed", "reason": "PARAM_UNSET"})
ck("구동: 탐색 조건이 안 되면(설정 없음, 주행 기능 없음) ARM 없이 탐색 모듈에 맡겨 사유를 받음(PARAM_UNSET)",
   d1 and ud.phase == "failed" and ud.reason == "PARAM_UNSET", str(sr.calls))
SIMCFG = dict((k, v) for k, v in json.load(open(os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                                                           "config.sim.json"))).items() if not k.startswith("_"))
lk, ud, sr, clk, pushed = urig(SIMCFG, proto="1.1", eapd=205.8)
lk.caps = set(["DRIVE", "HEAD_HOLD", "MOT", "AID"])
ud.request("drive", "enter"); ud.request("drive", "start"); uack(ud, lk); utel(ud, lk, "IDLE")
d2 = ucmds(lk)[-1] == "ARM" and sr.calls == []
uack(ud, lk)
d3 = sr.calls == [("start", 205.8)] and ud.phase == "running"
ud.request("drive", "stop")
d4 = sr.calls[-1] == ("cancel", "USER_STOP") and ud.phase == "stopping"
ud.on_search_done(False, {"phase": "failed", "reason": "USER_STOP"})
ck("구동(v1.1, 설정과 EAPD 있음): ARM 뒤 탐색 시작(시작 순간 EAPD), 중지는 탐색 모듈 중지 → stopped",
   d2 and d3 and d4 and ud.phase == "stopped", str(sr.calls))
lk, ud, sr, clk, pushed = urig(SIMCFG, proto="1.1", eapd=205.8)
lk.caps = set(["DRIVE", "HEAD_HOLD", "MOT", "AID"])
ud.request("drive", "enter"); ud.request("drive", "start"); uack(ud, lk); utel(ud, lk, "HOMED")
ud.on_search_done(True, {"phase": "done"})
ck("구동: HOMED 면 ARM 없이 탐색, 완료 → done", ucmds(lk) == ["GET_STATUS"] and ud.phase == "done")
lk, ud, sr, clk, pushed = urig()
ud.request("patient", "enter")
ck("환자 상태: 시작할 장치 동작 없음(NO_ACTION), 명령 없음",
   ud.request("patient", "start") == (False, "NO_ACTION") and lk.sent == [])
ck("유닛 이름, 동작 이름 검사", urig()[1].request("x", "enter") == (False, "UNKNOWN_UNIT")
   and urig()[1].request("compress", "jump") == (False, "UNKNOWN_ACTION"))

# 브리지 + 모의 Teensy(v1.1): 시연 모드는 연결만으로 ARM, HOME 을 보내지 않는다
_udkeep = dict((k, B.CONFIG.get(k)) for k in ("target_depth_mm", "force_max_N", "vacuum_target_kPa"))


# ── 강성 측정(stiff, 10/23 팀장: 구동 대신 시연 메뉴에) ──
from saver.unit_demo import stiffness, STIFF_SETTLE, STIFF_CYCLES
import math as _m
_T = 60.0 / 110
_kpk = 514.5 / 50.0                                  # 모의 흉부 모델(k 10 N/mm, c 0.3 N·s/mm)의 주기 최대 힘 / 깊이
_n = int((STIFF_SETTLE + STIFF_CYCLES) * _T / 0.05) + 2
_pk = [(i * 0.05, 514.5, 50.0) for i in range(_n)]  # 주기 최댓값으로 보내는 펌웨어
r_pk = stiffness(_pk, _T)
ck("강성 추정: 주기 최댓값 텔레메트리 → k = F/D, 처음 주기는 버리고 10주기",
   r_pk and r_pk["n"] == STIFF_CYCLES and abs(r_pk["k_N_mm"] - _kpk) < 0.01 and r_pk["k_sd"] == 0.0
   and len(r_pk["pts"]) == STIFF_CYCLES, str(r_pk))


def _inst(t, d=50.0, f=110 / 60.0):
    th = 2 * _m.pi * f * t
    return 10.0 * d / 2 * (1 - _m.cos(th)) + 0.3 * d * _m.pi * f * _m.sin(th)


_in = [(i * 0.05, _inst(i * 0.05), 50.0) for i in range(_n)]  # 순간값(20 Hz 표본)으로 보내는 펌웨어
r_in = stiffness(_in, _T)
ck("강성 추정: 순간 힘 20 Hz 표본이어도 주기 창 최댓값으로 4 % 안에서 같은 값",
   r_in and r_in["n"] == STIFF_CYCLES and 0.96 * _kpk <= r_in["k_N_mm"] <= _kpk + 0.01, str(r_in and r_in["k_N_mm"]))
_gap = [(t, (None if 2 * _T <= t < 3 * _T else f), d) for t, f, d in _pk]
_shallow = [(t, f, (0.5 if 3 * _T <= t < 4 * _T else d)) for t, f, d in _pk]
ck("강성 추정: 힘 값이 없는 주기, 깊이가 1 mm 미만인 주기는 빼고, 0 으로 나누지 않음",
   stiffness(_gap, _T)["n"] == STIFF_CYCLES - 1 and stiffness(_shallow, _T)["n"] == STIFF_CYCLES - 1
   and stiffness([(t, None, d) for t, f, d in _pk], _T) is None and stiffness([], _T) is None)
ck("강성 추정: 끝나지 않은 주기 창은 세지 않음",
   stiffness([x for x in _pk if x[0] < (STIFF_SETTLE + 3.5) * _T], _T)["n"] == 3)

SC = {"target_depth_mm": 50, "force_max_N": 600}


def stiff_run(lk, ud, clk, secs, force=514.5, depth=50.0, ms0=1000):
    """압박 중 텔레메트리를 20 Hz 로 secs 초 넣는다(장치 시각 ms)."""
    for i in range(int(secs / 0.05)):
        lk.mcu_state = "CPR_RUNNING"
        m = {"t": "tel", "state": "CPR_RUNNING", "ms": ms0 + i * 50, "estop": False, "faults": []}
        if force is not None:
            m["cp_force_N"] = force
        if depth is not None:
            m["cp_depth_mm"] = depth
        clk["t"] += 0.05
        ud.on_tel(m)
        if ud.phase != "running":
            break


lk, ud, sr, clk, pushed = urig()
ud.request("stiff", "enter")
ck("강성 측정: 목표 깊이가 비어 있으면 명령 없이 실패(TARGET_NOT_SET)",
   ud.request("stiff", "start") == (True, None) and ud.phase == "failed" and ud.reason == "TARGET_NOT_SET" and lk.sent == [])
lk, ud, sr, clk, pushed = urig(SC)
ud.request("stiff", "enter")
e0 = lk.sent == [] and ud.phase == "idle"
ud.request("stiff", "start"); uack(ud, lk); utel(ud, lk, "HOMED")
s1 = ucmds(lk) == ["GET_STATUS", "SET_TARGET"] and lk.sent[-1][1] == {"bpm": 110, "depth_mm": 50, "force_max_N": 600}
uack(ud, lk); uack(ud, lk)
ck("강성 측정: 진입만으로 명령 없음, 시작은 압박과 같은 순서(SET_TARGET, START_CPR), ACK 뒤 running",
   e0 and s1 and ucmds(lk)[-1] == "START_CPR" and ud.phase == "running", str(ucmds(lk)))
stiff_run(lk, ud, clk, 3.0)
mid = pushed[-1].get("stiff") or {}
m1 = ud.phase == "running" and 0 < mid.get("done", 0) < STIFF_CYCLES and mid.get("of") == STIFF_CYCLES
stiff_run(lk, ud, clk, 5.0, ms0=4000)
end_kw = lk.sent[-1][1]
m2 = ucmds(lk)[-1] == "STOP_CPR" and end_kw.get("reason") == "END" and ud.phase == "stopping"
uack(ud, lk)
fin = pushed[-1].get("stiff") or {}
ck("강성 측정: 진행(끝난 주기 수)을 알리고, 10주기 뒤 STOP_CPR{reason:END} → ACK 뒤 done, k = F/D",
   m1 and m2 and ud.phase == "done" and fin.get("done") == STIFF_CYCLES and fin.get("n") == STIFF_CYCLES
   and abs(fin.get("k_N_mm", 0) - _kpk) < 0.01 and fin.get("f_N") == 514.5 and fin.get("d_mm") == 50.0,
   "%s %s %s" % (ud.phase, end_kw, fin))
ck("강성 측정: 다른 유닛의 상태 알림에는 강성 값이 없음", urig()[1].snapshot()["stiff"] is None)

lk, ud, sr, clk, pushed = urig(SC)
ud.request("stiff", "enter"); ud.request("stiff", "start"); uack(ud, lk); utel(ud, lk, "HOMED"); uack(ud, lk); uack(ud, lk)
stiff_run(lk, ud, clk, 3.0)
ud.request("stiff", "stop"); s_kw = lk.sent[-1][1]; uack(ud, lk)
part = pushed[-1].get("stiff") or {}
ck("강성 측정: 도중 [정지] → STOP_CPR{USER_STOP} → stopped, 끝난 주기까지의 부분 결과",
   s_kw.get("reason") == "USER_STOP" and ud.phase == "stopped" and 0 < part.get("n", 0) < STIFF_CYCLES, str(part))
lk, ud, sr, clk, pushed = urig(SC)
ud.request("stiff", "enter"); ud.request("stiff", "start"); uack(ud, lk); utel(ud, lk, "HOMED"); uack(ud, lk); uack(ud, lk)
stiff_run(lk, ud, clk, 8.0, force=None)
ck("강성 측정: 힘 값이 오지 않으면 실패(STIFF_NO_DATA)하고 압박 정지 요청(SAFETY)",
   ud.phase == "failed" and ud.reason == "STIFF_NO_DATA" and ucmds(lk)[-1] == "STOP_CPR"
   and lk.sent[-1][1].get("reason") == "SAFETY", "%s %s" % (ud.reason, lk.sent[-1]))
lk, ud, sr, clk, pushed = urig(SC)
ud.request("stiff", "enter"); ud.request("stiff", "start"); uack(ud, lk); utel(ud, lk, "HOMED"); uack(ud, lk); uack(ud, lk)
clk["t"] += STEP_TIMEOUT_S + 0.5; ud.tick()
ck("강성 측정: START_CPR 뒤 압박이 시작되지 않으면 실패(NOT_RUNNING)하고 정지 요청",
   ud.phase == "failed" and ud.reason == "NOT_RUNNING" and ucmds(lk)[-1] == "STOP_CPR", str(ucmds(lk)))
lk, ud, sr, clk, pushed = urig(SC)
ud.request("stiff", "enter"); ud.request("stiff", "start"); uack(ud, lk); utel(ud, lk, "HOMED"); uack(ud, lk); uack(ud, lk)
stiff_run(lk, ud, clk, 2.0)
clk["t"] += 30.0; ud.tick()
ck("강성 측정: 측정 중 텔레메트리가 끊겨 끝나지 않으면 실패(STIFF_TIMEOUT)하고 정지 요청",
   ud.phase == "failed" and ud.reason == "STIFF_TIMEOUT" and ucmds(lk)[-1] == "STOP_CPR", str(ud.reason))
lk, ud, sr, clk, pushed = urig(SC)
ud.request("stiff", "enter"); ud.request("stiff", "start"); uack(ud, lk); utel(ud, lk, "HOMED"); uack(ud, lk); uack(ud, lk)
stiff_run(lk, ud, clk, 1.0)
ud.request("compress", "enter")
ck("강성 측정 중 다른 유닛 화면으로 → 압박 정지(STOP_CPR USER_STOP)",
   ucmds(lk)[-1] == "STOP_CPR" and lk.sent[-1][1].get("reason") == "USER_STOP", str(lk.sent[-1]))


def _ud_bridge(port, unit_demo=True):
    b_ = B.Bridge(argparse.Namespace(serial=port, logdir=tempfile.mkdtemp(prefix="saver_ud_"), spec=None, port=0,
                                     unit_demo=unit_demo))
    sent_ = []
    orig_ = b_.link.send
    def rec_(cmd, **kw):
        sent_.append((cmd, kw))
        return orig_(cmd, **kw)
    b_.link.send = rec_
    pushed_ = []
    b_.units.push = pushed_.append
    return b_, sent_, pushed_


def _ud_wait(b_, cond, limit):
    end = time.monotonic() + limit
    while time.monotonic() < end and not cond():
        b_.units.tick(); time.sleep(0.05)
    return cond()


def _ud_sim(port, extra=()):
    p_ = subprocess.Popen([sys.executable, "-u", os.path.join(SIMDIR, "fake_teensy.py"), "--port", port, "--hz", "20",
                           "--seed", "8", "--quiet", "--time-scale", "0.05"] + list(extra),
                          stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    time.sleep(1.2)
    return p_


B.CONFIG.update({"target_depth_mm": 50, "force_max_N": 600, "vacuum_target_kPa": -22.5})
simu = _ud_sim("/tmp/ttyFAKE_UD", ["--bench"])
bu, sentu, pushu = _ud_bridge("/tmp/ttyFAKE_UD")
bu.link.start()
up_ = _ud_wait(bu, lambda: bu.link.connected and bu.link.proto == "1.1" and bu.link.mcu_state == "IDLE", 6.0)
time.sleep(1.0)
moving = [c for c, _ in sentu if c not in ("PING", "GET_STATUS", "GET_INFO")]
ck("브리지 시연 모드: 장치 연결만으로 ARM, HOME 등 동작 명령 없음, 상태머신 흐름 기록 없음",
   up_ and moving == [] and bu.fsm.log == [], "%s %s" % (moving, bu.link.mcu_state))
cm = bu.config_msg()
ck("브리지 시연 모드: 설정 메시지에 mode unit_demo, 목표값", cm["mode"] == "unit_demo" and cm["target_depth_mm"] == 50
   and cm["force_max_N"] == 600 and cm["vacuum_target_kPa"] == -22.5 and cm["target_bpm"] == 110, str(cm))
ck("브리지 시연 모드: 보고 있지 않은 유닛 시작 요청은 거부 응답",
   (bu.unit_msg({"type": "unit", "unit": "compress", "action": "start"}) or {}).get("reason") == "NOT_ON_SCREEN")
bu.unit_msg({"type": "unit", "unit": "compress", "action": "enter"})
bu.unit_msg({"type": "unit", "unit": "compress", "action": "start"})
run_ = _ud_wait(bu, lambda: bu.units.phase == "running" and (bu.link.last_tel or {}).get("state") == "CPR_RUNNING", 8.0)
seq_ = [c for c, _ in sentu if c not in ("PING", "GET_INFO")]
ck("브리지 + 모의 Teensy(시연 허용 --bench): 압박 시작 → ARM, HOME, SET_TARGET, START_CPR → 장치 CPR_RUNNING",
   run_ and seq_[-4:] == ["ARM", "HOME", "SET_TARGET", "START_CPR"], "%s %s %s" % (bu.units.phase, bu.units.reason, seq_))
tel_ = dict(bu.link.last_tel)
ck("브리지 + 모의 Teensy: 압박 중 텔레메트리에 깊이, 속도, 힘(UI 표시 값)",
   all(isinstance(tel_.get(k), (int, float)) for k in ("cp_depth_mm", "cp_rate_cpm", "cp_force_N")), str(sorted(tel_)))
bu.unit_msg({"type": "unit", "unit": "compress", "action": "stop"})
st_ = _ud_wait(bu, lambda: bu.units.phase == "stopped" and (bu.link.last_tel or {}).get("state") == "HOMED", 5.0)
ck("브리지 + 모의 Teensy: 정지 → STOP_CPR, 장치 HOMED, stopped", st_, "%s %s" % (bu.units.phase, bu.link.mcu_state))
bu.unit_msg({"type": "unit", "unit": "stiff", "action": "enter"})
bu.unit_msg({"type": "unit", "unit": "stiff", "action": "start"})
sd_ = _ud_wait(bu, lambda: bu.units.phase in ("done", "failed", "stopped"), 15.0)
sv_ = bu.units.snapshot().get("stiff") or {}
ck("브리지 + 모의 Teensy: 강성 측정 → 10주기 뒤 STOP_CPR(END), done, 모의 흉부 모델 강성(약 10.3 N/mm)을 다시 구함",
   sd_ and bu.units.phase == "done" and [c for c, _ in sentu if c == "STOP_CPR"] and
   [k for c, k in sentu if c == "STOP_CPR"][-1].get("reason") == "END" and 9.6 <= sv_.get("k_N_mm", 0) <= 10.8,
   "%s %s %s" % (bu.units.phase, bu.units.reason, sv_))
_ud_wait(bu, lambda: (bu.link.last_tel or {}).get("state") == "HOMED", 4.0)
bu.unit_msg({"type": "unit", "unit": "vacuum", "action": "enter"})
bu.unit_msg({"type": "unit", "unit": "vacuum", "action": "start"})
vh_ = _ud_wait(bu, lambda: bu.units.phase == "running" and (bu.link.last_tel or {}).get("vac_kPa", 0) < -20, 6.0)
ck("브리지 + 모의 Teensy: 흡착 시작 → VACUUM_ON → VACUUM_OK, 음압 텔레메트리", vh_,
   "%s %s %s" % (bu.units.phase, bu.units.reason, (bu.link.last_tel or {}).get("vac_kPa")))
bu.units.ui_gone()
vo_ = _ud_wait(bu, lambda: bu.units.phase == "stopped", 4.0)
ck("브리지 + 모의 Teensy: UI 가 모두 끊기면 흡착 해제(VACUUM_OFF)", vo_ and sentu[-1][0] in ("VACUUM_OFF", "PING")
   and "VACUUM_OFF" in [c for c, _ in sentu], bu.units.phase)
bu.link.stop(); simu.terminate(); simu.wait()

simn = _ud_sim("/tmp/ttyFAKE_UD2")
bn, sentn, pushn = _ud_bridge("/tmp/ttyFAKE_UD2")
bn.link.start()
_ud_wait(bn, lambda: bn.link.connected and bn.link.proto == "1.1" and bn.link.mcu_state == "IDLE", 6.0)
bn.unit_msg({"type": "unit", "unit": "compress", "action": "enter"})
bn.unit_msg({"type": "unit", "unit": "compress", "action": "start"})
rj_ = _ud_wait(bn, lambda: bn.units.phase == "failed", 8.0)
ck("브리지 + 모의 Teensy(표준 v1.1): 흡착 없는 압박 시작은 장치가 거부(NOT_READY), 화면에 사유",
   rj_ and bn.units.reason == "NOT_READY" and bn.link.mcu_state == "HOMED",
   "%s %s %s" % (bn.units.phase, bn.units.reason, bn.link.mcu_state))
bn.link.stop(); simn.terminate(); simn.wait()
for _p in ("/tmp/ttyFAKE_UD", "/tmp/ttyFAKE_UD2"):
    try:
        os.remove(_p)
    except OSError:
        pass
bo, _, _ = _ud_bridge("/nonexistent_ud", unit_demo=False)
ck("운용 모드 브리지: 유닛 요청 거부(not_unit_demo), 설정 메시지 mode operate",
   (bo.unit_msg({"type": "unit", "unit": "compress", "action": "enter"}) or {}).get("reason") == "not_unit_demo"
   and bo.config_msg()["mode"] == "operate")
B.CONFIG.update(_udkeep)
cm0 = bo.config_msg()
ck("설정 메시지: 비어 있는 목표값(0)은 null", cm0["target_depth_mm"] is None and cm0["force_max_N"] is None
   and cm0["vacuum_target_kPa"] is None, str(cm0))

print("\n%s  (통과 %d / 실패 %d)\n" % ("전체 통과" if P_["fail"] == 0 else "실패 있음", P_["pass"], P_["fail"]))
sys.exit(0 if P_["fail"] == 0 else 1)
