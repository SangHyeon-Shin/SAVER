# 프로토콜 v1.1 초안: 주행 탐색 명령 (검토용)

| 항목 | 내용 |
|---|---|
| 상태 | **초안, 합의 전.** `protocol/`은 공동 영역이라 이 문서에서 먼저 합의한다 |
| 작성 | 신상현 (2026-10-01), 한건민 10/1 회신 반영 |
| 검토 | 한건민 |
| 범위 | 탐색 후 고정 순서(제출 보고서 6.2~6.3)에 필요한 명령, 이벤트, 텔레메트리, 오류 코드 |
| 반영 | 합의 후 `protocol/teensy_usb_protocol_v1.md`를 v1.1로 고치고 `jetson/tests/test_all.py`, `teensy/test_saver_proto.cpp`를 함께 고친다 |
| 와이어 버전 | `v:1` 유지. 명령·필드 추가만 하므로 프레이밍은 그대로다. v1.0 펌웨어는 새 명령을 `1-CM-005 UNKNOWN_COMMAND`로 거부한다 |

> 연구용 시제품의 마네킹·모사체 벤치 시험 범위다. 아래 설정값은 비어 있으면 해당 명령을 거부한다.
> 값을 임의로 채우지 않는다.

---

## 0. 10/1 합의된 것

1. 새 명령은 `DRIVE_STEP(dist_mm)`, `DRIVE_STOP`, `HEAD_HOLD(press_mm)` 3개다.
2. 말단 판정, 압박점 위치 계산, EAPD 계산은 **Jetson**이 한다. Teensy는 요청을 실행하고 인터록으로 거부·정지한다.
3. 압박점 이동 거리는 제출 보고서 6.2 식 그대로 Jetson이 계산한다.
   `D = 89.7 mm × EAPD / 205.8 mm`, 상한 95 mm. (10/1 합의안의 "85 mm 상수"는 제출본과 달라 정정한다.)
4. 목표 깊이도 Jetson이 계산해 기존 `SET_TARGET(depth_mm)`으로 보낸다. 깊이 규칙은 한성윤 확정값을 따른다.
5. 새 내부 상태는 만들지 않는다. 대신 동작 표시 필드를 둔다(§3).
6. 오류 유닛 `TU`(운반 유닛)를 추가한다(§6).

---

## 1. 하드웨어 전제와 미검증 항목

| 항목 | 현재 상태 (한건민 10/1) | 조치 | 기한 |
|---|---|---|---|
| 후륜 엔코더 | A/B 엔코더. 핀은 모터 1이 16/17, 모터 2가 20/21(기존 구동 모터 자료). 장착·배선·펄스 수·mm 환산 미검증 | `teensy/pinmap.h`와 `docs/hardware_mapping.md`에 반영. 1 m 직진 3회로 `enc_counts_per_mm` 교정 | 10/7 |
| **후륜 비상정지** | **후륜 전원을 독립적으로 차단하는 회로 없음.** 이전 시험은 파워서플라이 스위치로 정지 | 비상정지 회로가 후륜 모터 전원도 끊도록 배선한다(불변식 3). 완료 전에는 바퀴를 띄운 벤치에서만 `DRIVE_STEP`을 돌린다 | 10/7 사전 점검 전 |
| 제동 | 주행 PWM 0과 EN LOW로 출력만 끔. 능동 제동, 정지 거리, 정지 후 위치 유지 미검증 | 드라이버의 단락 제동 지원 여부 확인. 10/7에 정지 거리와 헤드 접촉 상태에서 바퀴 밀림(엔코더 변화)을 측정 | 10/7 |
| 자이로 | WT901C-TTL. Teensy UART 배정, 실물 수신, 설치 방향, 자기장 영향 미확인 | 엔코더가 Serial4(16/17)와 Serial5(20/21) 핀을 쓰므로 그 밖의 UART에 배정. 모터 근처라 방향은 지자기 대신 자이로 상대값으로 보는 방식을 검토 | 10/5 |
| 주행 부호 | 제안: `dist_mm > 0` 장치 전방, `< 0` 후방 | 동의. 전방과 모사체 머리 방향의 대응은 설치 절차(UI 안내)로 고정하고 Jetson 설정 `scan_dir_sign`으로 맞춘다 | — |
| 설정값 | 미확정 | §7 표에 자리만 둔다. 0이면 미설정으로 보고 해당 명령을 거부한다 | 10/6 |

