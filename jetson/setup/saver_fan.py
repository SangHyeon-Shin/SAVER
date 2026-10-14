#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""saver_fan.py — 젯슨 나노 방열판 냉각 팬 자동 제어 (root 서비스 saver-fan.service)

jugfk/jetson-fan-ctl(Pyrestone/jetson-fan-ctl 갈래, MIT)의 방식을 따른다. 온도에 비례해 PWM 0~255:
    PWM = 255 × (T − fan_off_temp_C) / (fan_max_temp_C − fan_off_temp_C), 0~255 로 자름
    기본 20 °C 이하 정지, 50 °C 이상 최대, 2 s 주기(참고 구현 기본값). /sys/devices/pwm-fan/target_pwm 에 쓴다

참고 구현과 다른 점
  - 판단 온도는 CPU, GPU 온도 영역의 최댓값이다(참고 구현은 thermal_zone0 = AO-therm 하나).
    PMIC-Die 영역은 실측이 아닌 고정값(100 °C)으로 알려져 있어 쓰지 않는다. CPU, GPU 영역이 없으면 thermal_zone0
  - 온도를 못 읽거나 설정이 잘못되면 255(최대)
  - 커널 자동 제어(temp_control)가 있으면 도는 동안 끄고, 끝날 때 다시 켠다(공장 기본으로 돌아감).
    temp_control 이 없는 장치면 끝날 때 255. 참고 구현처럼 끝날 때 0(정지)으로 두지 않는다
  - jetson_clocks(클럭 최대 고정)를 부르지 않는다. 배터리 전원이고 발열만 늘린다
  - 설정 /etc/saver/fan.json 을 주기마다 다시 읽는다(서비스 재시작 없이 반영). 잘못된 항목은 기본값
이 값들은 젯슨 발열용 공학 설정이며 의학적 기준이 아니다. 장치 냉각팬 1, 2(Teensy 핀 5, 2, TEMP_FAN_ON_C)와는 별개다.

  sudo python3 saver_fan.py --config /etc/saver/fan.json   # 서비스(계속)
  python3 saver_fan.py --show                                # 온도, PWM, 설정 보기(쓰지 않음, root 불필요)
  sudo python3 saver_fan.py --test 5                         # 5초 동안 최대로 돌려 팬과 배선 확인(서비스를 멈춘 뒤)
  sudo python3 saver_fan.py --release                        # 커널 자동 제어로 되돌림, 없으면 255(서비스 ExecStopPost)
