#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""fake_teensy.py — Teensy 4.1 모의 장치

실제 Teensy 없이 Jetson 쪽 전체를 개발·시험하기 위한 시뮬레이터입니다.
가상 시리얼 포트(pty)를 만들기 때문에 Jetson 코드는 진짜 장비와 완전히 같은
경로(pyserial → /dev/pts/N)로 접속합니다. 코드에 "가짜용 분기"가 생기지 않습니다.

명세: protocol/teensy_usb_protocol_v1.md v1.1
젯슨은 v1.1 펌웨어하고만 동작하므로 기본은 v1.1 모의 장치(fake_teensy_v11.py)입니다.
이 파일의 FakeTeensy 는 v1.0 펌웨어 모의로 남겨 두며(--fw 1.0), 젯슨의 판 불일치 처리 시험에 씁니다.
Teensy 실물 구현과 동작이 일치해야 합니다. 다르면 명세를 고치고 양쪽을 맞추세요.

실행
  python3 fake_teensy.py                      # v1.1, 시뮬레이션 설정(teensy_settings.sim.json). 포트 경로를 출력하고 대기
  python3 fake_teensy.py --port /tmp/ttyFAKE  # 심볼릭 링크까지 생성
  python3 fake_teensy.py --scenario overcurrent@15
  python3 fake_teensy.py --no-settings        # 설정값을 모두 비운 v1.1(실물 펌웨어 기본과 같음: ARM, HOME 거부)
  python3 fake_teensy.py --fw 1.0             # v1.0 펌웨어 모의(젯슨은 판 불일치로 처리)
  python3 fake_teensy.py --bench              # 결선 시연 제안: 흡착 없이 압박 시연 허용(실물 펌웨어 미구현, D-51)

※ 백그라운드(&)로 돌리려면 stdin 을 끊어야 합니다. 터미널을 읽는 백그라운드
   프로세스는 SIGTTIN 으로 정지됩니다. 아래 중 하나를 쓰세요.
     python3 fake_teensy.py --port /tmp/ttyFAKE < /dev/null &
     python3 fake_teensy.py --port /tmp/ttyFAKE --fifo /tmp/saver_inject &
   FIFO 를 쓰면 백그라운드에서도 고장 주입이 됩니다.
     echo oc > /tmp/saver_inject

실행 중 키보드로 고장 주입 (엔터로 입력)
  oc     과전류 (3-CU-021)
  drverr 음압 드라이버 오류 (2-VU-007)
  hot    구동부 온도 경고 (2-MU-011)
  estop  비상정지
  leak   음압 누설 (2-VU-005)
  vloss  흡착 상실 (3-VU-006)
  drop   송신 중단 = 통신 두절 재현
  slow   텔레메트리 1 Hz 로 저하
  ok     정상 복귀
  q      종료