---

## 2. 명령

### 2.1 새 명령

| 명령 | 파라미터 | 허용 조건 | 동작 | 완료 보고 |
|---|---|---|---|---|
| `DRIVE_STEP` | `dist_mm`(0 아님, 절댓값 ≤ `drv_step_max_mm`), `speed_mm_s`(선택, ≤ `drv_speed_max_mm_s`, 생략 시 `drv_speed_mm_s`) | `HOMED`, `drv_busy`와 `homing` 아님, 헤드가 HOME 위치이거나 `head_hold_active`, §7 주행 설정값 모두 설정됨 | 엔코더 기준으로 주행 → 정지 → 정지 확인 → 반력 평균 | `evt DRIVE_DONE` |
| `DRIVE_STOP` | — | **모든 상태** | 주행 중이면 즉시 출력 차단(제동 가능하면 제동). 이미 정지면 정지 유지. **FAULT·ESTOP을 해제하거나 출력을 다시 켜지 않는다.** 헤드 유지는 풀지 않는다 | ACK. 주행 중이었으면 `DRIVE_DONE result=STOPPED` |
| `HEAD_HOLD` | `press_mm`(> 0, ≤ `head_press_max_mm`) | `HOMED`, `drv_busy`와 `homing` 아님, `scan_force_max_N`·`contact_force_N` 설정됨 | §2.3 절차로 접촉점을 찾고 `press_mm`만큼 더 내려 **리니어 센서 기준 위치 유지**. 반력은 계측과 상한 감시에만 쓴다(힘 제어 아님) | `evt HEAD_HELD` |

**거부 사유(`ack ok:false`, `why`)**: `NOT_HOMED`, `DRIVE_BUSY`, `HOMING_BUSY`, `HEAD_NOT_READY`(헤드가 HOME도 유지도 아님), `DIST_RANGE`, `PRESS_RANGE`, `SPEED_RANGE`, `LIMIT_UNSET`(설정값 없음). 오류 코드는 기존 `2-CU-011` 체계를 쓰고, 설정값 없음은 새 코드 `2-TU-008`로 한다.

**중첩과 중복**
- 주행 중 새 `DRIVE_STEP`은 `DRIVE_BUSY`로 거부한다. 큐에 쌓지 않는다.
- 같은 `seq`를 다시 받으면 재실행하지 않고 ACK를 다시 보낸다(§4.1 기존 규칙). 그 명령이 이미 끝났으면 저장해 둔 `DRIVE_DONE`/`HEAD_HELD`도 다시 보낸다(§4.4).

### 2.2 기존 명령 변경

| 명령 | v1.0 | v1.1 |
|---|---|---|
| `HOME` | `ARMED`에서만 | `ARMED`, `HOMED`. `HOMED`에서는 `drv_busy`가 아닐 때만. 헤드 유지 중이면 유지를 끝내고 복귀한 뒤 `head_hold_active=false`, `evt HOMING_DONE` |
| `SET_CP_POINT` | `idx` 또는 `offset_mm` | 탐색 후 고정에서는 `VACUUM_HOLD`에서 `offset_mm=0`을 보낸다. 뜻은 "현재 위치가 압박점"이다. Teensy는 **현재 위치를 접촉점으로 간주하지 않고** §2.3 접촉 확인 절차로 헤드를 내려 **압박 접촉 영점**을 새로 잡는다. 접촉하지 못하면 `2-TU-006`. 성공하면 `evt CP_ZERO_SET` |
| `STIFFNESS_SCAN` | 고정 후 탐색 | 변경 없음. 결선 C안에서는 쓰지 않는다 |
| ESTOP 중 허용 명령 | `PING`, `GET_STATUS` | **`DRIVE_STOP` 추가**(ACK만, 출력 상태 변화 없음) |

### 2.3 접촉 확인 절차 (HEAD_HOLD, SET_CP_POINT 공통)

1. 헤드를 `head_search_speed_mm_s`로 내린다.
2. `cp_force_N ≥ contact_force_N`이 `contact_confirm_ms` 동안 이어지면 접촉으로 본다. 그 위치를 `head_contact_mm`로 기록한다.
3. `head_search_max_mm`까지 내려도 접촉이 없으면 멈추고 복귀하며 `2-TU-006`을 보낸다.
4. `HEAD_HOLD`는 접촉점에서 `press_mm` 더 내려 위치 유지에 들어간다. `SET_CP_POINT`는 접촉점을 압박 영점으로 둔다.
5. 이 과정 내내 `cp_force_N > scan_force_max_N`이면 `3-TU-003`.

