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
from fake_teensy_v11 import FakeTeensy11
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

print("\n%s  (통과 %d / 실패 %d)\n" % ("전체 통과" if P_["fail"] == 0 else "실패 있음", P_["pass"], P_["fail"]))
sys.exit(0 if P_["fail"] == 0 else 1)
