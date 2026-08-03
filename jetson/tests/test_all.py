#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""SAVER Jetson 자동시험 — 하드웨어 없이 전부 통과해야 합니다."""
import os, sys, time, json, csv, shutil, subprocess, tempfile, threading
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from saver import protocol as P
from saver.state_machine import StateMachine
from saver.teensy_serial import TeensyLink
from saver.logger import CsvLogger
from saver.mark import split_mark, build_message
from simulators.fake_teensy import write_demo_log
from tools import analyze_log as A

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
        "POSITION_OK","PPG_ATTACHED","VACUUM_OK","INFO_CONFIRMED","CPR_START"]
ok = all(fsm.fire(e) for e in path)
ck("정상 흐름 S1→S7", ok and fsm.node == "S7", fsm.node)
ck("S7 running=True", fsm.is_running())
ck("진입 명령 발행", "START_CPR" in fired and "VACUUM_ON" in fired, ",".join(fired))
ck("허용 안 된 이벤트 무시", fsm.fire("RELEASE_DONE") is False and fsm.node == "S7")
fsm.fire("ROSC_SUSPECT"); ck("ROSC 분기", fsm.node == "S9")
fsm.fire("RESUME_CPR");  ck("압박 재개", fsm.node == "S7")

f2 = StateMachine(); f2.force("S5")
f2.fire("VACUUM_ERROR")
ck("오류 진입", f2.node == "S10" and f2.error_cause == "vacuum")
f2.fire("RECHECK"); ck("RECHECK 복귀 S10→S5", f2.node == "S5")

spec_path = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                         "..", "protocol", "state_machine_v2.json")
with open(spec_path, "r", encoding="utf-8") as spec_fp:
    spec = json.load(spec_fp)
bad = [(n, e, t) for n, d in spec["nodes"].items()
       for e, t in d.get("on", {}).items() if t != "@return" and t not in spec["nodes"]]
ck("노드 참조 무결성", not bad, str(bad[:2]))
unknown = [(n, e) for n, d in spec["nodes"].items()
           for e in d.get("on", {}) if e not in spec["events"]]
ck("이벤트 정의 무결성", not unknown, str(unknown[:2]))
cmds = set(c for d in spec["nodes"].values() for c in d.get("enter_cmds", []))
ck("enter_cmds 가 명세 명령인지", cmds <= set(P.CMD), str(sorted(cmds - set(P.CMD))))

print("\n== 3. 측정 mark와 로그 분석 ==")
label, note = split_mark("trial_1 depth 50mm")
ck("mark CLI 한 인자 분리", label == "trial_1" and note == "depth 50mm")
ck("mark WebSocket 메시지", build_message(label, note) ==
   {"type": "mark", "label": "trial_1", "note": "depth 50mm"})