**`press_mm` 기준**: 최초 접촉점(`head_contact_mm`)에서 잰 추가 하강량이다. 기구 원점 기준이 아니다.
**`press_mm = 0`**: 허용하지 않는다(`PRESS_RANGE`). 접촉만 하는 동작은 `SET_CP_POINT`가 맡는다.
**유지 해제**: `HOME`(복귀)으로만 푼다. `DRIVE_STOP`은 바퀴만 멈춘다. FAULT·ESTOP으로 모터가 차단되면 유지도 끝나고 `head_hold_active=false`가 된다.

---

## 3. 텔레메트리 추가 필드 (20 Hz, 측정할 수 없으면 생략)

| 필드 | 단위 | 설명 |
|---|---|---|
| `drv_busy` | bool | 주행 출력 중이거나 정지 확인 중 |
| `homing` | bool | HOME 동작 중 |
| `head_hold_active` | bool | 헤드 위치 유지 중 |
| `drv_pos_mm` | mm | 주행 원점(§5) 기준 누적 위치, 엔코더 두 바퀴 평균 |
| `drv_yaw_deg` | ° | 주행 원점에서의 방향 변화(자이로) |
| `drv_pitch_deg`, `drv_roll_deg` | ° | 기울기. 신뢰할 수 없으면 생략 |
| `head_pos_mm` | mm | 기구 HOME 원점 기준 헤드 위치, 하강이 양수 |
| `head_contact_mm` | mm | 마지막 접촉점(HOME 원점 기준) |
| `drv_last_seq` | int | 마지막으로 끝난 주행 명령의 seq |
| `drv_last_result` | string | 그 결과(`OK` `STOPPED` `SHORT` `FAULT`) |

**Jetson 규칙**: `state=HOMED`만 보고 정지로 판단하지 않는다. 주행 정지는 `drv_busy=false`, 헤드 상태는 `head_hold_active`와 `homing`으로 판단한다.

---

## 4. 이벤트

### 4.1 `HEAD_HELD`
```json
{"v":1,"t":"evt","evt":"HEAD_HELD","seq":21,"head_contact_mm":31.20,"head_pos_mm":41.20,"cp_force_N":18.40}
```

### 4.2 `DRIVE_DONE`
```json
{"v":1,"t":"evt","evt":"DRIVE_DONE","seq":22,"result":"OK","drive_mm":9.96,"drv_pos_mm":40.10,"drv_yaw_deg":0.60,"cp_force_N":28.40,"force_n":250}
```

| 필드 | 뜻 |
|---|---|
| `seq` | 원래 `DRIVE_STEP`(또는 `DRIVE_STOP`)의 seq. **evt에 seq를 싣는 것은 v1.1에서 새로 생기는 규칙** |
| `result` | `OK` 목표 거리 ± `drv_pos_tol_mm` 도달 / `STOPPED` `DRIVE_STOP`으로 중단 / `SHORT` 시간 제한 안에 거리 미달(`2-TU-004`와 함께) / `FAULT` 위험 오류로 정지(먼저 `err`) |
| `drive_mm` | 이번 명령으로 실제 이동한 거리(엔코더) |
| `drv_pos_mm` | 정지 후 누적 위치(§5 주행 원점 기준) |
| `cp_force_N` | 정지 확인 뒤 `drv_avg_ms` 구간의 반력 평균. **유효 샘플이 `drv_avg_min_n`보다 적거나 로드셀 오류면 필드를 생략한다. 0 N으로 대신 보내지 않는다** |
| `force_n` | 평균에 쓴 유효 샘플 수 |

### 4.3 완료 조건
1. 목표 거리에 이르거나 정지 요청·오류가 생기면 주행 출력을 끈다(제동 가능하면 제동).
2. 엔코더 변화가 `drv_settle_ms` 동안 없으면 정지로 확인한다.
3. 이어서 `drv_avg_ms` 동안 반력을 평균한다(500 Hz 원시는 Teensy 내부 기록).
4. 그 뒤 `DRIVE_DONE`을 보낸다. ACK는 접수만 뜻한다.
5. `drv_timeout_ms` 안에 1~3이 끝나지 않으면 정지하고 `result=SHORT`, `2-TU-004`.

