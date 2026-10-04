#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""SAVER USB 프로토콜 v1.0 + v1.1 초안(docs/protocol_v1.1_draft.md) — Jetson 측 구현

명세: protocol/teensy_usb_protocol_v1.md
Teensy 측 대응 구현: teensy/saver_proto.h

Python 3.6 (JetPack 4.6.x) 호환. f-string·dataclass·walrus 사용하지 않습니다.
"""
import json
import random

PROTO_VERSION = 1
MAX_LINE = 512
MAX_BODY = MAX_LINE - 1     # 줄 끝 LF 를 뺀 본문 한도 (v1.1 §3.4)

# ── 명령 (명세 §5) ───────────────────────────────────────────────
CMD = [
    "PING", "GET_STATUS", "ARM", "DISARM", "HOME", "SET_TARGET",
    "VACUUM_ON", "VACUUM_OFF", "STIFFNESS_SCAN", "SET_CP_POINT",
    "TRIAL_COMPRESSION", "START_CPR", "PAUSE_CPR", "RESUME_CPR",
    "STOP_CPR", "RESET_FAULT", "SET_TEL_RATE",
    # v1.1 초안 §2
    "GET_INFO", "GET_FAULTS", "DRIVE_STEP", "DRIVE_STOP", "HEAD_HOLD",
]

# v1.1 §2.5 명령 분류. 조회는 bid·aid 없이 보내고, 나머지는 bid·aid 를 싣는다.
QUERY_CMDS = ("PING", "GET_STATUS", "GET_INFO", "GET_FAULTS")
STOP_CMDS = ("DRIVE_STOP", "STOP_CPR", "PAUSE_CPR", "DISARM")
ACTION_CMDS = ("HOME", "HEAD_HOLD", "DRIVE_STEP", "SET_CP_POINT", "TRIAL_COMPRESSION", "STIFFNESS_SCAN")
AID_MAX = 4294967295

# ── Teensy 내부 상태 (명세 §7) — Jetson의 S1~S13과 별개 ──────────
MCU_STATES = [
    "IDLE", "ARMED", "HOMED", "VACUUM_HOLD", "CPR_READY",
    "CPR_RUNNING", "CPR_PAUSED", "FAULT", "ESTOP",
]

# ── 타이밍 (명세 §9) ─────────────────────────────────────────────
ACK_TIMEOUT_S = 0.2
ACK_RETRY = 2
HEARTBEAT_S = 1.0
STALE_S = 0.5       # 텔레메트리가 이보다 오래되면 표시값으로 쓰지 않는다
TEL_HZ_DEFAULT = 20

# ── 오류 코드 (명세 §8) ──────────────────────────────────────────
ERR = {
    "1-CM-003": "512 byte 초과 / 파싱 실패",
    "2-CM-002": "ACK 미수신",
    "3-CM-001": "프로토콜 버전 불일치",
    "3-CM-010": "통신 두절",
    "2-CU-011": "ARM 되지 않은 상태에서 구동 명령",
    "2-CU-012": "목표 깊이 도달 실패",
    "3-CU-021": "모터 과전류",
    "3-CU-022": "리미트 스위치 이탈",
    "3-CU-023": "엔코더 신호 상실",
    "2-VU-004": "음압 목표 미달",
    "2-VU-005": "음압 누설 감지",
    "3-VU-006": "흡착 상실",
    "1-PU-001": "PPG 신호 품질 저하",
    "2-PU-002": "PPG 센서 미착용/탈락",
    "2-VU-007": "음압 모터 드라이버 ERR 신호",
    "1-MU-010": "구동부 온도 상승 (팬 가동)",
    "2-MU-011": "구동부 온도 경고",
    "3-MU-001": "전원 전압 하한 미달",
    "3-MU-002": "비상정지 작동",
    # v1.1 초안 §6 (운반 유닛 TU, 음압 해제)
    "2-TU-001": "주행 방향 이탈",
    "2-TU-002": "기울기 초과",
    "3-TU-003": "탐색 반력 상한 초과",
    "2-TU-004": "주행 거리 미달·시간 초과",
    "3-TU-005": "주행 엔코더 무효",
    "2-TU-006": "접촉 실패",
    "2-TU-007": "자이로 수신 끊김",
    "2-TU-008": "설정값 미설정으로 명령 거부",
    "3-TU-009": "로드셀 데이터 상실·정체",
    "3-TU-010": "리니어 센서 상실·범위 이탈",
    "2-VU-008": "음압 해제 시간 초과",
}


def resolve_enter_cmd(cmd, mcu_state):
    """상태 진입 명령을 장치 상태에 맞게 고른다(state_machine_v2.json v2.2).

    S7 진입의 START_CPR: 장치가 CPR_PAUSED 면 RESUME_CPR(일시정지·AED·ROSC 확인 뒤 재개),
    CPR_RUNNING 이면 보내지 않는다(None, 인공호흡 안내처럼 멈추지 않은 경우).
    정지 방향(PAUSE_CPR 등)은 장치 상태가 오래됐을 수 있으므로 항상 그대로 보낸다."""
    if cmd == "START_CPR":
        if mcu_state == "CPR_PAUSED":
            return "RESUME_CPR"
        if mcu_state == "CPR_RUNNING":
            return None
    return cmd


def severity(code):
    """'3-CU-021' → 3. 알 수 없으면 1(주의)로 처리한다."""
    try:
        return int(str(code).split("-")[0])
    except (ValueError, IndexError):
        return 1


ALARM_BY_SEV = {1: "caution", 2: "warning", 3: "danger"}


class ProtocolError(Exception):
    pass


def encode(msg):
    """dict → 한 줄 JSON(bytes). 명세 §3"""
    msg.setdefault("v", PROTO_VERSION)
    line = json.dumps(msg, ensure_ascii=True, separators=(",", ":"))
    if len(line) > MAX_BODY:
        raise ProtocolError("line too long: %d byte" % (len(line) + 1))
    return (line + "\n").encode("ascii")


def decode(line):
    """한 줄 → dict. 실패하면 None을 반환하고 호출부는 그 줄만 버린다.

    명세 §3: 파싱 실패로 프로그램이 죽으면 안 된다.
    """
    if isinstance(line, bytes):
        try:
            line = line.decode("utf-8", "replace")
        except Exception:
            return None
    line = line.strip()
    if not line or len(line) > MAX_BODY:
        return None
    try:
        msg = json.loads(line)
    except ValueError:
        return None
    if not isinstance(msg, dict) or "t" not in msg:
        return None
    return msg


def build_cmd(seq, cmd, **kw):
    msg = {"v": PROTO_VERSION, "t": "cmd", "seq": seq, "cmd": cmd}
    msg.update(kw)
    return msg


class SeqCounter(object):
    """1~65535 순환 (명세 §4.1)"""

    def __init__(self, start=1):
        self._n = start

    def next(self):
        n = self._n
        self._n = 1 if self._n >= 65535 else self._n + 1
        return n


class AidCounter(object):
    """v1.1 aid: 부팅 세션 안에서 1부터 늘리고 순환하지 않는다(§2.5). 한도에 이르면 None."""

    def __init__(self, start=1):
        self._n = max(1, int(start))

    def next(self):
        if self._n > AID_MAX:
            return None
        n = self._n
        self._n += 1
        return n


def initial_seq(rng=None):
    """세션 시작 seq (1~65535 중 무작위).

    Teensy 는 최근 8개 seq 를 기억하고 같은 seq 는 실행하지 않는다(명세 §4.1).
    Teensy 가 별도 전원으로 켜져 있는 채 Jetson 만 재시작하면, 1부터 다시 세는
    seq 가 직전 세션과 겹쳐 새 명령이 ACK 만 받고 실행되지 않는다.
    시작값을 무작위로 두어 겹칠 확률을 명령당 8/65535 이하로 낮춘다.
    """
    return (rng or random.SystemRandom()).randint(1, 65535)