"""
import argparse
import glob
import json
import math
import os
import signal
import sys
import time

DEFAULTS = {"fan_off_temp_C": 20.0, "fan_max_temp_C": 50.0, "interval_s": 2.0}
INTERVAL_RANGE = (0.5, 30.0)
FAN_DIR = "sys/devices/pwm-fan"
ZONES_GLOB = "sys/devices/virtual/thermal/thermal_zone*"
LOG_STEP = 25                    # PWM 이 이만큼 바뀌면 기록
LOG_EVERY_S = 300.0              # 바뀌지 않아도 이 간격으로 기록


def _say(msg):
    """로그 한 줄. 로캘과 무관하게 UTF-8 로 쓰고, 출력이 실패해도 제어를 멈추지 않는다."""
    try:
        sys.stdout.buffer.write((msg + "\n").encode("utf-8"))
        sys.stdout.flush()
    except (OSError, ValueError, AttributeError):
        pass


def _num(v):
    return isinstance(v, (int, float)) and not isinstance(v, bool) and math.isfinite(v)


def fan_curve(temp_c, off_c, max_c):
    """온도 → PWM(0~255). 온도를 모르거나 설정이 잘못되면 255(최대)."""
    if not (_num(temp_c) and _num(off_c) and _num(max_c)) or max_c <= off_c:
        return 255
    spd = 255.0 * (temp_c - off_c) / (max_c - off_c)
    return int(min(max(0.0, spd), 255.0))


def read_zones(root):
    """[(영역 이름, 종류, °C)]. 읽지 못하거나 범위(-40~150 °C) 밖인 값은 뺀다."""
    out = []
    for z in sorted(glob.glob(os.path.join(root, ZONES_GLOB))):
        try:
            with open(os.path.join(z, "type")) as f:
                kind = f.read().strip()
            with open(os.path.join(z, "temp")) as f:
                t = int(f.read().strip()) / 1000.0
        except (OSError, ValueError):
            continue
        if -40.0 <= t <= 150.0:
            out.append((os.path.basename(z), kind, t))
    return out


def pick_temp(zones):
    """판단 온도: CPU, GPU 영역의 최댓값. 없으면 thermal_zone0. 그것도 없으면 None."""
    cg = [t for _, kind, t in zones if "cpu" in kind.lower() or "gpu" in kind.lower()]
    if cg:
        return max(cg)
    z0 = [t for name, _, t in zones if name == "thermal_zone0"]
    return z0[0] if z0 else None


def load_config(path):
    """(설정, 무시한 항목 목록). 파일이 없으면 기본값, 잘못된 항목은 그 항목만 기본값."""
    cfg, bad = dict(DEFAULTS), []
    if path and os.path.exists(path):
        try:
            with open(path, encoding="utf-8") as f:
                data = json.load(f)
        except (OSError, ValueError):
            data = None
        if not isinstance(data, dict):
            return cfg, ["파일 형식"]
        for k in DEFAULTS:
            if k in data:
                if _num(data[k]):
                    cfg[k] = float(data[k])
                else:
                    bad.append(k)
    if cfg["fan_max_temp_C"] <= cfg["fan_off_temp_C"]:
        bad.append("fan_max_temp_C <= fan_off_temp_C")
        cfg["fan_off_temp_C"], cfg["fan_max_temp_C"] = DEFAULTS["fan_off_temp_C"], DEFAULTS["fan_max_temp_C"]
    if not INTERVAL_RANGE[0] <= cfg["interval_s"] <= INTERVAL_RANGE[1]:
        bad.append("interval_s")
        cfg["interval_s"] = DEFAULTS["interval_s"]
    return cfg, bad


def _read(path):
    try:
        with open(path) as f:
            return f.read().strip()
    except OSError:
        return None


def _write(path, value):
    with open(path, "w") as f:
        f.write(str(value))


class FanControl(object):
    """sysfs 팬 하나. root 는 시험용 가짜 sysfs 뿌리."""

    def __init__(self, root="/", config=None):
        self.root = root
        self.config = config
        d = os.path.join(root, FAN_DIR)
        self.pwm_path = os.path.join(d, "target_pwm")
        self.cur_path = os.path.join(d, "cur_pwm")
        self.rpm_path = os.path.join(d, "rpm_measured")
        self.tc_path = os.path.join(d, "temp_control")

    def present(self):
        return os.path.exists(self.pwm_path)

    def take(self):
        """커널 자동 제어가 있으면 끈다(이 프로그램이 쓴 값이 덮이지 않게). 실패해도 주기마다 다시 쓰므로 계속."""
        if os.path.exists(self.tc_path):
            try:
                _write(self.tc_path, 0)
            except OSError:
                pass

    def release(self):
        """끝: 커널 자동 제어를 켜 공장 기본으로 되돌리고, 그 기능이 없는 장치면 최대(255)로 둔다."""
        try:
            if os.path.exists(self.tc_path):
                _write(self.tc_path, 1)
            else:
                _write(self.pwm_path, 255)
        except OSError:
            pass

    def step(self, write=True):
        """한 주기: 설정과 온도를 읽어 PWM 을 계산하고 쓴다. (판단 온도, PWM, 영역, 설정, 무시한 설정)"""
        cfg, bad = load_config(self.config)
        zones = read_zones(self.root)
        t = pick_temp(zones)
        pwm = fan_curve(t, cfg["fan_off_temp_C"], cfg["fan_max_temp_C"])
        if write:
            _write(self.pwm_path, pwm)
        return t, pwm, zones, cfg, bad

    def show(self):
        """상태 세 줄(쓰지 않음)."""
        t, pwm, zones, cfg, bad = self.step(write=False)
        cg = ["%s %.1f" % (kind.replace("-therm", ""), tz) for _, kind, tz in zones
              if "cpu" in kind.lower() or "gpu" in kind.lower()]
        lines = ["  온도   %s °C (판단 %s)" % (", ".join(cg) or "CPU, GPU 영역 없음",
                                               "%.1f °C" % t if t is not None else "모름 → 최대")]
        if self.present():
            now = [_read(self.pwm_path)]
            cur, rpm, tc = _read(self.cur_path), _read(self.rpm_path), _read(self.tc_path)
            if cur is not None:
                now.append("실제 %s" % cur)
            if rpm is not None:
                now.append("%s rpm" % rpm)
            if tc is not None:
                now.append("커널 자동 제어 %s" % ("꺼짐(saver-fan 이 제어)" if tc == "0" else "켜짐"))
            lines.append("  PWM    곡선 %d / 255, 지금 %s" % (pwm, ", ".join(now)))
        else:
            lines.append("  PWM    팬 PWM 파일 없음 (%s)" % self.pwm_path)
        src = self.config if self.config and os.path.exists(self.config) else "파일 없음, 기본값"
        lines.append("  설정   %g °C 이하 정지, %g °C 이상 최대, %g s 주기 (%s)%s" % (
            cfg["fan_off_temp_C"], cfg["fan_max_temp_C"], cfg["interval_s"], src,
            ("  잘못된 항목은 기본값: " + ", ".join(bad)) if bad else ""))
        return lines


def _run(fc):
    """서비스 본체: 주기마다 쓰고, 바뀔 때와 5분마다 기록한다."""
    fc.take()
    logged_at, last_pwm, last_bad = 0.0, None, None
    while True:
        try:
            t, pwm, _, cfg, bad = fc.step()
        except OSError as e:
            _say("팬 PWM 쓰기 실패: %s (root 권한 확인)" % e)
            return 1
        now = time.monotonic()
        if last_pwm is None or abs(pwm - last_pwm) >= LOG_STEP or (pwm != last_pwm and pwm in (0, 255)) \
                or bad != last_bad or now - logged_at >= LOG_EVERY_S:
            _say("온도 %s → PWM %d%s" % ("%.1f °C" % t if t is not None else "모름(최대)", pwm,
                                          ("  잘못된 설정 항목은 기본값: " + ", ".join(bad)) if bad else ""))
            logged_at, last_pwm, last_bad = now, pwm, bad
        time.sleep(cfg["interval_s"])


def _test(fc, seconds):
    """최대(255)로 seconds 초 돌리며 1초마다 실제 PWM, 회전수를 보인다."""
    fc.take()
    try:
        _write(fc.pwm_path, 255)
    except OSError as e:
        _say("팬 PWM 쓰기 실패: %s (sudo 로 실행)" % e)
        return 1
    _say("최대(255)로 %d초. 팬이 돌고 바람이 나와야 한다" % seconds)
    for i in range(int(seconds)):
        time.sleep(1.0)
        cur, rpm = _read(fc.cur_path), _read(fc.rpm_path)
        _say("  %d s  실제 PWM %s%s" % (i + 1, cur if cur is not None else "?", (", %s rpm" % rpm) if rpm is not None else ""))
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description="젯슨 방열판 냉각 팬 자동 제어 (온도 → PWM)")
    ap.add_argument("--config", default="/etc/saver/fan.json", help="설정 파일 (기본 /etc/saver/fan.json)")
    ap.add_argument("--show", action="store_true", help="온도, PWM, 설정 보기(쓰지 않음)")
    ap.add_argument("--test", type=int, metavar="SEC", help="SEC 초 동안 최대로 돌린 뒤 되돌림")
    ap.add_argument("--release", action="store_true", help="커널 자동 제어로 되돌림(없으면 255)")
    ap.add_argument("--wait-s", type=float, default=20.0, help="팬 PWM 파일이 생기기를 기다리는 최대 초(부팅 직후)")
    ap.add_argument("--root", default="/", help="시험용 가짜 sysfs 뿌리")
    a = ap.parse_args(argv)

    fc = FanControl(a.root, a.config)
    if a.show:
        for line in fc.show():
            _say(line)
        return 0
    if a.release:
        fc.release()
        return 0
    end = time.monotonic() + max(0.0, a.wait_s)
    while not fc.present() and time.monotonic() < end:
        time.sleep(1.0)                  # 부팅 직후 드라이버가 늦게 뜨는 경우
    if not fc.present():
        # 팬 PWM 이 없는 장치(젯슨 나노가 아님 등). 0 으로 끝내 서비스가 재시작을 되풀이하지 않게 한다(Restart=on-failure)
        _say("팬 PWM 파일이 없어 끝냄: %s" % fc.pwm_path)
        return 0

    signal.signal(signal.SIGTERM, lambda *_: sys.exit(0))
    try:
        if a.test is not None:
            return _test(fc, max(1, min(a.test, 60)))
        return _run(fc)
    except KeyboardInterrupt:
        return 0
    finally:
        fc.release()


if __name__ == "__main__":
    sys.exit(main())