### 4.4 완료 이벤트 유실
- Teensy는 마지막 `DRIVE_DONE`과 `HEAD_HELD`를 한 건씩 저장한다.
- Jetson은 `drv_timeout_ms + 1000 ms` 안에 `DRIVE_DONE`이 없으면 다음 순서를 따른다.
  1. `GET_STATUS`로 `drv_busy`, `drv_last_seq`, `drv_last_result`를 본다.
  2. 끝난 것이 확인되면 **같은 seq의 `DRIVE_STEP`을 다시 보낸다.** Teensy는 재실행하지 않고 ACK와 저장된 `DRIVE_DONE`을 다시 보낸다.
  3. 아직 `drv_busy`면 `DRIVE_STOP`을 보낸다. 그래도 결과가 없으면 FAULT를 표시하고 탐색을 중단한다.

---

## 5. 기준점 구분

| 기준 | 정의 | 바뀌는 때 | 프로토콜로 바꿀 수 있나 |
|---|---|---|---|
| 센서 영구 보정값 | 리니어 센서·로드셀 변환식 | 교정 작업 때만(펌웨어 설정) | **아니오.** 어떤 명령도 덮어쓰지 않는다 |
| 기구 HOME 원점 | 상단 리미트 기준 헤드 원점 | `HOME` 완료 | `HOME` |
| 탐색 접촉점 | 탐색 시작 때 모사체 표면 | `HEAD_HOLD` | `HEAD_HOLD` |
| 압박 접촉 영점 | 흡착 고정 뒤 압박 시작 기준 | `SET_CP_POINT(offset_mm=0)` | `SET_CP_POINT` |
| 주행 원점 | `drv_pos_mm = 0`, `drv_yaw_deg = 0` | `ARM` 완료, `HEAD_HELD` | `ARM`, `HEAD_HOLD` |

---

## 6. 오류 코드 (`TU` = 운반 유닛, 추가만 한다)

| 코드 | 의미 | 정지 범위 | latched | 복구 |
|---|---|---|---|---|
| `2-TU-001` | 방향 이탈(`|drv_yaw_deg| > drv_yaw_max_deg`) | 주행 정지, 헤드 유지 | 아니오 | 재정렬 뒤 새 명령 |
| `2-TU-002` | 기울기 초과(`> drv_tilt_max_deg`) | 주행 정지, 헤드는 `HOME` 요청을 기다림 | 아니오 | 사용자 확인 |
| `3-TU-003` | 탐색 반력 상한 초과(`> scan_force_max_N`) | **모든 모터 먼저 차단 → 보고** | 예 | 원인 해소 뒤 `RESET_FAULT`, 이어서 `HOME` |
| `2-TU-004` | 거리 미달·시간 초과(막힘, 미끄러짐) | 주행 정지 | 아니오 | 사용자 확인 |
| `3-TU-005` | 주행 엔코더 신호 상실 | 모든 모터 먼저 차단 → 보고 | 예 | `RESET_FAULT` |
| `2-TU-006` | 접촉 실패(`head_search_max_mm` 안에 접촉 없음) | 헤드 정지·복귀 | 아니오 | 위치 재정렬 |
| `2-TU-007` | 자이로 수신 끊김(`drv_imu_timeout_ms`) | 주행 정지 | 아니오 | 사용자 확인 |
| `2-TU-008` | 설정값 미설정으로 명령 거부 | 동작 없음 | 아니오 | 설정값 입력 |

- 심각도 3은 기존 §4.5대로 **Teensy가 먼저 정지하고 나중에 보고**한다.
- 주행 중 통신 두절은 기존 §9를 따른다. 압박 중이 아니므로 즉시 FAULT, 모터 차단, 음압 유지다.
- 정지 범위, latched 여부, 복구 조건은 한건민 검토 후 확정한다.

---

## 7. 설정값 (0 = 미설정 → 관련 명령 거부)

### 7.1 Teensy (한건민)

