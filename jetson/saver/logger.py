#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""logger.py — W7 사양 CSV 로그

한 줄 = 한 타임스탬프. 이벤트·명령·오류도 각각 한 줄로 남깁니다.
로그 실패가 제어를 막으면 안 되므로 모든 쓰기는 예외를 삼킵니다.
"""
import csv
import os
from datetime import datetime

HEADER = [
    "ts_ms", "source", "mode", "state", "substate",
    "cp_rate_cpm", "cp_depth_mm", "cp_force_N", "cp_motor_cmd", "cp_motor_i_A",
    "vac_kPa", "vac_cmd", "vac_leak_flag",
    "radar_rr_bpm", "radar_hr_bpm", "radar_sqi",
    "ppg_hr_bpm", "spo2_pct", "ppg_sqi",
    "pupil_L_mm", "pupil_R_mm", "plr_score", "face_ci", "vision_conf",
    "rosc_prob", "decision", "recommendation", "conf",
    "err_code", "err_severity", "err_latched",
    "temp_C", "fan_pct",
]

_TEL_MAP = {
    "cp_rate_cpm": "cp_rate_cpm", "cp_depth_mm": "cp_depth_mm",
    "cp_force_N": "cp_force_N", "cp_motor_i_A": "cp_motor_i_A",
    "vac_kPa": "vac_kPa", "vac_leak": "vac_leak_flag",
    "ppg_hr_bpm": "ppg_hr_bpm", "spo2_pct": "spo2_pct", "ppg_sqi": "ppg_sqi",
    "temp_C": "temp_C", "fan_pct": "fan_pct",
}


class CsvLogger(object):

    def __init__(self, outdir="logs", prefix="saver"):
        self.path = None
        self.state = ""
        try:
            os.makedirs(outdir)
        except OSError:
            pass
        name = datetime.now().strftime(prefix + "_%Y%m%d_%H%M%S.csv")
        self.path = os.path.join(outdir, name)
        self.fp = open(self.path, "w", newline="")
        self.w = csv.DictWriter(self.fp, fieldnames=HEADER, extrasaction="ignore")
        self.w.writeheader()
        self.fp.flush()

    def _row(self, **kw):
        import time
        row = dict.fromkeys(HEADER, "")
        row["ts_ms"] = int(time.time() * 1000)
        row["state"] = kw.pop("state", self.state)
        row.update(kw)
        try:
            self.w.writerow(row)
            self.fp.flush()
        except Exception:
            pass                       # 로그 실패가 제어를 막으면 안 된다

    def telemetry(self, msg):
        row = {}
        for k, col in _TEL_MAP.items():
            if k in msg:
                v = msg[k]
                row[col] = int(v) if isinstance(v, bool) else v
        self._row(source="mcu", substate=msg.get("state", ""), **row)

    def event(self, source, name, **kw):
        self._row(source=source, decision=name)

    def transition(self, frm, event, to, state):
        self.state = state or to
        self._row(source="fsm", decision="%s:%s->%s" % (event, frm, to))

    def error(self, msg):
        self._row(source="mcu", err_code=msg.get("code", ""),
                  err_severity=msg.get("sev", ""),
                  err_latched=1 if msg.get("latched") else 0,
                  decision=msg.get("why", ""))

    def close(self):
        try:
            self.fp.close()
        except Exception:
            pass
