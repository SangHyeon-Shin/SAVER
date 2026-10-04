#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""fake_teensy_v11.py — 프로토콜 v1.1 초안(docs/protocol_v1.1_draft.md, 7차 합의본) 모의 Teensy

v1.0 모의 장치(fake_teensy.FakeTeensy)를 상속해 탐색 후 고정(D-11)에 필요한 부분만 구현한다.
펌웨어 구현(한건민)이 아니다. Jetson 개발과 시험용이며, 동작이 펌웨어와 다르면 펌웨어가 기준이다.

구현한 것
  - GET_INFO(INFO 이벤트), tel 의 tn·boot_id, 매 주기 mot
  - bid·aid 판단 순서(§2.5): AID_REQUIRED, BOOT_MISMATCH, 같은 aid 재전송, SEQ_CONFLICT, STALE_SEQ, AID_EXHAUSTED
    기록 칸 act_cur·act_rej·cmd_last, 정지 방향 명령 예외(정지 출력 먼저, 기록 사유로 거부하지 않음)
  - 동작 명령: HOME(ARMED·HOMED·VACUUM_HOLD), HEAD_HOLD, DRIVE_STEP, SET_CP_POINT(offset_mm=0, 접촉 영점)
    한 번에 하나(busy), 완료 이벤트 HOMING_DONE·HEAD_HELD·DRIVE_DONE·CP_ZERO_SET
  - DRIVE_STOP(제동), DISARM 음압 유지 판정과 rearm_lock, VACUUM_OFF 해제 확인(vac_released)과 VACUUM_RELEASED,
    해제 시간 초과 2-VU-008, ARM 의 VACUUM_NOT_RELEASED
  - 설정값이 없으면 해당 명령 거부(LIMIT_UNSET, 2-TU-008). 기본값은 모두 미설정이다
  - 모의 반력: Phantom(경질부 위에서 높고 말단을 지나면 낮아지는 강성, 잡음)

구현하지 않은 것: GET_FAULTS, 엔코더·IMU·기울기 고장, SETTLE_FAIL·SHORT, STIFFNESS_SCAN·TRIAL_COMPRESSION 의 busy,
압박 중 동작. 이 값들은 펌웨어 시험(teensy/test_saver_proto.cpp)과 실물 시험이 맡는다.

