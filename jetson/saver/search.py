# -*- coding: utf-8 -*-
"""search.py — 압박점 탐색: 검상돌기 말단 판정과 탐색 지휘

판정 규칙은 제출 보고서 6.2 를 그대로 옮긴다(프로토콜 v1.1 초안 §0.1-2·3, §7.2).

  말단 판정, 식 (5)
      r_i = 1 - F_i / Fref_i >= tip_drop_ratio,   Fref_i = (1/N) * sum(F_j, j = i-N .. i-1)
    머리 → 발 방향으로 진행하며 처음 성립하는 위치를 검상돌기 말단으로 본다.
    첫 급감에서 멈추지 않고 ROI 끝까지 취득한 뒤 판정한다. 뒤에 나온 후보는 기록만 한다.
  압박점 이동 거리, 식 (7)
      D = min(cp_dist_max_mm, cp_dist_ref_mm * EAPD / eapd_ref_mm)
    말단에서 머리 쪽으로 D 만큼 옮긴 곳이 압박점이다.
    EAPD 가 없으면 대체 상수를 쓰지 않는다. 탐색을 시작하지 않고 재측정을 안내한다.

  이 판정과 D 는 설계 가설이며 인체에서 검증되지 않았다.

지휘 순서 (SearchRunner)
  HOME → HEAD_HOLD(press_mm, drv_zero) → DRIVE_STEP 을 ROI 끝까지 반복 → 판정
  → HOME(헤드 올림) → DRIVE_STEP 으로 압박점까지 이동
  Jetson 은 목표 거리만 요청한다. 주행, 정지 확인, 반력 평균, 인터록은 Teensy 가 한다.
  동작 명령은 한 번에 하나만 보내고, mot 가 오래됐으면 보내지 않는다(초안 §3.3-2).

설정값은 코드 기본값이 모두 비어 있다. 하나라도 비어 있으면 탐색을 시작하지 않는다.
"""
import math
import threading
import time

from . import protocol as P

# 탐색에 필요한 Jetson 설정값. 비어 있으면(None, 0) 탐색을 시작하지 않는다.
PARAM_KEYS = (
    "press_mm",            # 접촉점에서 더 내리는 깊이(보고서 δ₀)
    "cp_dist_ref_mm",      # 식 (7) 기준 거리
    "eapd_ref_mm",         # 식 (7) 기준 EAPD
    "cp_dist_max_mm",      # 식 (7) 상한
    "tip_drop_ratio",      # 식 (5) 판정 기준(감소율)
    "tip_ref_n",           # 식 (5) 이동평균 개수 N. 보고서에 값 없음
    "scan_step_mm",        # 탐색 간격
    "scan_roi_mm",         # 탐색 구간(시작점부터 주행 거리)
    "scan_dir_sign",       # 머리 → 발 방향의 주행 부호(+1 또는 -1)
    "move_step_mm",        # 압박점 이동 1회 상한. Teensy drv_step_max_mm 이하
    "move_tol_mm",         # 압박점 도달 판정 허용 오차
    # 대기 한도 계산용(초안 §4.4). Teensy §7.1 설정값과 같아야 한다
    "drv_timeout_ms", "drv_settle_max_ms", "drv_avg_ms", "head_timeout_ms",
)

NEED_CAPS = ("DRIVE", "HEAD_HOLD", "MOT", "AID")
DONE_EVENTS = ("HOMING_DONE", "HEAD_HELD", "DRIVE_DONE")

MOT_WAIT_S = 2.0          # mot 가 오래됐을 때 동작 명령을 미루는 최대 시간
STATUS_WAIT_S = 1.0       # §4.4 GET_STATUS 응답 대기
STATUS_TRIES = 3

ACTIVE = ("home0", "hold", "scan", "home1", "move", "stopping")


def _num(v):
    return isinstance(v, (int, float)) and not isinstance(v, bool) and math.isfinite(v)


