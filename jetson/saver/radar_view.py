# -*- coding: utf-8 -*-
"""radar_view.py — 레이더 시험 화면: 호흡수, 심박수, 거리(cm)를 크게 보이는 웹 화면

브리지, Teensy 와 무관하게 레이더 포트만 읽는다(장치를 움직이지 않는다). 브리지가 같은 포트를 읽고 있으면
바이트를 나눠 가지므로 먼저 saver-stop 한다.

  saver-radar --ui                      # = python3 -m saver.radar /dev/saver-radar --ui
  saver-radar --ui --open               # 젯슨 화면에 Chromium 창까지 띄움
  saver-radar --ui --host 0.0.0.0       # 같은 망의 노트북, 휴대폰에서도 보기(기본은 젯슨 안에서만)
  saver-radar --ui --floor 520          # 레이더 높이(mm, radar_floor_mm 과 같은 뜻, 자로 잰 값)를 주면 EAPD 도 보인다
  → http://127.0.0.1:8090

  /              화면(radar_view.html): 큰 숫자, 최근 3분 추이, 표(최근 30초), CSV 받기
                 거리 칸: 지금 거리, EAPD 가 쓰는 최근 EAPD_WINDOW_S(5초) 중앙값, EAPD(= 높이 − 중앙값, --floor 를 줬을 때)
  /radar.json    지금 값과 최근 기록(1초 간격, 최근 HISTORY_S 초)
  /radar.csv     최근 기록 CSV. 시각은 UTC ISO 8601

값은 표시와 시험용이다. 판단(회복 판정 등)에 쓰지 않는다. 정상 범위 같은 기준을 화면에 두지 않는다.
Python 3.6 호환(표준 라이브러리만).
"""
import collections
import json
import os
import socket
import subprocess
import sys
import threading
import time
from datetime import datetime, timezone

try:
    from http.server import BaseHTTPRequestHandler, HTTPServer
    from socketserver import ThreadingMixIn
except ImportError:                     # pragma: no cover  (Python 2 는 지원하지 않는다)
    raise

from .radar import BAUD, EAPD_WINDOW_S, RadarLink

HISTORY_S = 600         # 화면과 CSV 에 남기는 최근 기록(초). 화면 추이는 최근 3분
SAMPLE_S = 1.0          # 기록 간격(초)
HTML_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "radar_view.html")


def _cm(mm):
    return None if mm is None else round(mm / 10.0, 2)


class History(object):
    """1초마다 남기는 [UTC epoch 초, 호흡수, 심박수, 사람 감지, 거리 cm, EAPD cm]. 값이 없으면 None."""

    def __init__(self, seconds=HISTORY_S):
        self._rows = collections.deque(maxlen=max(1, int(seconds / SAMPLE_S)))
        self._lock = threading.Lock()

    def add(self, t, snap):
        row = [round(t, 3), snap.get("rr_bpm"), snap.get("hr_bpm"), snap.get("presence"),
               _cm(snap.get("distance_mm")), _cm(snap.get("eapd_mm"))]
        with self._lock:
            self._rows.append(row)

    def rows(self):
        with self._lock:
            return [list(r) for r in self._rows]


def state(link, hist, floor_mm=None, now=None):
    """화면이 읽는 JSON. 값은 radar.py 의 신선도 규칙을 그대로 따른다(오래되거나 측정 못 한 값은 None).
    거리와 EAPD 는 cm 로 바꿔 보낸다(radar.py, 설정, 기록은 mm 그대로)."""
    s = link.snapshot(floor_mm)
    return {
        "port": link.port,
        "connected": bool(s.get("connected")),
        "receiving": bool(s.get("receiving")),
        "presence": s.get("presence"),
        "rr_bpm": s.get("rr_bpm"),
        "hr_bpm": s.get("hr_bpm"),
        "distance_cm": _cm(s.get("distance_mm")),
        "distance_med_cm": _cm(s.get("distance_med_mm")),
        "eapd_cm": _cm(s.get("eapd_mm")),
        "floor_cm": _cm(floor_mm) if floor_mm and floor_mm > 0 else None,
        "eapd_window_s": EAPD_WINDOW_S,
        "frames": s.get("frames", 0),
        "lost": s.get("lost", 0),
        "bad": s.get("bad", 0),
        "error": s.get("error"),
        "now": round(time.time() if now is None else now, 3),
        "history": hist.rows(),
    }