모든 수치(Phantom, 시뮬레이션 설정)는 시뮬레이션 전용이며 의학적·공학적 근거가 없다.
"""
import math
import threading
import time

from fake_teensy import FakeTeensy, PROTO_VERSION

QUERY = ("PING", "GET_STATUS", "GET_INFO", "GET_FAULTS")
STOP = ("DRIVE_STOP", "STOP_CPR", "PAUSE_CPR", "DISARM")
ACTION = ("HOME", "HEAD_HOLD", "DRIVE_STEP", "SET_CP_POINT", "TRIAL_COMPRESSION", "STIFFNESS_SCAN")
IMMEDIATE = ("ARM", "SET_TARGET", "VACUUM_ON", "VACUUM_OFF", "START_CPR", "RESUME_CPR",
             "RESET_FAULT", "SET_TEL_RATE")
AID_LIMIT = 4294967295
BUSY_NAME = {"HOME": "HOMING", "HEAD_HOLD": "HEAD_SEARCH", "DRIVE_STEP": "DRIVE", "SET_CP_POINT": "CP_ZERO"}
DONE_EVT = {"HOME": "HOMING_DONE", "HEAD_HOLD": "HEAD_HELD", "DRIVE_STEP": "DRIVE_DONE", "SET_CP_POINT": "CP_ZERO_SET"}
REARM_BLOCKED = ("ARM", "HOME", "HEAD_HOLD", "DRIVE_STEP", "SET_CP_POINT", "SET_TARGET", "START_CPR",
                 "RESUME_CPR", "TRIAL_COMPRESSION", "VACUUM_ON")

# 설정값(초안 §7.1). None = 미설정 → 관련 명령 거부
SETTING_KEYS = (
    "limit_top_ok",
    "drv_speed_mm_s", "drv_speed_max_mm_s", "drv_step_max_mm", "drv_timeout_ms",
    "drv_settle_ms", "drv_settle_max_ms", "drv_avg_ms", "drv_avg_min_n", "drv_pos_tol_mm",
    "force_stale_ms", "scan_force_max_N", "contact_force_N", "contact_confirm_ms",
    "head_search_speed_mm_s", "head_search_max_mm", "head_timeout_ms", "head_press_max_mm",
    "vac_release_kPa", "vac_release_ms", "vac_release_timeout_ms",
)
HEAD_KEYS = ("scan_force_max_N", "contact_force_N", "contact_confirm_ms", "head_search_speed_mm_s",
             "head_search_max_mm", "head_timeout_ms", "force_stale_ms")
DRIVE_KEYS = ("drv_speed_mm_s", "drv_speed_max_mm_s", "drv_step_max_mm", "drv_timeout_ms", "drv_settle_ms",
              "drv_settle_max_ms", "drv_avg_ms", "drv_avg_min_n", "drv_pos_tol_mm", "scan_force_max_N",
              "force_stale_ms")

VAC_TARGET_KPA = -22.5        # PROJECT_STATE §2 펌웨어 확정값
VAC_MIN_START_KPA = -15.0     # PROJECT_STATE §2 펌웨어 확정값
LOADCELL_SPS = 80.0           # 시뮬레이션 신규 변환 속도


class Phantom(object):
    """정중선 위치 x(mm, 주행 방향 +)에 따른 모의 반력. 시뮬레이션 전용.

    surface_mm: HOME 원점에서 모사체 표면까지 헤드 하강량
    tip_mm: 장치 시작 위치(x=0)에서 경질부가 끝나는 위치(검상돌기 말단 모사)
    k_hard, k_soft: 경질부, 연질부 유효 강성(N/mm). width_mm: 전이 폭
    """

    def __init__(self, surface_mm=35.0, tip_mm=60.0, k_hard=4.0, k_soft=1.2,
                 width_mm=8.0, noise_N=0.4, rng=None):
        self.surface_mm = surface_mm
        self.tip_mm = tip_mm
        self.k_hard = k_hard
        self.k_soft = k_soft
        self.width_mm = width_mm
        self.noise_N = noise_N
        self.rng = rng

    def k(self, x):
        z = (self.tip_mm - x) / (self.width_mm / 4.0)
        z = max(-40.0, min(40.0, z))
        return self.k_soft + (self.k_hard - self.k_soft) / (1.0 + math.exp(-z))

    def force(self, x, depth_mm):
        noise = self.rng.gauss(0.0, self.noise_N) if self.rng else 0.0
        if depth_mm <= 0:
            return noise
        return self.k(x) * depth_mm + noise


class FakeTeensy11(FakeTeensy):

    def __init__(self, tel_hz=20, seed=None, settings=None, boot_id=None, phantom=None,
                 auto=True, time_scale=1.0, outbox=None, vac_residual_kPa=0.0):
        super(FakeTeensy11, self).__init__(tel_hz=tel_hz, seed=seed)
        self.fw = "1.1.0-sim"
        self.caps = ["DRIVE", "HEAD_HOLD", "CP_ZERO", "MOT", "AID"]
        self.boot_id = boot_id if boot_id is not None else self.rng.randint(1, 65535)
        self.settings = dict((k, None) for k in SETTING_KEYS)
        self.settings.update(settings or {})
        self.phantom = phantom or Phantom(rng=self.rng)
        if self.phantom.rng is None:
            self.phantom.rng = self.rng
        self.auto = auto                  # False 면 finish_job() 을 불러야 동작이 끝난다(단위 시험)
        self.time_scale = time_scale      # 동작 소요 시간 배율(시험 가속)
        self.outbox = outbox              # 리스트를 주면 pty 대신 여기에 쌓는다

        # §2.5 기록
        self.aid_max = 0
        self.act_cur = None
        self.act_rej = None
        self.cmd_last = None
        self.last_seq = None

        # 동작
        self.busy = "NONE"
        self.job = None
        self._job_token = 0

        # 헤드(HOME 원점 기준 하강 양수). HOME 전에는 원점이 없다(None)
        self.head_pos = None
        self.head_hold_active = False
        self.head_contact = None
        self.cp_zero_set = False
        self.press_depth = 0.0            # 접촉점에서 더 누른 깊이

        # 주행(장치 실제 위치 _x, 주행 원점 기준 보고 위치 drv_pos)
        self._x = 0.0
        self.drv_origin = None
        self.drv_settled = True

        # 음압
        self.vac_output = False
        self.vac_target = VAC_TARGET_KPA
        self.vac_p = 0.0
        self.vac_residual = vac_residual_kPa    # 출력을 꺼도 남는 음압(체크밸브 모사)
        self.vac_released = False         # 부팅 직후 false(§4.6)
        self._rel_ms = 0.0
        self.rearm_lock = False
        self.pending_release = None       # (seq, aid, 수락 시각 ms, 시간 초과 보고 여부)

        self.tn = 0
        self._last_tick = time.time()

    # ── 송신 ────────────────────────────────────────────────
    def _send(self, msg):
        if self.outbox is not None:
            if not self.dropped:
                msg.setdefault("v", PROTO_VERSION)
                self.outbox.append(msg)
            return
        super(FakeTeensy11, self)._send(msg)

    def _ack(self, seq, ok=True, err=None, why=None):
        m = {"t": "ack", "seq": seq, "ok": ok}
        if ok:
            m["state"] = self.state
        else:
            m["err"] = err or "2-CU-011"
            m["why"] = why or ""
        self._send(dict(m))
        return m

    # ── 시간 진행 (음압, 해제 확인) ─────────────────────────
    def tick(self, dt=None):
        now = time.time()
        if dt is None:
            dt = now - self._last_tick
        self._last_tick = now
        target = self.vac_target if self.vac_output else self.vac_residual
        tau = 0.25
        self.vac_p += (target - self.vac_p) * (1.0 - math.exp(-dt / tau))

        # VACUUM_ON 뒤 시작 한계보다 강하면 고정 완료
        if self.vac_output and self.state == "HOMED" and self.vac_p <= VAC_MIN_START_KPA:
            self.state = "VACUUM_HOLD"
            self.evt("VACUUM_OK")

        # 해제 확인(§4.6): 출력 꺼짐 + 판정값 설정 + |p| ≤ 기준이 vac_release_ms 동안 연속
        thr, need = self.settings.get("vac_release_kPa"), self.settings.get("vac_release_ms")
        if (not self.vac_output) and thr and need and abs(self.vac_p) <= thr:
            self._rel_ms += dt * 1000.0
        else:
            self._rel_ms = 0.0
        self.vac_released = bool(thr and need and self._rel_ms >= need)

        if self.pending_release:
            seq, aid, at_ms, reported = self.pending_release
            if self.vac_released:
                self.pending_release = None
                if self.state not in ("FAULT", "ESTOP"):
                    self.state = "IDLE"
                self.rearm_lock = False
                self.evt("VACUUM_RELEASED", seq=seq, aid=aid, vac_kPa=round(self.vac_p, 2))
            else:
                limit = self.settings.get("vac_release_timeout_ms")
                if limit and not reported and self.ms() - at_ms > limit:
                    self.pending_release = (seq, aid, at_ms, True)
                    self.err("2-VU-008", 2, "VACUUM_RELEASE_TIMEOUT", latched=False)

    # ── 텔레메트리 ──────────────────────────────────────────
    def telemetry(self):
        self.tick()
        self.tn += 1
        ms = self.ms()
        t = time.time() - self.t0
        m = {"t": "tel", "ms": ms, "tn": self.tn, "boot_id": self.boot_id, "state": self.state}
        if self.state == "CPR_RUNNING":
            m["cp_rate_cpm"] = round(self.target_bpm + math.sin(t * 0.7) * 1.2, 2)
            m["cp_depth_mm"] = round(self.target_depth + math.sin(t * 1.9) * 1.4, 2)
            m["cp_force_N"] = round(380 + math.sin(t * 1.9) * 45, 2)
        elif self.head_hold_active:
            m["cp_force_N"] = round(self.phantom.force(self._x, self.press_depth), 2)
        m["vac_kPa"] = round(self.vac_p + self.rng.uniform(-0.2, 0.2), 2)
        m["temp_C"] = 28.0
        m["estop"] = self.estop
        m["limit_top"] = self.head_pos == 0.0
        m["limit_bottom"] = False
        m["vac_leak"] = self.leak
        m["faults"] = [self.fault] if self.fault else []
        self._send(m)
        self._send(self.mot_msg(ms))

    def mot_msg(self, ms=None):
        m = {"t": "mot", "ms": self.ms() if ms is None else ms, "tn": self.tn, "boot_id": self.boot_id,
             "busy": self.busy, "head_hold_active": self.head_hold_active,
             "drv_settled": self.drv_settled,
             "rearm_lock": self.rearm_lock, "vac_released": self.vac_released}
        if self.job:
            m["busy_seq"], m["busy_aid"] = self.job["seq"], self.job["aid"]
        if self.drv_origin is not None:
            m["drv_pos_mm"] = round(self._x - self.drv_origin, 2)
            m["drv_yaw_deg"] = 0.0
        if self.head_pos is not None:
            m["head_pos_mm"] = round(self.head_pos, 2)
        if self.head_contact is not None:
            m["head_contact_mm"] = round(self.head_contact, 2)
        if self.act_cur and self.act_cur.get("done"):
            m["mot_last_seq"] = self.act_cur["seq"]
            m["mot_last_aid"] = self.act_cur["aid"]
            m["mot_last_cmd"] = self.act_cur["cmd"]
            m["mot_last_result"] = self.act_cur["done"].get("result")
        return m

    # ── 명령 처리 ───────────────────────────────────────────
    def _norm(self, cmd, msg):
        p = {}
        for k, v in msg.items():
            if k in ("v", "t", "seq", "cmd", "bid", "aid"):
                continue
            if isinstance(v, float) or (k.endswith("_mm") and isinstance(v, (int, float))):
                v = round(float(v), 2)
            p[k] = v
        if cmd == "DRIVE_STEP" and "speed_mm_s" not in p and self.settings.get("drv_speed_mm_s"):
            p["speed_mm_s"] = round(float(self.settings["drv_speed_mm_s"]), 2)
        if cmd == "HEAD_HOLD" and "drv_zero" not in p:
            p["drv_zero"] = False
        return p

    def handle(self, msg):
        if msg.get("t") != "cmd":
            return
        seq, cmd = msg.get("seq"), msg.get("cmd")
        if seq is None or not isinstance(cmd, str) or not cmd:
            return
        is_stop = cmd in STOP
        if is_stop:
            self._stop_output(cmd)        # 구조 검사를 통과한 정지 명령은 출력부터 멈춘다(§2.5-0)
        if msg.get("v") != PROTO_VERSION:
            self.err("3-CM-001", 3, "VERSION_MISMATCH")
            return
        if cmd in QUERY:
            return self._query(seq, cmd)
        if self.state == "ESTOP" and cmd != "DRIVE_STOP":
            self._ack(seq, False, "3-MU-002", "ESTOP_ACTIVE")
            return

        params = self._norm(cmd, msg)
        bid, aid = msg.get("bid"), msg.get("aid")
        if not isinstance(aid, int) or aid <= 0 or bid is None:
            return self._unrecorded(seq, cmd, params, "AID_REQUIRED")
        if bid != self.boot_id:
            return self._unrecorded(seq, cmd, params, "BOOT_MISMATCH")
        for rec in (self.act_cur, self.cmd_last):
            if rec and rec["aid"] == aid:
                return self._replay(seq, cmd, params, rec)
        if self.act_rej and self.act_rej["aid"] == aid:
            return self._replay(seq, cmd, params, self.act_rej)
        if aid <= self.aid_max:
            return self._unrecorded(seq, cmd, params, "STALE_SEQ")
        if self.aid_max >= AID_LIMIT:
            return self._unrecorded(seq, cmd, params, "AID_EXHAUSTED")

        self.aid_max = aid
        self.last_seq = seq
        rec = {"aid": aid, "seq": seq, "cmd": cmd, "params": params}
        if cmd in ACTION:
            self._eval_action(rec)
        else:
            self._eval_immediate(rec)

    def _unrecorded(self, seq, cmd, params, why):
        """기록하지 않는 거부. 정지 방향 명령은 거부하지 않고 실행만 한다(기록 갱신 없음)."""
        if cmd in STOP:
            ok, err, w = self._exec_stop(cmd, params)
            self._ack(seq, ok, err, w)
            return
        self._ack(seq, False, "2-CU-011", why)

    def _replay(self, seq, cmd, params, rec):
        if rec["seq"] == seq and rec["cmd"] == cmd and rec["params"] == params:
            self._send(dict(rec["ack"]))          # 재실행하지 않고 원래 ACK
            if rec.get("done"):
                self._send(dict(rec["done"]))     # 끝났으면 저장된 완료 이벤트
            return
        self._ack(seq, False, "2-CU-011", "SEQ_CONFLICT")

    def _query(self, seq, cmd):
        if cmd == "PING":
            self._ack(seq)
        elif cmd == "GET_STATUS":
            self._ack(seq)
            self.telemetry()
        elif cmd == "GET_INFO":
            self._ack(seq)
            m = {"seq": seq, "fw": self.fw, "proto": "1.1", "caps": list(self.caps), "boot_id": self.boot_id}
            if self.last_seq is not None:
                m["last_seq"] = self.last_seq
            if self.aid_max:
                m["last_aid"] = self.aid_max
            self.evt("INFO", **m)
        else:                                      # GET_FAULTS 는 구현하지 않음
            self._ack(seq, False, "1-CM-005", "UNKNOWN_COMMAND")

    # ── 동작 명령 ───────────────────────────────────────────
    def _unset(self, keys):
        return [k for k in keys if not self.settings.get(k)]

    def _head_home(self):
        return self.head_pos == 0.0 and not self.head_hold_active

    def _check_action(self, cmd, p):
        if self.rearm_lock and cmd in REARM_BLOCKED:
            return ("2-CU-011", "REARM_LOCKED")
        if self.state == "FAULT":
            return ("2-CU-011", "FAULT_ACTIVE")
        if cmd == "HOME":
            if self.state not in ("ARMED", "HOMED", "VACUUM_HOLD"):
                return ("2-CU-011", "NOT_ARMED")
            if not self.settings.get("limit_top_ok"):
                return ("2-CU-011", "HOME_UNAVAILABLE")
        elif cmd == "HEAD_HOLD":
            if self.state != "HOMED":
                return ("2-CU-011", "NOT_HOMED")
            if self._unset(HEAD_KEYS + ("head_press_max_mm",)):
                return ("2-TU-008", "LIMIT_UNSET")
            if self.head_hold_active:
                return ("2-CU-011", "ALREADY_HOLDING")
            if not self._head_home():
                return ("2-CU-011", "HEAD_NOT_READY")
            press = p.get("press_mm")
            if not isinstance(press, (int, float)) or press <= 0 or press > self.settings["head_press_max_mm"]:
                return ("2-CU-011", "PRESS_RANGE")
        elif cmd == "DRIVE_STEP":
            if self.state != "HOMED":
                return ("2-CU-011", "NOT_HOMED")
            if self._unset(DRIVE_KEYS):
                return ("2-TU-008", "LIMIT_UNSET")
            if not (self._head_home() or self.head_hold_active):
                return ("2-CU-011", "HEAD_NOT_READY")
            if not self.drv_settled:
                return ("2-CU-011", "NOT_SETTLED")
            d = p.get("dist_mm")
            if not isinstance(d, (int, float)) or d == 0 or abs(d) > self.settings["drv_step_max_mm"]:
                return ("2-CU-011", "DIST_RANGE")
            if p.get("speed_mm_s", 0) <= 0 or p["speed_mm_s"] > self.settings["drv_speed_max_mm_s"]:
                return ("2-CU-011", "SPEED_RANGE")
        elif cmd == "SET_CP_POINT":
            if self.state != "VACUUM_HOLD":
                return ("2-CU-011", "NOT_FIXED")
            if self._unset(HEAD_KEYS):
                return ("2-TU-008", "LIMIT_UNSET")
            if not self._head_home():
                return ("2-CU-011", "HEAD_NOT_READY")
        elif cmd in ("TRIAL_COMPRESSION", "STIFFNESS_SCAN"):
            need = "CPR_READY" if cmd == "TRIAL_COMPRESSION" else "VACUUM_HOLD"
            if self.state != need:
                return ("2-CU-011", "NOT_READY")
            if cmd == "TRIAL_COMPRESSION" and not self.cp_zero_set:
                return ("2-CU-011", "CP_ZERO_UNSET")
        return None

    def _eval_action(self, rec):
        cmd, p = rec["cmd"], rec["params"]
        reason = ("2-CU-011", "BUSY") if self.busy != "NONE" else self._check_action(cmd, p)
        if reason:
            rec["ack"] = self._ack(rec["seq"], False, reason[0], reason[1])
            self.act_rej = rec
            return
        if cmd in ("TRIAL_COMPRESSION", "STIFFNESS_SCAN"):      # busy 미구현(레거시)
            rec["ack"] = self._ack(rec["seq"])
            rec["done"] = {"t": "evt", "ms": self.ms(), "evt": "TRIAL_RESULT" if cmd == "TRIAL_COMPRESSION"
                           else "SCAN_RESULT", "seq": rec["seq"], "aid": rec["aid"]}
            self.act_cur = rec
            self._send(dict(rec["done"]))
            return
        rec["done"] = None
        self.act_cur = rec
        self.busy = BUSY_NAME[cmd]
        self.job = {"seq": rec["seq"], "aid": rec["aid"], "cmd": cmd, "p": p, "t0": time.time(),
                    "dur": self._duration(cmd, p)}
        if cmd == "HOME" and self.state == "VACUUM_HOLD":
            self.cp_zero_set = False                # 복귀 시작 순간 영점 무효(§2.2)
        if cmd == "DRIVE_STEP":
            self.drv_settled = False
        rec["ack"] = self._ack(rec["seq"])
        self._job_token += 1
        if self.auto:
            tok = self._job_token
            timer = threading.Timer(self.job["dur"] * self.time_scale, self._auto_finish, [tok])
            timer.daemon = True
            timer.start()

    def _duration(self, cmd, p):
        s = self.settings
        if cmd == "HOME":
            return 0.3 + (self.head_pos or 0.0) / 40.0
        if cmd in ("HEAD_HOLD", "SET_CP_POINT"):
            depth = self.phantom.surface_mm + p.get("press_mm", 0.0)
            return depth / float(s["head_search_speed_mm_s"])
        if cmd == "DRIVE_STEP":
            return (abs(p["dist_mm"]) / float(p["speed_mm_s"])
                    + s["drv_settle_ms"] / 1000.0 + s["drv_avg_ms"] / 1000.0)
        return 0.5

    def _auto_finish(self, token):
        with self.lock:
            if self.job and token == self._job_token:
                self.finish_job()

    def finish_job(self, result="OK"):
        """현재 동작을 끝낸다. 시험에서는 직접 부르고, 실행 중에는 타이머가 부른다."""
        job = self.job
        if not job:
            return None
        cmd, p = job["cmd"], job["p"]
        ev = {"t": "evt", "ms": self.ms(), "evt": DONE_EVT[cmd], "seq": job["seq"], "aid": job["aid"]}
        s = self.settings
        if result == "OK" and cmd == "HOME":
            self.head_pos = 0.0
            self.head_hold_active = False
            self.press_depth = 0.0
            if self.state == "ARMED":
                self.state = "HOMED"
        elif result == "OK" and cmd in ("HEAD_HOLD", "SET_CP_POINT"):
            contact = self.phantom.surface_mm
            if contact > s["head_search_max_mm"]:
                result = "NO_CONTACT"
                self.err("2-TU-006", 2, "NO_CONTACT", latched=False)
                self.head_pos = 0.0
            else:
                self.head_contact = contact
                if cmd == "HEAD_HOLD":
                    self.press_depth = float(p["press_mm"])
                    self.head_pos = contact + self.press_depth
                    f = self.phantom.force(self._x, self.press_depth)
                    if f > s["scan_force_max_N"]:
                        return self._fault_job(ev, "3-TU-003", "SCAN_FORCE_MAX")
                    self.head_hold_active = True
                    if p.get("drv_zero"):
                        self.drv_origin = self._x
                    ev["head_contact_mm"] = round(contact, 2)
                    ev["head_pos_mm"] = round(self.head_pos, 2)
                    ev["cp_force_N"] = round(f, 2)
                else:
                    self.head_pos = contact
                    self.cp_zero_set = True
                    self.state = "CPR_READY"
                    ev["head_contact_mm"] = round(contact, 2)
        elif cmd == "DRIVE_STEP":
            frac = 1.0
            if result == "STOPPED":
                frac = min(1.0, (time.time() - job["t0"]) / max(job["dur"] * self.time_scale, 1e-6)) \
                    if self.auto else 0.0
            moved = float(p["dist_mm"]) * frac
            self._x += moved
            self.drv_settled = True
            ev["drive_mm"] = round(moved, 2) + 0.0      # -0.0 표기 방지
            if self.drv_origin is not None:
                ev["drv_pos_mm"] = round(self._x - self.drv_origin, 2)
                ev["drv_yaw_deg"] = 0.0
            if result == "OK":
                n = int(s["drv_avg_ms"] / 1000.0 * LOADCELL_SPS)
                depth = self.press_depth if self.head_hold_active else 0.0
                samples = [self.phantom.force(self._x, depth) for _ in range(max(n, 0))]
                if any(f > s["scan_force_max_N"] for f in samples):
                    return self._fault_job(ev, "3-TU-003", "SCAN_FORCE_MAX")
                if n >= s["drv_avg_min_n"]:
                    ev["cp_force_N"] = round(sum(samples) / float(n), 2)
                ev["force_n"] = n
        elif result == "STOPPED" and cmd == "HOME" and self.state in ("ARMED", "HOMED"):
            self.state = "ARMED"                    # 중단된 HOME → ARMED(§4.5)
        ev["result"] = result
        return self._end_job(ev)

    def _fault_job(self, ev, code, why):
        # sev 3: 출력부터 끊고(헤드 유지 해제) 보고한다
        self.head_hold_active = False
        self.err(code, 3, why)
        ev["result"] = "FAULT"
        ev["code"] = code
        return self._end_job(ev)

    def _end_job(self, ev):
        if self.act_cur and self.act_cur["aid"] == self.job["aid"]:
            self.act_cur["done"] = ev            # 결과를 기록에 먼저 저장하고(§2.5 busy 범위)
        self.job = None
        self.busy = "NONE"
        self._send(dict(ev))                     # 그 뒤 송신
        return ev

    # ── 정지 방향 ───────────────────────────────────────────
    def _stop_output(self, cmd):
        if cmd in ("DRIVE_STOP", "DISARM") and self.job:
            self.finish_job("STOPPED")
        if cmd in ("STOP_CPR", "PAUSE_CPR", "DISARM") and self.job and self.job["cmd"] != "DRIVE_STEP":
            self.finish_job("STOPPED")

    def _exec_stop(self, cmd, p):
        """정지 출력은 이미 _stop_output 에서 인가했다. 여기서는 상태 전이와 ACK 내용만 정한다."""
        if cmd == "DRIVE_STOP":
            return True, None, None
        if self.state == "ESTOP":
            return False, "3-MU-002", "ESTOP_ACTIVE"
        if cmd == "STOP_CPR":
            if self.state not in ("CPR_RUNNING", "CPR_PAUSED"):
                return False, "2-CU-011", "NOT_RUNNING"
            self.state = "HOMED"                 # v1.0 §7. 음압 출력은 그대로(VACUUM_OFF 로만 해제)
            return True, None, None
        if cmd == "PAUSE_CPR":
            if self.state != "CPR_RUNNING":
                return False, "2-CU-011", "NOT_RUNNING"
            self.state = "CPR_PAUSED"
            return True, None, None
        # DISARM (§2.2): 음압 유지 판정
        if self.state == "FAULT":
            return False, "2-CU-011", "FAULT_ACTIVE"
        self.head_hold_active = False
        self.press_depth = 0.0
        if self.vac_output or not self.vac_released:
            self.state = "VACUUM_HOLD"
            self.rearm_lock = True
            self.drv_origin = None
            self.cp_zero_set = False
            self.head_pos = None                 # HOME 원점 무효
        else:
            self.state = "IDLE"
            self.drv_origin = None
            self.cp_zero_set = False
            self.head_pos = None
        return True, None, None

    # ── 즉시 변경 ───────────────────────────────────────────
    def _eval_immediate(self, rec):
        cmd, p, seq = rec["cmd"], rec["params"], rec["seq"]
        if cmd in STOP:
            ok, err, why = self._exec_stop(cmd, p)
            rec["ack"] = self._ack(seq, ok, err, why)
            self.cmd_last = rec
            return
        if self.busy != "NONE" and cmd != "SET_TEL_RATE":
            rec["ack"] = self._ack(seq, False, "2-CU-011", "BUSY")
            self.cmd_last = rec
            return
        ok, err, why = self._immediate(cmd, p)
        if ok and cmd == "VACUUM_OFF":
            # 대기 중인 해제 요청. 새 요청이 오면 바꾸고 시간 초과를 다시 잰다(§4.6)
            self.pending_release = (seq, rec["aid"], self.ms(), False)
        rec["ack"] = self._ack(seq, ok, err, why)
        self.cmd_last = rec

    def _immediate(self, cmd, p):
        if self.rearm_lock and cmd in REARM_BLOCKED:
            return False, "2-CU-011", "REARM_LOCKED"
        if cmd == "ARM":
            if self.state != "IDLE":
                return False, "2-CU-011", "NOT_IDLE"
            if not self.vac_released:
                return False, "2-CU-011", "VACUUM_NOT_RELEASED"
            self.state = "ARMED"
        elif cmd == "SET_TARGET":
            if self.state in ("IDLE", "FAULT"):
                return False, "2-CU-011", "NOT_ARMED"
            if "bpm" in p:
                self.target_bpm = float(p["bpm"])
            if "depth_mm" in p:
                self.target_depth = float(p["depth_mm"])
        elif cmd == "VACUUM_ON":
            if self.state != "HOMED":
                return False, "2-CU-011", "NOT_HOMED"
            if isinstance(p.get("target_kPa"), (int, float)):
                self.vac_target = float(p["target_kPa"])
            self.vac_output = True
        elif cmd == "VACUUM_OFF":
            self.vac_output = False             # 상태와 rearm_lock 은 해제 확인까지 그대로
        elif cmd == "START_CPR":
            if self.state != "CPR_READY":
                return False, "2-CU-011", "NOT_READY"
            if not self.cp_zero_set:
                return False, "2-CU-011", "CP_ZERO_UNSET"
            if self.target_depth <= 0:
                return False, "2-CU-012", "TARGET_NOT_SET"
            self.state = "CPR_RUNNING"
        elif cmd == "RESUME_CPR":
            if self.state != "CPR_PAUSED":
                return False, "2-CU-011", "NOT_PAUSED"
            self.state = "CPR_RUNNING"
        elif cmd == "RESET_FAULT":
            if self.fault:
                return False, self.fault, "FAULT_PERSISTS"
            if self.state == "FAULT":
                self.state = "IDLE"
                self.head_pos = None
                self.drv_origin = None
        elif cmd == "SET_TEL_RATE":
            hz = p.get("hz")
            if not (isinstance(hz, (int, float)) and 1 <= hz <= 50):
                return False, "1-CM-004", "BAD_RATE"
            self.tel_period = 1.0 / float(hz)
        else:
            return False, "1-CM-005", "UNKNOWN_COMMAND"
        return True, None, None

    # ── 고장 주입 ───────────────────────────────────────────
    def inject(self, what):
        if what == "ok":
            ok = super(FakeTeensy11, self).inject(what)
            self.head_hold_active = False
            self.head_pos = None
            self.drv_origin = None
            return ok
        if what == "estop":
            self._stop_output("DISARM")
        return super(FakeTeensy11, self).inject(what)