def check_params(cfg):
    """설정값 검사. (값 dict, 비었거나 잘못된 이름 목록)을 돌려준다."""
    p, bad = {}, []
    for k in PARAM_KEYS:
        v = cfg.get(k)
        if k == "scan_dir_sign":
            ok = _num(v) and v in (1, -1)
        elif k == "tip_ref_n":
            ok = _num(v) and v >= 1 and float(v) == int(v)
            v = int(v) if ok else v
        elif k == "tip_drop_ratio":
            ok = _num(v) and 0 < v < 1
        else:
            ok = _num(v) and v > 0
        if ok:
            p[k] = v
        else:
            bad.append(k)
    if not bad and p["scan_step_mm"] > p["scan_roi_mm"]:
        bad.append("scan_step_mm")
    if not bad and p["move_tol_mm"] >= p["move_step_mm"]:
        bad.append("move_tol_mm")
    return p, bad


def cp_distance(eapd_mm, p):
    """식 (7). EAPD 가 없거나 잘못됐으면 None(대체 상수 없음)."""
    if not _num(eapd_mm) or eapd_mm <= 0:
        return None
    return min(float(p["cp_dist_max_mm"]), float(p["cp_dist_ref_mm"]) * eapd_mm / float(p["eapd_ref_mm"]))


def detect_tip(xs, fs, n, ratio):
    """식 (5). xs, fs 는 탐색 순서(머리 → 발)의 위치와 반력.

    처음 r_i >= ratio 인 점을 말단으로 본다. 앞의 N 점은 기준이 없어 판정하지 않는다.
    기준 평균이 0 이하면(접촉 없음) 그 점은 판정하지 않는다.
    돌려주는 값: tip_index, tip_x(없으면 None), 점마다 ref·r, 후보 목록, 후보 묶음 수
    """
    ref, r, cand = [], [], []
    for i in range(len(fs)):
        if i < n:
            ref.append(None)
            r.append(None)
            continue
        m = sum(fs[i - n:i]) / float(n)
        if m <= 0:
            ref.append(m)
            r.append(None)
            continue
        ri = 1.0 - fs[i] / m
        ref.append(m)
        r.append(ri)
        if ri >= ratio:
            cand.append(i)
    groups = sum(1 for k, i in enumerate(cand) if k == 0 or cand[k - 1] != i - 1)
    tip = cand[0] if cand else None
    return {"tip_index": tip, "tip_x": xs[tip] if tip is not None else None,
            "ref": ref, "r": r, "candidates": cand, "groups": groups}