"""
import argparse
import json
import os
import pty
import termios
import tty
import random
import signal
import sys
import threading
import time

PROTO_VERSION = 1
MAX_LINE = 512


class FakeTeensy(object):

    def __init__(self, tel_hz=20, seed=None, bench=False):
        self.master, self.slave = pty.openpty()
        # 실제 시리얼처럼 raw 모드로. 기본 pty 는 에코가 켜져 있어서
        # 자기가 보낸 텔레메트리를 되읽어 명령과 뒤엉킨다(→ 1-CM-003 유발).
        tty.setraw(self.master)
        tty.setraw(self.slave)
        attr = termios.tcgetattr(self.slave)
        attr[3] = attr[3] & ~termios.ECHO          # lflag
        termios.tcsetattr(self.slave, termios.TCSANOW, attr)
        self.port = os.ttyname(self.slave)
        self.rng = random.Random(seed)

        self.state = "IDLE"
        self.seen_seq = []           # 최근 8개 (명세 §4.1)
        self.fault = None
        self.estop = False
        self.leak = False
        self.dropped = False         # True 면 아무것도 송신하지 않음
        self.tel_period = 1.0 / tel_hz

        self.target_bpm = 110.0
        self.target_depth = 0.0      # SET_TARGET 전에는 0 → START_CPR 거부
        self.vacuum = False          # 음압 출력(VACUUM_ON 완료 ~ VACUUM_OFF)
        # 결선 시연 제안(D-51, 한건민 합의 전, 실물 펌웨어 미구현): 흡착 없이 HOMED 에서 START_CPR 을 받는다.
        # 기본(False)은 명세 v1.0 §5 그대로 CPR_READY 에서만 받는다
        self.bench = bench
        self.t0 = time.time()
        self.running = True
        self.lock = threading.Lock()
        self._buf = b""

    # ── 송신 ────────────────────────────────────────────────
    def _send(self, msg):
        if self.dropped:
            return
        msg.setdefault("v", PROTO_VERSION)
        line = json.dumps(msg, ensure_ascii=True, separators=(",", ":")) + "\n"
        if len(line) > MAX_LINE:
            line = json.dumps({"v": 1, "t": "err", "code": "1-CM-003",
                               "sev": 1, "latched": False, "why": "SELF_TOO_LONG"}) + "\n"
        data = line.encode("ascii")
        sent = 0
        try:
            while sent < len(data):
                sent += os.write(self.master, data[sent:])
        except BlockingIOError:
            pass
        except OSError:
            self.running = False

    def ms(self):
        return int((time.time() - self.t0) * 1000)

    def ack(self, seq, ok=True, err=None, why=None):
        m = {"t": "ack", "seq": seq, "ok": ok}
        if ok:
            m["state"] = self.state
        else:
            m["err"] = err or ""
            m["why"] = why or ""
        self._send(m)

    def evt(self, name, **kw):
        m = {"t": "evt", "ms": self.ms(), "evt": name}
        m.update(kw)
        self._send(m)

    def err(self, code, sev, why, latched=None):
        if latched is None:
            latched = sev >= 3
        if sev >= 3:
            if self.state != "ESTOP":     # 비상정지가 우선한다(ESTOP 을 FAULT 로 덮어쓰지 않음)
                self.state = "FAULT"      # 명세 §4.5: 정지가 먼저, 보고가 나중
            self.fault = code
        self._send({"t": "err", "ms": self.ms(), "code": code,
                    "sev": sev, "latched": latched, "why": why})

    # ── 텔레메트리 ──────────────────────────────────────────
    def telemetry(self):
        self._send(self.tel_msg())

    def tel_msg(self):
        """한 주기 텔레메트리(v1.0 필드). v1.1 모의 장치는 이 값에 tn·boot_id 와 자기 상태를 더한다."""
        import math
        t = time.time() - self.t0
        m = {"t": "tel", "ms": self.ms(), "state": self.state}

        if self.state == "CPR_RUNNING":
            m["cp_rate_cpm"] = round(self.target_bpm + math.sin(t * 0.7) * 1.2, 2)
            m["cp_depth_mm"] = round(self.target_depth + math.sin(t * 1.9) * 1.4, 2)
            m["cp_force_N"] = round(380 + math.sin(t * 1.9) * 45, 2)
            m["cp_motor_i_A"] = round(6.5 + abs(math.sin(t * 1.9)) * 2.0, 2)
        elif self.state in ("CPR_PAUSED", "CPR_READY"):
            m["cp_rate_cpm"] = 0.0
            m["cp_depth_mm"] = 0.0
            m["cp_force_N"] = 0.0
            m["cp_motor_i_A"] = 0.1

        if self.vacuum:
            base = -29.5 if not self.leak else -18.0
            m["vac_kPa"] = round(base + self.rng.uniform(-0.4, 0.4), 2)
        else:
            m["vac_kPa"] = round(self.rng.uniform(-0.5, 0.0), 2)

        # PPG: 신호 품질이 나쁘면 필드를 생략한다 (명세 §6 — 0을 보내지 않는다)
        sqi = round(self.rng.uniform(0.75, 0.95), 2)
        m["ppg_sqi"] = sqi
        if sqi >= 0.5:
            m["ppg_hr_bpm"] = round(70 + math.sin(t * 0.3) * 5, 2)
            m["spo2_pct"] = round(96 + self.rng.uniform(0, 2), 2)

        # 구동부 온도 — 압박 중에는 상승
        load = 1.0 if self.state == "CPR_RUNNING" else 0.0
        m["temp_C"] = round(28 + load * (6 + 3 * abs(math.sin(t * 0.05))), 2)
        m["fan_pct"] = round(100.0 if m["temp_C"] >= 40 else (40.0 if load else 0.0), 2)

        m["estop"] = self.estop
        m["limit_top"] = self.state in ("IDLE", "ARMED", "HOMED")
        m["limit_bottom"] = False
        m["vac_leak"] = self.leak
        m["faults"] = [self.fault] if self.fault else []
        return m

    # ── 명령 처리 ───────────────────────────────────────────
    def require(self, seq, need, why):
        if self.state in need:
            return True
        self.ack(seq, False, "2-CU-011", why)
        return False

    def handle(self, msg):
        if msg.get("v") != PROTO_VERSION:
            self.err("3-CM-001", 3, "VERSION_MISMATCH")
            return
        if msg.get("t") != "cmd":
            return
        seq = msg.get("seq")
        cmd = msg.get("cmd")
        if seq is None or not cmd:
            return

        # 중복 seq: 재실행하지 않고 ACK만 다시 (명세 §4.1)
        if seq in self.seen_seq:
            self.ack(seq, True)
            return
        self.seen_seq.append(seq)
        if len(self.seen_seq) > 8:
            self.seen_seq.pop(0)

        if self.state == "ESTOP" and cmd not in ("PING", "GET_STATUS"):
            self.ack(seq, False, "3-MU-002", "ESTOP_ACTIVE")
            return

        if cmd == "PING":
            self.ack(seq)
        elif cmd == "GET_STATUS":
            self.ack(seq)
            self.telemetry()
        elif cmd == "ARM":
            if not self.require(seq, ("IDLE",), "NOT_IDLE"):
                return
            self.state = "ARMED"
            self.ack(seq)
        elif cmd == "DISARM":
            self.state = "IDLE"
            self.ack(seq)
        elif cmd == "HOME":
            if not self.require(seq, ("ARMED",), "NOT_ARMED"):
                return
            self.ack(seq)
            threading.Timer(0.8, self._homed).start()
        elif cmd == "SET_TARGET":
            if "bpm" in msg:
                self.target_bpm = float(msg["bpm"])
            if "depth_mm" in msg:
                self.target_depth = float(msg["depth_mm"])
            self.ack(seq)
        elif cmd == "VACUUM_ON":
            if not self.require(seq, ("HOMED",), "NOT_HOMED"):
                return
            self.ack(seq)
            threading.Timer(1.2, self._vacuum_ok).start()
        elif cmd == "VACUUM_OFF":
            self.leak = False
            self.vacuum = False
            if self.state in ("VACUUM_HOLD", "CPR_READY"):
                self.state = "HOMED"
            self.ack(seq)
            self.evt("VACUUM_RELEASED")
        elif cmd == "STIFFNESS_SCAN":
            if not self.require(seq, ("VACUUM_HOLD",), "NOT_FIXED"):
                return
            self.ack(seq)
            threading.Timer(1.5, self._scan_result,
                            [int(msg.get("points", 8))]).start()
        elif cmd == "SET_CP_POINT":
            if not self.require(seq, ("VACUUM_HOLD",), "NOT_FIXED"):
                return
            self.state = "CPR_READY"
            self.ack(seq)
        elif cmd == "TRIAL_COMPRESSION":
            if not self.require(seq, ("CPR_READY",), "NOT_READY"):
                return
            self.ack(seq)
            threading.Timer(1.0, self.evt, ["TRIAL_RESULT"]).start()
        elif cmd == "START_CPR":
            need = ("CPR_READY", "HOMED") if self.bench else ("CPR_READY",)
            if not self.require(seq, need, "NOT_READY"):
                return
            if self.target_depth <= 0:            # 미확정 목표값으로 구동 금지
                self.ack(seq, False, "2-CU-012", "TARGET_NOT_SET")
                return
            self.state = "CPR_RUNNING"
            self.ack(seq)
        elif cmd == "PAUSE_CPR":
            if not self.require(seq, ("CPR_RUNNING",), "NOT_RUNNING"):
                return
            self.state = "CPR_PAUSED"
            self.ack(seq)
        elif cmd == "RESUME_CPR":
            if not self.require(seq, ("CPR_PAUSED",), "NOT_PAUSED"):
                return
            self.state = "CPR_RUNNING"
            self.ack(seq)
        elif cmd == "STOP_CPR":
            if self.state in ("CPR_RUNNING", "CPR_PAUSED"):
                self.state = "HOMED"
            self.ack(seq)
        elif cmd == "RESET_FAULT":
            if self.fault:                        # 원인 미해소 → 거부
                self.ack(seq, False, self.fault, "FAULT_PERSISTS")
                return
            self.state = "IDLE"
            self.ack(seq)
        elif cmd == "SET_TEL_RATE":
            hz = msg.get("hz")
            if isinstance(hz, (int, float)) and 1 <= hz <= 50:
                self.tel_period = 1.0 / float(hz)
                self.ack(seq)
            else:
                self.ack(seq, False, "1-CM-004", "BAD_RATE")
        else:
            self.ack(seq, False, "1-CM-005", "UNKNOWN_COMMAND")

    def _homed(self):
        if self.state == "ARMED":
            self.state = "HOMED"
            self.evt("HOMING_DONE")

    def _vacuum_ok(self):
        if self.state == "HOMED":
            self.state = "VACUUM_HOLD"
            self.vacuum = True
            self.evt("VACUUM_OK")

    def _scan_result(self, points):
        stiff = [round(1.0 + self.rng.uniform(0, 1.6), 2) for _ in range(points)]
        best = stiff.index(max(stiff))
        self.evt("SCAN_RESULT", points=points, best_idx=best, stiff=stiff)

    # ── 고장 주입 ───────────────────────────────────────────
    def inject(self, what):
        if what == "oc":
            self.err("3-CU-021", 3, "OVER_CURRENT")
        elif what == "estop":
            self.estop = True
            self.state = "ESTOP"
            self.err("3-MU-002", 3, "ESTOP_ACTIVE")
        elif what == "leak":
            self.leak = True
            self.err("2-VU-005", 2, "VACUUM_LEAK")
        elif what == "vloss":
            self.err("3-VU-006", 3, "VACUUM_LOST")
        elif what == "drverr":
            self.err("2-VU-007", 2, "VACUUM_DRIVER_ERR")
        elif what == "hot":
            self.err("2-MU-011", 2, "OVER_TEMPERATURE")
        elif what == "drop":
            self.dropped = True
        elif what == "slow":
            self.tel_period = 1.0
        elif what == "ok":
            self.dropped = False
            self.leak = False
            self.vacuum = False
            self.estop = False
            self.fault = None
            self.tel_period = 1.0 / 20
            self.state = "IDLE"
        else:
            return False
        return True

    # ── 루프 ────────────────────────────────────────────────
    def read_loop(self):
        while self.running:
            try:
                data = os.read(self.master, 512)
            except OSError:
                break
            if not data:
                continue
            self._buf += data
            while b"\n" in self._buf:
                raw, self._buf = self._buf.split(b"\n", 1)
                if len(raw) > MAX_LINE:
                    self.err("1-CM-003", 1, "LINE_TOO_LONG", latched=False)
                    continue
                try:
                    msg = json.loads(raw.decode("utf-8", "replace").strip() or "{}")
                except ValueError:
                    self.err("1-CM-003", 1, "PARSE_FAIL", latched=False)
                    continue
                with self.lock:
                    self.handle(msg)

    def run(self):
        threading.Thread(target=self.read_loop, daemon=True).start()
        self.evt("BOOT")
        while self.running:
            time.sleep(self.tel_period)
            with self.lock:
                self.telemetry()


def main():
    ap = argparse.ArgumentParser(description="Teensy 4.1 모의 장치")
    ap.add_argument("--hz", type=float, default=20, help="텔레메트리 주기")
    ap.add_argument("--port", default=None, help="이 경로에 심볼릭 링크 생성")
    ap.add_argument("--seed", type=int, default=None)
    ap.add_argument("--scenario", default=None,
                    help="자동 고장 주입. 예: overcurrent@15 / estop@8 / drop@20")
    ap.add_argument("--fifo", default=None,
                    help="고장 주입용 FIFO 경로. 백그라운드 실행 시 사용")
    ap.add_argument("--quiet", action="store_true")
    ap.add_argument("--fw", choices=("1.0", "1.1"), default="1.1",
                    help="1.1(기본): 프로토콜 v1.1 모의(fake_teensy_v11.py). 1.0: v1.0 펌웨어 모의(판 불일치 시험용)")
    ap.add_argument("--sim-settings", nargs="?", const="__default__", default="__default__",
                    help="v1.1 설정값 JSON(시뮬레이션 전용). 기본 teensy_settings.sim.json")
    ap.add_argument("--no-settings", action="store_true",
                    help="v1.1 설정값을 모두 비운다(실물 펌웨어 기본과 같이 ARM·HOME·탐색 명령을 거부)")
    ap.add_argument("--time-scale", type=float, default=1.0, help="v1.1 동작 소요 시간 배율")
    ap.add_argument("--bench", action="store_true",
                    help="결선 시연 제안(D-51): 흡착 없이 HOMED 에서 START_CPR 허용. 실물 펌웨어에는 아직 없다(한건민 합의 전)")
    a = ap.parse_args()

    if a.fw == "1.1":
        from fake_teensy_v11 import FakeTeensy11
        settings = None
        if a.sim_settings and not a.no_settings:
            path = a.sim_settings
            if path == "__default__":
                path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "teensy_settings.sim.json")
            with open(path) as f:
                settings = dict((k, v) for k, v in json.load(f).items() if not k.startswith("_"))
            print("[주의] 시뮬레이션 설정을 불러왔습니다(%s). 실기에 쓰지 마세요." % path)
        fake = FakeTeensy11(tel_hz=a.hz, seed=a.seed, settings=settings, time_scale=a.time_scale, bench=a.bench)
    else:
        fake = FakeTeensy(tel_hz=a.hz, seed=a.seed, bench=a.bench)
    link = None
    if a.port:
        try:
            if os.path.islink(a.port) or os.path.exists(a.port):
                os.remove(a.port)
            os.symlink(fake.port, a.port)
            link = a.port
        except OSError as e:
            print("[warn] 심볼릭 링크 실패: %s" % e)

    print("=" * 58)
    print(" 가짜 Teensy 준비됨")
    print("   포트 : %s" % fake.port)
    if link:
        print("   링크 : %s" % link)
    print("   주기 : %.0f Hz" % a.hz)
    print("   고장 주입: oc / estop / leak / vloss / drverr / hot / drop / slow / ok / q")

    print("=" * 58)

    if a.scenario:
        name, _, delay = a.scenario.partition("@")
        alias = {"overcurrent": "oc", "vacuum_loss": "vloss", "comm_loss": "drop"}
        threading.Timer(float(delay or 5),
                        fake.inject, [alias.get(name, name)]).start()

    def apply(cmd):
        c = cmd.strip().lower()
        if not c:
            return True
        if c in ("q", "quit", "exit"):
            fake.running = False
            return True
        with fake.lock:
            ok = fake.inject(c)
        if not a.quiet:
            print("  → %s" % ("주입: " + c if ok else "알 수 없는 입력: " + c))
        return ok

    # FIFO 주입 — 백그라운드 실행에서도 동작한다
    if a.fifo:
        try:
            if os.path.exists(a.fifo):
                os.remove(a.fifo)
            os.mkfifo(a.fifo)
            print("   주입 FIFO : echo oc > %s" % a.fifo)
        except OSError as e:
            print("[warn] FIFO 생성 실패: %s" % e)
            a.fifo = None

    if a.fifo:
        def fifo_loop():
            while fake.running:
                try:
                    with open(a.fifo, "r") as f:      # 쓰는 쪽이 붙을 때까지 대기
                        for line in f:
                            apply(line)
                except OSError:
                    time.sleep(0.5)
        threading.Thread(target=fifo_loop, daemon=True).start()

    # stdin 주입.
    #  - 파이프·파일·/dev/null → 읽어도 안전
    #  - 터미널 → 포그라운드 프로세스 그룹일 때만. 백그라운드에서 터미널을
    #    읽으면 SIGTTIN 으로 프로세스가 정지된다.
    try:
        if sys.stdin is None or sys.stdin.closed:
            can_read_stdin = False
        elif not sys.stdin.isatty():
            can_read_stdin = True
        else:
            can_read_stdin = (os.getpgrp() == os.tcgetpgrp(sys.stdin.fileno()))
    except (OSError, ValueError, AttributeError):
        can_read_stdin = False

    if can_read_stdin:
        def stdin_loop():
            for line in sys.stdin:
                if not apply(line):
                    continue
                if not fake.running:
                    break
        threading.Thread(target=stdin_loop, daemon=True).start()
    elif not a.fifo and not a.quiet:
        print("   (터미널을 읽을 수 없는 백그라운드 실행 — 주입하려면 --fifo 사용)")
    signal.signal(signal.SIGINT, lambda *_: setattr(fake, "running", False))

    try:
        fake.run()
    finally:
        if link:
            try:
                os.remove(link)
            except OSError:
                pass
        print("\n가짜 Teensy 종료")


if __name__ == "__main__":
    main()