| 이름 | 단위 | 쓰임 | 근거 | 기한 |
|---|---|---|---|---|
| `scan_force_max_N` | N | 접촉 탐색·헤드 유지·주행 중 반력 상한 | 모사체·고정구·기구의 허용 하중과 보정된 로드셀 실측. 로드셀 정격(100 kg)을 쓰지 않는다 | 10/6 |
| `contact_force_N`, `contact_confirm_ms` | N, ms | 접촉 판정 | 무부하 로드셀 잡음 실측 | 10/6 |
| `head_search_speed_mm_s`, `head_search_max_mm` | mm/s, mm | 접촉 탐색 속도·하강 한도 | 기구와 마네킹 치수 | 10/6 |
| `head_press_max_mm` | mm | `press_mm` 상한 | 기구 | 10/6 |
| `drv_speed_mm_s`, `drv_speed_max_mm_s`, `drv_accel_mm_s2` | mm/s, mm/s² | 주행 속도·가감속 | 10/7 정지 거리 측정 | 10/7 |
| `drv_step_max_mm` | mm | 1회 이동 상한 | 탐색 구간 | 10/6 |
| `drv_timeout_ms` | ms | 1회 이동 시간 제한 | 속도와 상한에서 산정 | 10/6 |
| `drv_settle_ms`, `drv_avg_ms`, `drv_avg_min_n` | ms, ms, 개 | 정지 확인·반력 평균·유효 샘플 수 | 10/7 측정 | 10/7 |
| `drv_pos_tol_mm` | mm | 도달 판정 허용 오차 | 엔코더 교정 결과 | 10/7 |
| `drv_yaw_max_deg`, `drv_tilt_max_deg` | ° | 방향·기울기 허용치 | 10/7 측정 | 10/7 |
| `drv_imu_timeout_ms`, `drv_enc_timeout_ms` | ms | 센서 타임아웃 | 센서 주기 | 10/6 |
| `enc_counts_per_mm` | 개/mm | 엔코더 환산 | 1 m 직진 3회 | 10/7 |

### 7.2 Jetson (신상현)

| 이름 | 값 | 근거 |
|---|---|---|
| `press_mm` | 10.0 | 제출 보고서 6.2 (δ₀) |
| `cp_dist_ref_mm`, `eapd_ref_mm`, `cp_dist_max_mm` | 89.7, 205.8, 95 | 제출 보고서 6.2 식. 설계 가설이며 인체 검증 없음 |
| 판정 기준 | 직전 이동평균 대비 30 % 감소 | 제출 보고서 6.2 |
| `scan_step_mm`, `scan_roi_mm` | 미정 | 모사체 실측 후(선정용 반복) |
| `scan_dir_sign` | 미정 | 설치 방향 확인 후 |
| EAPD 미측정 시 이동 거리 | 미정 | 보고서에 규칙 없음. 합의 필요 |

---

## 8. 시험

**기본 묶음(하드웨어 없음)**
- Jetson `tests/test_all.py` + 모의 Teensy:
  - 상태 불일치 거부
  - 설정값 미설정 거부
  - 주행 중 `DRIVE_STEP` 거부
  - 중복 seq: 재실행하지 않고 ACK와 `DRIVE_DONE`을 다시 보냄
  - `DRIVE_DONE` 유실 → `GET_STATUS` → 재요청
  - FAULT·ESTOP 중 `DRIVE_STOP`이 상태를 바꾸지 않음
  - 주행 중 통신 두절 → FAULT
  - 반력이 무효면 필드 생략
- Teensy `test_saver_proto.cpp`:
  - 음수·소수 `dist_mm` 파싱
  - 필드 생략 규칙
  - evt의 seq

**하드웨어 시험(기본 묶음 밖, 실물 필요)**
- 10/7: 바퀴를 띄운 벤치 → 바닥 주행 순서로 엔코더 환산, 정지 거리, 헤드 접촉 상태의 바퀴 밀림, 자이로 수신을 확인한다.
- 비상정지가 후륜 전원을 끊는지 확인한다. 끊지 않으면 바닥 주행을 하지 않는다.

---

## 9. 일정

| 날짜 | 신상현 | 한건민 |
|---|---|---|
| 10/2 | 초안 공유 | 검토 의견 |
| 10/3 | 의견 반영, 합의 | 합의 |
| 10/3~5 | `protocol/` v1.1 반영(한건민 검토 뒤 커밋), Jetson 모의 Teensy와 시험 | 펌웨어 구현과 시험, 후륜 비상정지 배선 |
| 10/6 | 설정값 7.1 중 10/6 항목 확정(팀) | 같이 |
| 10/7 | — | Teensy 단독 주행·제동·헤드 유지 점검, 7.1 중 10/7 항목 측정 |
| 10/11 | 벤치 통신 시험(새 명령 포함, 모터 없이) | 같이 |
| 10/12 | 장치 연결 | 같이 |