def to_csv(rows):
    out = ["time_utc,rr_bpm,hr_bpm,distance_cm,eapd_cm,presence"]
    blank = lambda v: "" if v is None else v
    for t, rr, hr, pr, dist, eapd in rows:
        out.append("%s,%s,%s,%s,%s,%s" % (
            datetime.fromtimestamp(t, timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
            blank(rr), blank(hr), blank(dist), blank(eapd), "" if pr is None else (1 if pr else 0)))
    return ("\n".join(out) + "\n").encode("utf-8")


class _Server(ThreadingMixIn, HTTPServer):
    daemon_threads = True
    allow_reuse_address = True


class _Handler(BaseHTTPRequestHandler):
    server_version = "SAVER-radar-view"

    def log_message(self, fmt, *args):     # 요청마다 줄을 찍지 않는다
        pass

    def _send(self, code, body, ctype, extra=None):
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        for k, v in (extra or {}).items():
            self.send_header(k, v)
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        path = self.path.split("?", 1)[0]
        srv = self.server
        try:
            if path in ("/", "/index.html"):
                self._send(200, srv.html, "text/html; charset=utf-8")
            elif path == "/radar.json":
                body = json.dumps(state(srv.link, srv.hist, srv.floor_mm), ensure_ascii=False).encode("utf-8")
                self._send(200, body, "application/json; charset=utf-8")
            elif path == "/radar.csv":
                name = "radar_%s.csv" % datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
                self._send(200, to_csv(srv.hist.rows()), "text/csv; charset=utf-8",
                           {"Content-Disposition": "attachment; filename=%s" % name})
            else:
                self._send(404, b"not found", "text/plain; charset=utf-8")
        except (BrokenPipeError, ConnectionResetError):   # 화면을 닫은 경우
            pass


def make_server(link, hist, host="127.0.0.1", http_port=8090, floor_mm=None):
    srv = _Server((host, http_port), _Handler)
    with open(HTML_PATH, "rb") as f:
        srv.html = f.read()
    srv.link = link
    srv.hist = hist
    srv.floor_mm = floor_mm
    return srv


def _display_env():
    """젯슨 화면(DISPLAY=:0)에 창을 띄울 환경. SSH 로 들어오면 XAUTHORITY 가 없어 거부되므로 데스크톱 세션의 것을 찾는다."""
    env = dict(os.environ)
    env.setdefault("DISPLAY", ":0")
    if "XAUTHORITY" not in env:
        for p in ("/run/user/%d/gdm/Xauthority" % os.getuid(), os.path.expanduser("~/.Xauthority")):
            if os.path.exists(p):
                env["XAUTHORITY"] = p
                break
    return env


def _open_browser(url):
    """젯슨 화면에 Chromium 앱 창을 띄운다(키오스크와 다른 프로필이라 서로 막지 않는다).
    (띄운 프로세스 또는 None, 열었는지). 리눅스가 아니면(노트북) 기본 브라우저로 연다."""
    if not sys.platform.startswith("linux"):
        import webbrowser
        return None, bool(webbrowser.open(url))
    prof = os.path.expanduser("~/.config/saver-radar-view")
    flags = ["--app=" + url, "--user-data-dir=" + prof, "--start-maximized", "--no-first-run",
             "--password-store=basic", "--noerrdialogs", "--disable-translate", "--disable-infobars"]
    for exe in ("chromium-browser", "chromium"):
        try:
            return subprocess.Popen([exe] + flags, env=_display_env(),
                                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL), True
        except OSError:
            continue
    return None, False


def serve(port, baud=BAUD, host="127.0.0.1", http_port=8090, open_browser=False, seconds=0, floor_mm=None):
    """레이더를 읽으며 화면을 띄운다. seconds 가 0 이면 Ctrl+C 까지. floor_mm(레이더 높이)을 주면 EAPD 도 보인다."""
    link = RadarLink(port, baud).start()
    hist = History()
    try:
        srv = make_server(link, hist, host, http_port, floor_mm)
    except (OSError, socket.error) as e:
        link.stop()
        print("화면 서버를 열지 못했습니다(%s:%d): %s. 다른 번호는 --http-port" % (host, http_port, e))
        return 1
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    url = "http://%s:%d" % ("127.0.0.1" if host in ("0.0.0.0", "") else host, srv.server_address[1])
    print("레이더 시험 화면: %s  (레이더 %s, Ctrl+C 로 끝)" % (url, port))
    if host in ("0.0.0.0", ""):
        print("  같은 망의 다른 기기: http://<젯슨 IP>:%d  (saver-status 의 주소)" % srv.server_address[1])
    win = None
    if open_browser:
        win, opened = _open_browser(url)
        print("  젯슨 화면에 창을 띄웠습니다(끝내면 함께 닫힘)" if win is not None else
              ("  브라우저로 열었습니다" if opened else "  Chromium 을 찾지 못했습니다. 주소를 브라우저에 직접 넣으세요"))
    sys.stdout.flush()
    t0 = time.monotonic()
    next_t, n = t0, 0
    try:
        while not seconds or time.monotonic() - t0 < seconds:
            next_t += SAMPLE_S
            time.sleep(max(0.0, next_t - time.monotonic()))
            snap = link.snapshot(floor_mm)
            hist.add(time.time(), snap)
            n += 1
            if n % 5 == 0:                  # 터미널에는 5초마다 한 줄
                dist, eapd = _cm(snap.get("distance_mm")), _cm(snap.get("eapd_mm"))
                print("%s 호흡 %s 심박 %s 거리 %s cm EAPD %s cm 사람 %s 프레임 %d 빠짐 %d" % (
                    time.strftime("%H:%M:%S"), snap.get("rr_bpm", "—"), snap.get("hr_bpm", "—"),
                    "—" if dist is None else dist, "—" if eapd is None else eapd,
                    {True: "있음", False: "없음"}.get(snap.get("presence"), "—"),
                    snap.get("frames", 0), snap.get("lost", 0)))
                sys.stdout.flush()
    except KeyboardInterrupt:
        pass
    finally:
        srv.shutdown()
        srv.server_close()
        link.stop()
        if win is not None and win.poll() is None:
            win.terminate()
    return 0