class SearchRunner(object):
    """탐색 지휘. 링크 콜백(on_ack, on_event)과 주기 tick 으로 진행한다.

    link 가 갖춰야 할 것(TeensyLink): proto, caps, boot_id, send, sent, resend, mot_fresh,
    status_pair, last_mot. 콜백은 잠금 밖에서 부른다(상태머신 잠금과 엇갈리지 않게).
      on_update(snapshot)          진행 상황(UI 표시용)
      on_done(ok, snapshot)        끝. ok=False 면 snapshot["reason"]
    """

    def __init__(self, link, cfg, on_update=None, on_done=None, logger=None, clock=time.monotonic):
        self.link = link
        self.cfg = cfg
        self.on_update = on_update or (lambda s: None)
        self.on_done = on_done or (lambda ok, s: None)
        self.log = logger
        self.clock = clock
        self._lock = threading.RLock()
        self._out = []
        self.phase = "idle"
        self._reset()

    def _reset(self):
        self.p = {}
        self.reason = None
        self.detail = None
        self.eapd_mm = None
        self.d_mm = None
        self.points = []           # [{"x", "F", "n"}]
        self.det = None
        self.target_x = None
        self.pos_x = None
        self.head = {}
        self.moved = False         # 장치에 동작 명령을 하나라도 보냈는지
        self._wait = None
        self._queued = None
        self._scan_cmd = 0.0
        self._move_n = 0
        self._move_max = 0
        self._stop_at = None
        self._cancel_reason = None

    # ── 공개 ────────────────────────────────────────────────
    @property
    def active(self):
        return self.phase in ACTIVE

    def start(self, eapd_mm):
        """탐색 시작. 시작하지 못하면 False 이고 on_done(False) 로 사유를 알린다."""
        with self._lock:
            if self.active:
                return False
            self._reset()
            self.phase = "check"
            p, bad = check_params(self.cfg)
            if bad:
                self._fail("PARAM_UNSET", ",".join(bad))
            elif self.link.proto != P.PROTO_REV:
                self._fail("FW_VERSION", str(self.link.proto))      # v1.1 펌웨어가 아님(판 불일치, 확인 전 포함)
            elif not set(NEED_CAPS) <= set(self.link.caps or ()):
                # v1.1 이지만 주행·헤드 기능을 알리지 않음(GET_INFO caps). 펌웨어에 구동 구현이 없는 경우
                self._fail("UNSUPPORTED", ",".join(c for c in NEED_CAPS if c not in (self.link.caps or ())))
            else:
                self.p = p
                d = cp_distance(eapd_mm, p)
                if d is None:
                    self._fail("EAPD_MISSING")
                else:
                    self.eapd_mm, self.d_mm = float(eapd_mm), d
                    self._enter("home0")
                    self._request("HOME")
            ok = self.phase != "failed"
            out, self._out = self._out, []
        self._flush(out)
        return ok

    def cancel(self, reason="USER_STOP"):
        """진행 중이면 DRIVE_STOP 을 보내고 중단 완료 이벤트를 기다린다."""
        with self._lock:
            if self.active and self.phase != "stopping":
                if self._wait:
                    self._halt(reason)
                else:
                    self._fail(reason)
            out, self._out = self._out, []
        self._flush(out)

    def on_ack(self, msg):
        with self._lock:
            w = self._wait
            if w and msg.get("seq") == w["seq"] and self.phase != "stopping":
                if msg.get("ok"):
                    w["acked"] = True
                else:
                    self._fail("REJECTED", "%s:%s:%s" % (w["cmd"], msg.get("why", ""), msg.get("err", "")))
            out, self._out = self._out, []
        self._flush(out)

    def on_event(self, msg):
        with self._lock:
            self._on_event(msg)
            out, self._out = self._out, []
        self._flush(out)

    def tick(self):
        with self._lock:
            self._tick()
            out, self._out = self._out, []
        self._flush(out)

    def snapshot(self):
        with self._lock:
            return self._snap()

    # ── 내부: 진행 ──────────────────────────────────────────
    def _on_event(self, msg):
        w = self._wait
        if not w or msg.get("evt") not in DONE_EVENTS or msg.get("seq") != w["seq"]:
            return
        if msg.get("aid") is not None and msg.get("aid") != w["aid"]:
            return
        self._wait = None
        res = msg.get("result")
        if self.phase == "stopping":
            self._fail(self._cancel_reason, "%s:%s" % (msg.get("evt"), res))
            return
        if res != "OK":
            self._fail("%s_%s" % (msg.get("evt"), res), msg.get("code"))
            return
        ph = self.phase
        if ph == "home0":
            self._enter("hold")
            self._request("HEAD_HOLD", press_mm=self.p["press_mm"], drv_zero=True)
        elif ph == "hold":
            self.head = dict((k, msg[k]) for k in ("head_contact_mm", "head_pos_mm", "cp_force_N") if k in msg)
            self._enter("scan")
            self._next_scan()
        elif ph == "scan":
            x, f = msg.get("drv_pos_mm"), msg.get("cp_force_N")
            if not _num(x):
                return self._fail("POSITION_UNKNOWN")
            if not _num(f):                 # 반력 샘플 부족: 0 으로 대신하지 않는다
                return self._fail("FORCE_SAMPLES_SHORT", msg.get("force_n"))
            self.pos_x = x
            self.points.append({"x": x, "F": f, "n": msg.get("force_n")})
            self._detect()
            self._next_scan()
        elif ph == "home1":
            if self.det["tip_index"] is None:
                return self._fail("TIP_NOT_FOUND")
            self._enter("move")
            self._move_n = 0
            self._move_max = int(math.ceil(abs(self.target_x - self.pos_x) / self.p["move_step_mm"])) + 2
            self._next_move()
        elif ph == "move":
            x = msg.get("drv_pos_mm")
            if not _num(x):
                return self._fail("POSITION_UNKNOWN")
            self.pos_x = x
            self._next_move()

    def _next_scan(self):
        step = self.p["scan_step_mm"]
        if self._scan_cmd + step > self.p["scan_roi_mm"] + 1e-9:
            # ROI 끝: 판정하고 헤드를 올린다. 말단이 없어도 헤드는 올린다
            self._detect()
            if self.det["tip_index"] is not None:
                self.target_x = self.det["tip_x"] - self.p["scan_dir_sign"] * self.d_mm
            self._enter("home1")
            self._request("HOME")
            return
        self._scan_cmd += step
        self._request("DRIVE_STEP", dist_mm=round(self.p["scan_dir_sign"] * step, 2))

    def _next_move(self):
        rem = self.target_x - self.pos_x
        dist = round(max(-self.p["move_step_mm"], min(self.p["move_step_mm"], rem)), 2)
        if abs(rem) <= self.p["move_tol_mm"] or dist == 0:
            self._enter("done")
            self._emit(self.on_done, True, self._snap())
            return
        self._move_n += 1
        if self._move_n > self._move_max:
            return self._fail("MOVE_NOT_CONVERGED", round(rem, 2))
        self._request("DRIVE_STEP", dist_mm=dist)

    def _detect(self):
        xs = [pt["x"] for pt in self.points]
        fs = [pt["F"] for pt in self.points]
        self.det = detect_tip(xs, fs, self.p["tip_ref_n"], self.p["tip_drop_ratio"])

    # ── 내부: 송신과 대기 ───────────────────────────────────
    def _wait_s(self, cmd):
        p = self.p
        if cmd == "DRIVE_STEP":
            ms = p["drv_timeout_ms"] + p["drv_settle_max_ms"] + p["drv_avg_ms"] + 1000
        else:                               # HEAD_HOLD(§4.4). HOME 도 같은 한도를 쓴다(초안에 없음)
            ms = p["head_timeout_ms"] + 1000
        return ms / 1000.0

    def _request(self, cmd, **kw):
        if not self.link.mot_fresh():       # mot 가 오래되면 새 동작 명령을 보내지 않는다(§3.3-2)
            self._queued = (cmd, kw, self.clock())
            return
        self._queued = None
        seq = self.link.send(cmd, **kw)
        if seq is None:
            return self._fail("NOT_SENT", cmd)
        sent = self.link.sent(seq) or {}
        self.moved = True
        self._wait = {"seq": seq, "aid": sent.get("aid"), "cmd": cmd, "at": self.clock(),
                      "limit": self._wait_s(cmd), "boot": self.link.boot_id, "acked": False,
                      "extended": False, "resent": 0, "status_at": None, "status_tn": None,
                      "status_tries": 0}
        self._emit(self.on_update, self._snap())

    def _halt(self, reason):
        self._cancel_reason = reason
        self._enter("stopping")
        self._stop_at = self.clock()
        self.link.send("DRIVE_STOP")

    def _tick(self):
        now = self.clock()
        if self._queued and self.active:
            cmd, kw, at = self._queued
            if self.link.mot_fresh():
                self._request(cmd, **kw)
            elif now - at > MOT_WAIT_S:
                self._fail("MOT_STALE", cmd)
            return
        w = self._wait
        if not w:
            return
        if self.phase == "stopping":
            if now - self._stop_at > w["limit"]:
                self._fail(self._cancel_reason, "STOP_UNCONFIRMED")
            return
        if self.link.boot_id != w["boot"]:      # 재부팅: 결과 불명, 재전송하지 않는다(§2.5)
            return self._fail("REBOOTED", w["cmd"])
        # 완료 이벤트 유실(§4.4)
        if w["status_at"] is None:
            if now - w["at"] > w["limit"]:
                self._ask_status(w, now)
            return
        pair = self.link.status_pair()
        if pair and (w["status_tn"] is None or (pair[1].get("tn") or 0) > w["status_tn"]):
            return self._judge(w, pair[1], now)
        if now - w["status_at"] > STATUS_WAIT_S:
            if w["status_tries"] >= STATUS_TRIES:
                return self._fail("NO_STATUS", w["cmd"])
            self._ask_status(w, now)

    def _ask_status(self, w, now):
        pair = self.link.status_pair()
        w["status_tn"] = pair[1].get("tn") if pair else None
        w["status_at"] = now
        w["status_tries"] += 1
        self.link.send("GET_STATUS")

    def _judge(self, w, mot, now):
        if self.link.boot_id != w["boot"] or mot.get("boot_id", w["boot"]) != w["boot"]:
            return self._fail("REBOOTED", w["cmd"])     # 결과 불명. 재전송하지 않는다
        w["status_at"] = None
        if w["aid"] is not None and mot.get("mot_last_aid") == w["aid"]:
            if w["resent"] >= 1:
                return self._fail("EVENT_LOST", w["cmd"])
            w["resent"] += 1                 # 같은 seq·aid·파라미터로 다시 보내 저장된 결과를 받는다
            w["at"] = now
            if not self.link.resend(w["seq"]):
                return self._fail("NOT_SENT", w["cmd"])
            return
        if w["aid"] is not None and mot.get("busy_aid") == w["aid"]:
            if not w["extended"]:
                w["extended"] = True         # 한도까지 한 번 더 기다린다
                w["at"] = now
                return
            return self._halt("TIMEOUT")
        return self._fail("STATE_MISMATCH", w["cmd"])

    # ── 내부: 기록과 콜백 ───────────────────────────────────
    def _enter(self, phase):
        self.phase = phase
        if self.log:
            self.log.event("search", phase.upper())
        self._emit(self.on_update, self._snap())

    def _fail(self, reason, detail=None):
        self._wait = None
        self._queued = None
        self.reason = reason
        self.detail = detail
        self.phase = "failed"
        if self.log:
            self.log.event("search", "FAIL:" + str(reason))
        self._emit(self.on_done, False, self._snap())

    def _emit(self, fn, *args):
        self._out.append((fn, args))

    def _flush(self, out):
        for fn, args in out:
            try:
                fn(*args)
            except Exception:
                pass

    def _snap(self):
        det = self.det or {}
        pts = []
        for i, pt in enumerate(self.points):
            q = {"x": pt["x"], "F": pt["F"]}
            if det.get("ref") and i < len(det["ref"]):
                q["ref"] = det["ref"][i]
                q["r"] = det["r"][i]
            pts.append(q)
        cands = det.get("candidates") or []
        s = {"phase": self.phase, "reason": self.reason, "detail": self.detail,
             "eapd_mm": self.eapd_mm, "D_mm": round(self.d_mm, 2) if self.d_mm is not None else None,
             "points": pts, "tip_x": det.get("tip_x"),
             "candidates_x": [self.points[i]["x"] for i in cands],
             "candidate_groups": det.get("groups", 0),
             "target_x": round(self.target_x, 2) if self.target_x is not None else None,
             "pos_x": self.pos_x, "head": dict(self.head), "moved": self.moved,
             "enc_mode": getattr(self.link, "drv_enc_mode", None)}   # 1 이면 단일 엔코더 + 자이로(§4.7)
        if self.p:
            s["rule"] = {"drop_ratio": self.p["tip_drop_ratio"], "ref_n": self.p["tip_ref_n"],
                         "press_mm": self.p["press_mm"], "dir": self.p["scan_dir_sign"],
                         "roi_mm": self.p["scan_roi_mm"], "step_mm": self.p["scan_step_mm"]}
        return s
