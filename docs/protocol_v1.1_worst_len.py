# -*- coding: utf-8 -*-
"""프로토콜 v1.1 4차 초안 §3.4: 최악값 줄 길이 계산 (검토용).

형식 규칙의 최댓값(실수 -9999.99, 정수 최댓값, 최장 문자열, faults 6개)으로
메시지마다 한 줄을 만들어 본문 길이(LF 제외)를 잰다. 한도는 본문 511 byte.
하드웨어 없이 실행된다: python3 docs/protocol_v1.1_worst_len.py
"""
from __future__ import print_function
import json
from collections import OrderedDict as OD

F = -9999.99           # 실수: 소수 2자리, 범위 밖이면 생략
U32 = 4294967295       # ms, tn, aid
U16 = 65535            # seq, boot_id, force_n, n
STATE = 'VACUUM_HOLD'  # 최장 상태명
FAULTS = ['3-TU-010'] * 6
LIMIT = 511


def fmt(v):
    if isinstance(v, bool):
        return 'true' if v else 'false'
    if isinstance(v, float):
        return '%.2f' % v
    if isinstance(v, int):
        return str(v)
    if isinstance(v, list):
        return '[' + ','.join('"%s"' % x for x in v) + ']'
    return '"%s"' % v


def line(d):
    return '{' + ','.join('"%s":%s' % (k, fmt(v)) for k, v in d.items()) + '}'


def tel10():
    d = OD([('v', 1), ('t', 'tel'), ('ms', U32), ('state', STATE)])
    for k in ['cp_rate_cpm', 'cp_depth_mm', 'cp_force_N', 'cp_motor_i_A', 'vac_kPa',
              'ppg_hr_bpm', 'spo2_pct', 'ppg_sqi', 'temp_C', 'fan_pct']:
        d[k] = F
    for k in ['estop', 'limit_top', 'limit_bottom', 'vac_leak']:
        d[k] = False
    d['faults'] = FAULTS
    return d


def tel11():
    d = tel10()
    d['tn'] = U32
    d['boot_id'] = U16
    d['faults_more'] = True
    return d


def mot():
    return OD([('v', 1), ('t', 'mot'), ('ms', U32), ('tn', U32), ('boot_id', U16),
               ('busy', 'HEAD_SEARCH'), ('busy_seq', U16), ('busy_aid', U32),
               ('head_hold_active', False), ('drv_settled', False),
               ('drv_pos_mm', F), ('drv_yaw_deg', F), ('drv_pitch_deg', F), ('drv_roll_deg', F),
               ('head_pos_mm', F), ('head_contact_mm', F),
               ('mot_last_seq', U16), ('mot_last_aid', U32),
               ('mot_last_cmd', 'SET_CP_POINT'), ('mot_last_result', 'SETTLE_FAIL')])


def evt(name, extra):
    d = OD([('v', 1), ('t', 'evt'), ('ms', U32), ('evt', name)])
    d.update(extra)
    return d


MESSAGES = [
    ('v1.0 tel (faults 6)', tel10()),
    ('v1.1 tel', tel11()),
    ('v1.1 mot', mot()),
    ('DRIVE_DONE', evt('DRIVE_DONE', OD([('seq', U16), ('aid', U32), ('result', 'SETTLE_FAIL'),
                                         ('code', '3-TU-010'), ('drive_mm', F), ('drv_pos_mm', F),
                                         ('drv_yaw_deg', F), ('cp_force_N', F), ('force_n', U16)]))),
    ('HOMING_DONE (HEAD_HELD, CP_ZERO_SET)', evt('HOMING_DONE', OD([('seq', U16), ('aid', U32),
                                         ('result', 'NO_CONTACT'), ('code', '3-TU-010'),
                                         ('head_contact_mm', F), ('head_pos_mm', F), ('cp_force_N', F)]))),
    ('INFO', evt('INFO', OD([('seq', U16), ('fw', 'X' * 16), ('proto', '1.1'),
                             ('caps', ['DRIVE', 'HEAD_HOLD', 'CP_ZERO', 'MOT', 'AID', 'FAULTS']),
                             ('boot_id', U16), ('last_seq', U16), ('last_aid', U32)]))),
    ('ack', OD([('v', 1), ('t', 'ack'), ('seq', U16), ('ok', False), ('state', STATE),
                ('err', '3-TU-010'), ('why', 'HOME_UNAVAILABLE')])),
    ('err (why 24)', OD([('v', 1), ('t', 'err'), ('ms', U32), ('code', '3-TU-010'), ('sev', 3),
                         ('latched', True), ('why', 'X' * 24)])),
    ('FAULTS_END', evt('FAULTS_END', OD([('seq', U16), ('n', U16)]))),
]


def main():
    ok = True
    for name, d in MESSAGES:
        s = line(d)
        json.loads(s)
        over = len(s) > LIMIT
        ok = ok and not over
        print('%-38s %3d byte  margin %4d  %s' % (name, len(s), LIMIT - len(s), 'OVER' if over else 'OK'))
    return 0 if ok else 1


if __name__ == '__main__':
    raise SystemExit(main())