tmpdir = tempfile.mkdtemp(prefix="saver_test_")
try:
    logger = CsvLogger(tmpdir, prefix="mark")
    mark_path = logger.path
    mark_ok = logger.mark("vacuum_1", "surface=mat, voltage=24V\nload=0kg")
    logger.close()
    with open(mark_path, "r", newline="", encoding="utf-8") as fp:
        mark_rows = list(csv.DictReader(fp))
    ck("mark CSV source/label", mark_ok and len(mark_rows) == 1 and
       mark_rows[0]["source"] == "mark" and mark_rows[0]["mark_label"] == "vacuum_1")
    ck("mark note 자유 문자열", mark_rows[0]["mark_note"] ==
       "surface=mat, voltage=24V load=0kg")

    analysis_path = os.path.join(tmpdir, "analysis.csv")
    fields = ["ts_ms", "source", "mark_label", "mark_note", "vac_kPa",
              "cp_rate_cpm", "cp_depth_mm", "cp_depth_mm_ok",
              "cp_force_N", "cp_motor_i_A"]
    rows = [
        {"ts_ms": 0, "source": "mark", "mark_label": "vacuum_1",
         "mark_note": "target=-22.5 tol=2.5"},
        {"ts_ms": 1000, "source": "mcu", "vac_kPa": -15.0},
        {"ts_ms": 2000, "source": "mcu", "vac_kPa": -20.0},
        {"ts_ms": 3000, "source": "mcu", "vac_kPa": -19.0},
        {"ts_ms": 4000, "source": "mark", "mark_label": "compression_1",
         "mark_note": "depth=50mm"},
        {"ts_ms": 4000, "source": "mcu", "cp_rate_cpm": 120,
         "cp_depth_mm": 50, "cp_depth_mm_ok": "true", "cp_force_N": 300,
         "cp_motor_i_A": 5},
        {"ts_ms": 4250, "source": "mcu", "cp_rate_cpm": 120,
         "cp_depth_mm": 999, "cp_depth_mm_ok": "false", "cp_force_N": 310,
         "cp_motor_i_A": 5.2},
        {"ts_ms": 4500, "source": "mcu", "cp_rate_cpm": 120,
         "cp_depth_mm": 51, "cp_depth_mm_ok": "true", "cp_force_N": 320,
         "cp_motor_i_A": 5.4},
        {"ts_ms": 4750, "source": "mcu", "cp_rate_cpm": 120,
         "cp_depth_mm": 52, "cp_depth_mm_ok": "true", "cp_force_N": 330,
         "cp_motor_i_A": 5.6},
        {"ts_ms": 5000, "source": "mcu", "cp_rate_cpm": 120,
         "cp_depth_mm": 53, "cp_depth_mm_ok": "true", "cp_force_N": 340,
         "cp_motor_i_A": 5.8},
    ]
    with open(analysis_path, "w", newline="", encoding="utf-8") as fp:
        writer = csv.DictWriter(fp, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)
    analysis_fields, segments = A.read_segments(analysis_path)
    ck("mark 기준 구간 분리", len(segments) == 2 and
       [s["label"] for s in segments] == ["vacuum_1", "compression_1"])
    vacuum = A.vacuum_metrics(segments[0], -22.5, 2.5)
    ck("음압 목표 도달 시간", vacuum["reach_s"] == 2.0, str(vacuum["reach_s"]))
    ck("음압 누설률", vacuum["leak_kpa_min"] is not None and
       vacuum["leak_kpa_min"] > 0, str(vacuum["leak_kpa_min"]))
    depth_values = [A.number(row, "cp_depth_mm") for row in segments[1]["rows"]]
    depth_summary = A.summarize(depth_values)
    ck("*_ok=false 결측값 제외", depth_summary["max"] == 53 and
       depth_summary["n"] == 4, str(depth_summary))
    cycles = A.compression_cycles(segments[1]["rows"])
    ck("압박 사이클 구분", len(cycles) == 3, str(len(cycles)))

    fake_log_path = write_demo_log(tmpdir, hz=10, duration_s=1.0, seed=2)
    fake_fields, fake_segments = A.read_segments(fake_log_path)
    ck("fake_teensy mark 로그 생성", [s["label"] for s in fake_segments] ==
       ["vacuum_1", "compression_1"])
    ck("fake_teensy 압박 로그 분석", bool(A.compression_cycles(fake_segments[1]["rows"])))
finally:
    shutil.rmtree(tmpdir)

if "--unit-only" in sys.argv:
    print("\n%s  (통과 %d / 실패 %d)\n" %
          ("단위시험 통과" if P_["fail"] == 0 else "실패 있음",
           P_["pass"], P_["fail"]))
    sys.exit(0 if P_["fail"] == 0 else 1)

if os.name != "posix":
    print("\n== 4~6. Linux 가상 시리얼 통합 ==")
    print("  SKIP Windows에는 pty와 /tmp 가 없어 Jetson/Linux에서 실행합니다.")
    print("\n%s  (플랫폼 독립 통과 %d / 실패 %d)\n" %
          ("플랫폼 독립 시험 통과" if P_["fail"] == 0 else "실패 있음",
           P_["pass"], P_["fail"]))
    sys.exit(0 if P_["fail"] == 0 else 1)

print("\n== 4. 가짜 Teensy 실물 연결 ==")
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

print("\n== 5. 고장 주입 ==")
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

print("\n== 6. 재연결 ==")
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

print("\n%s  (통과 %d / 실패 %d)\n" % ("전체 통과" if P_["fail"] == 0 else "실패 있음", P_["pass"], P_["fail"]))
sys.exit(0 if P_["fail"] == 0 else 1)
