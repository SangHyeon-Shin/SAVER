# SAVER USB 통신 프로토콜 v1.0

**Jetson Nano (2019) ↔ Teensy 4.1**

| 항목 | 내용 |
|---|---|
| 문서 버전 | v1.0 (초안) |
| 작성 | 신상현 (SW/Jetson) |
| 검토·확정 | 한건민 (제어/Teensy) |
| 확정 기한 | **2026-07-31** — 이 날짜까지 합의되지 않으면 8/2 통합시험 불가 |
| 적용 범위 | Jetson Nano ↔ Teensy 4.1 USB 시리얼 구간만. 센서 내부 통신(I²C/SPI)은 범위 밖 |

> **이 문서는 계약서입니다.** 여기 적힌 필드명·단위·동작만 구현하고, 필요한 변경은
> 이 문서를 먼저 고친 뒤 양쪽이 반영합니다. 코드가 문서를 앞서가면 통합이 깨집니다.

---

## 1. 책임 경계

| 구분 | Jetson Nano (신상현) | Teensy 4.1 (한건민) |
|---|---|---|
| 역할 | 판단·통합·UI·기록 | **실시간 제어·센서·안전** |
| 제어 주기 | 없음 (10~20 Hz 표시용 수신) | 100~500 Hz 폐루프 |
| 압박 모터 | 목표값·시작/정지 **요청**만 | PID·PWM·엔코더·리미트 |
| 음압 | 목표 음압·모드 요청 | 압력 피드백·PWM·인터록 |
| PPG (MAX30102) | 결과 수신·표시·기록 | 수집·필터링·HR/SpO₂ 산출 |
| MR60BHA2 레이더 | 직접 수신·해석 | 관여 안 함 |
| 카메라 | 직접 수신·처리 | 관여 안 함 |
| 상태머신 | 운용 상태 S1~S13 | 구동부 내부 상태 (별도 이름) |
| 비상정지 | 표시·기록만 | **즉시 모터 차단** |

### 불변식 — 위반하면 안 되는 규칙

1. **Jetson은 PWM 값이나 모터 전압을 직접 지정하지 않는다.** 목표 BPM·깊이·힘 한계만 보낸다.
2. **Jetson의 명령은 요청이다.** Teensy는 자체 인터록을 검사한 뒤에만 실행하고, 거부할 수 있다.
3. **소프트웨어 정지는 물리 비상정지를 대체하지 않는다.** E-stop은 USB와 무관한 하드웨어 회로로 모터 출력을 차단하고, Teensy는 그 사실을 보고만 한다.
4. **Teensy는 Jetson이 죽어도 단독으로 안전 상태를 유지한다.**
5. **Teensy 상태 이름에 S1~S13을 쓰지 않는다.** 그건 Jetson 운용 상태다. (§7)

---

## 2. 물리 계층

> **플랫폼 확정: Teensy 4.1** (2026-08-03). PCB 는 발주하지 않았습니다.
> 저장소 이력의 회로도·펌웨어는 ESP32-S3 기준이므로 **핀 배정과 ADC 분해능이 다릅니다.**
> 그 자료는 `teensy/reference/` 에 하드웨어 사실 참고용으로만 남겨 두었습니다.
> 회로도는 Teensy 4.1 기준으로 다시 그려야 합니다.

| 항목 | 값 |
|---|---|
| 연결 | Teensy 4.1 내장 USB (USB CDC) → Jetson `/dev/ttyACM0` |
| Baud | `115200` 관례상 설정. **Teensy 4.1 네이티브 USB는 baud를 무시**하므로 실제 속도와 무관 |
| 흐름제어 | 없음 |
| 인코딩 | UTF-8 (실제로는 ASCII만 사용) |
| 포트 식별 | Jetson은 VID/PID `16C0:0483` (Teensy) 우선 탐색, 실패 시 설정 파일의 경로 |

Jetson 측 권한: `sudo usermod -aG dialout $USER` 후 재로그인.

---

## 3. 프레이밍 · 공통 규칙

**한 줄에 JSON 객체 하나** (newline-delimited JSON / JSON Lines).

```
{"v":1,"t":"cmd","seq":104,"cmd":"START_CPR"}\n
```

| 규칙 | 내용 |
|---|---|
| 줄바꿈 | `\n` (LF) 만 사용. `\r\n` 금지 |
| 최대 길이 | 512 byte. 초과분은 폐기하고 `1-CM-003` 보고 |
| 중첩 | **금지.** 1단계 평면 객체만 (파서 단순화) |
| 배열 | `faults` 등 문자열 배열만 허용 |
| 미지 필드 | 수신 측은 **무시**한다 (전방 호환) |
| 파싱 실패 | 줄 하나만 버리고 계속 동작. 절대 재시작하지 않음 |
| 부동소수 | 소수점 2자리까지. `NaN`/`Infinity` 금지 |
| 값 없음 | 필드를 **생략**한다. `null` 금지 |

### 공통 헤더 필드

| 필드 | 타입 | 설명 |
|---|---|---|
| `v` | int | 프로토콜 버전. 현재 `1`. 불일치 시 `3-CM-001` 후 안전 상태 |
| `t` | string | 메시지 타입: `cmd` `ack` `tel` `evt` `err` |
| `ms` | int | 송신 측 `millis()` 단조 시각 (선택, 디버깅용) |

---

## 4. 메시지 타입

### 4.1 `cmd` — Jetson → Teensy

```json
{"v":1,"t":"cmd","seq":104,"cmd":"SET_TARGET","bpm":110,"depth_mm":50.0,"force_max_N":600}
```

- `seq`: 1~65535 순환. **모든 명령에 필수.**
- Teensy는 **최근 8개 seq를 기억**하고 중복 수신 시 재실행하지 않는다. 단, ACK는 다시 보낸다.

### 4.2 `ack` — Teensy → Jetson

```json
{"v":1,"t":"ack","seq":104,"ok":true,"state":"CPR_RUNNING"}
{"v":1,"t":"ack","seq":105,"ok":false,"err":"2-CU-011","why":"NOT_ARMED"}
```

- 모든 `cmd`에 **200 ms 이내** 응답.
- 거부 시 `ok:false` + `err` 코드 + `why` (사람이 읽는 짧은 사유).
- Jetson은 200 ms 타임아웃, **최대 2회 재전송**, 그래도 없으면 `FAULT` 표시. 무한 재전송 금지.

### 4.3 `tel` — Teensy → Jetson (주기 송신)

```json
{"v":1,"t":"tel","ms":184320,"state":"CPR_RUNNING","cp_rate_cpm":109.8,"cp_depth_mm":49.2,
 "cp_force_N":382.1,"cp_motor_i_A":6.8,"vac_kPa":-27.6,"ppg_hr_bpm":72.4,"spo2_pct":97,
 "ppg_sqi":0.86,"temp_C":38.2,"fan_pct":40,"estop":false,"limit_top":false,
 "limit_bottom":false,"faults":[]}
```

기본 **20 Hz (50 ms)**. `SET_TEL_RATE`로 1~50 Hz 조정 가능.
고속 원시 데이터(100~500 Hz)는 **Teensy 내부에만** 두고 올리지 않는다.

### 4.4 `evt` — Teensy → Jetson (비동기 사건)

```json
{"v":1,"t":"evt","ms":190112,"evt":"HOMING_DONE"}
{"v":1,"t":"evt","ms":193004,"evt":"SCAN_RESULT","points":8,"best_idx":4,"stiff":[1.2,1.5,2.1,...]}
```

### 4.5 `err` — Teensy → Jetson (고장)

```json
{"v":1,"t":"err","ms":201500,"code":"3-CU-021","sev":3,"latched":true,"why":"OVER_CURRENT"}
```

- `latched:true` 면 `RESET_FAULT`를 받기 전까지 해제되지 않는다.
- `sev` 3(위험)은 Teensy가 **먼저 모터를 정지시킨 뒤** 보고한다. 보고와 정지의 순서를 바꾸지 않는다.

---

## 5. 명령 목록

| 명령 | 파라미터 | 동작 | 허용 상태 |
|---|---|---|---|
| `PING` | — | 생존 확인. ACK만 | 모두 |
| `GET_STATUS` | — | `tel` 1회 즉시 송신 | 모두 |
| `ARM` | — | 구동 허가. 인터록 전체 검사 | `IDLE` |
| `DISARM` | — | 구동 금지 | `ARMED` 이후 |
| `HOME` | — | 압박부 원점 복귀 | `ARMED` |
| `SET_TARGET` | `bpm`, `depth_mm`, `force_max_N` | 목표값 설정 (즉시 구동 안 함) | `ARMED` 이상 |
| `VACUUM_ON` | `target_kPa` | 흡착 시작 | `ARMED` |
| `VACUUM_OFF` | — | 음압 해제 | 모두 |
| `STIFFNESS_SCAN` | `points`, `probe_N` | 강성 매핑 스캔 실행 → `SCAN_RESULT` | `VACUUM_HOLD` |
| `SET_CP_POINT` | `idx` 또는 `offset_mm` | 압박점 확정 | `VACUUM_HOLD` |
| `TRIAL_COMPRESSION` | `cycles` | 시험 압박 → `TRIAL_RESULT` | `CPR_READY` |
| `START_CPR` | — | 압박 개시 | `CPR_READY` |
| `PAUSE_CPR` | — | 압박 일시정지 (음압 유지) | `CPR_RUNNING` |
| `RESUME_CPR` | — | 압박 재개 | `CPR_PAUSED` |
| `STOP_CPR` | `reason` | 압박 종료 | `CPR_RUNNING`/`CPR_PAUSED` |
| `RESET_FAULT` | — | latched 고장 해제 시도 | `FAULT` |
| `SET_TEL_RATE` | `hz` | 텔레메트리 주기 변경 (1~50) | 모두 |

`reason` 값: `USER_STOP` `ROSC_SUSPECTED` `SAFETY` `COMM_LOST` `END`

### Jetson 상태머신과의 연결

Jetson의 S1~S13에서 어떤 명령이 나가는지는 `protocol/state_machine_v2.json`이 기준입니다.
대표 대응만 적으면 아래와 같습니다.

| Jetson 상태 | 나가는 명령 |
|---|---|
| S1 초기화 | `PING` → `GET_STATUS` → `ARM` → `HOME` (오류 확인 뒤 S1에서 다시, v2.3) |
| S5 흡착·고정 | `VACUUM_ON` |
| (압박점 탐색) | `STIFFNESS_SCAN` → `SET_CP_POINT` |
| S6 압박 준비 | `SET_TARGET` → `SET_CP_POINT` (v2.2) |
| S7 압박 수행 | 처음(S6에서) `START_CPR`. 일시정지 뒤 재개는 `RESUME_CPR` (장치가 `CPR_PAUSED`일 때만 재개 이벤트를 받음, v2.3) |
| S8 일시정지, S9 회복 확인, X_VENT 인공호흡 안내, X_AED, X_POSOUT 위치 이탈 | `PAUSE_CPR` |
| S11 안전정지 | `STOP_CPR{reason:SAFETY}` |
| S12 수동 해제 | `VACUUM_OFF` |

---

## 6. 텔레메트리 필드

**단위를 필드명에 반드시 포함한다.** W7 CSV 로그 헤더와 이름을 맞췄습니다.

| 필드 | 단위 | 범위 | 설명 |
|---|---|---|---|
| `state` | — | §7 | Teensy 내부 상태 |
| `cp_rate_cpm` | 회/분 | 0~150 | 압박 속도 |
| `cp_depth_mm` | mm | 0~80 | 직전 사이클 최대 압박 깊이 |
| `cp_force_N` | N | 0~800 | 압박력 (로드셀) |
| `cp_motor_cmd` | % | -100~100 | PWM 듀티 (진단용) |
| `cp_motor_i_A` | A | 0~30 | 모터 전류 |
| `vac_kPa` | kPa | -80~0 | 게이지 압력 (음압이므로 음수) |
| `vac_leak` | bool | — | 누설 판정 |
| `ppg_hr_bpm` | 회/분 | 0~250 | PPG 심박 |
| `spo2_pct` | % | 0~100 | 산소포화도 |
| `ppg_sqi` | 0~1 | — | 신호 품질 지수 |
| `temp_C` | °C | -20~120 | 구동부 온도 (DS18B20) |
| `fan_pct` | % | 0~100 | 냉각팬 출력 |
| `estop` | bool | — | 비상정지 회로 상태 |
| `limit_top` / `limit_bottom` | bool | — | 리미트 스위치 |
| `faults` | string[] | — | 현재 활성 오류 코드 |

**부호 규약**: 음압은 게이지압 기준 **음수**로 보낸다. `-27.6` = 대기압보다 27.6 kPa 낮음.
압박 방향 변위는 **하강이 양수**.

**측정 불가 시**: 해당 필드를 **생략**한다. `0`을 보내면 안 된다.
Jetson은 필드 부재를 "미측정"으로 처리하고 정상값으로 간주하지 않는다.

---

## 7. Teensy 내부 상태

Jetson의 S1~S13과 **이름이 겹치면 안 됩니다.** 계층이 다릅니다.

```
IDLE ──ARM──> ARMED ──HOME──> HOMED ──VACUUM_ON──> VACUUM_HOLD
                                                        │
                                          SET_CP_POINT  ▼
                                                    CPR_READY
                                                        │ START_CPR
                                                        ▼
                                    CPR_PAUSED <──> CPR_RUNNING
                                                        │ STOP_CPR
                                                        ▼
                                                     HOMED

어느 상태에서든:  E-stop 입력 → ESTOP      (최우선)
                 sev 3 고장  → FAULT
```

| 상태 | 모터 | 음압 | 설명 |
|---|---|---|---|
| `IDLE` | 차단 | 해제 | 전원 인가 직후 |
| `ARMED` | 대기 | 해제 | 인터록 통과, 구동 허가됨 |
| `HOMED` | 정지 | 해제 | 원점 확인됨 |
| `VACUUM_HOLD` | 정지 | 유지 | 흡착 완료 |
| `CPR_READY` | 정지 | 유지 | 압박점·목표값 확정 |
| `CPR_RUNNING` | 구동 | 유지 | 압박 중 |
| `CPR_PAUSED` | 정지 | 유지 | 일시정지 |
| `FAULT` | 차단 | 유지 | latched 고장 |
| `ESTOP` | **하드웨어 차단** | 유지 | 비상정지 |

`ESTOP`에서 음압을 유지하는 이유: 흡착을 갑자기 풀면 장치가 환자 흉부에서 튕길 수 있습니다.
해제는 `VACUUM_OFF` 명령이나 수동 조작으로만 합니다. — **한성윤 확인 필요**

---

## 8. 오류 코드 (W7 체계 `S-UU-EEE`)

`S` = 심각도, `UU` = 유닛, `EEE` = 일련번호

| S | 등급 | UI 반응 | 동작 |
|---|---|---|---|
| 1 | 주의 | 노랑 띠 | 계속 진행 |
| 2 | 경고 | 주황 화면 | 사용자 확인 요구 |
| 3 | 위험 | 빨강 점멸 | **즉시 정지** |

| UU | 유닛 |
|---|---|
| `CU` | 압박부 (Compression Unit) |
| `VU` | 음압부 (Vacuum Unit) |
| `SU` | 센서 일반 |
| `PU` | PPG |
| `MU` | MCU·전원 |
| `CM` | 통신 |

### 초기 코드표

| 코드 | 의미 | 조치 |
|---|---|---|
| `1-CM-003` | 512 byte 초과 / 파싱 실패 | 줄 폐기 |
| `2-CM-002` | ACK 미수신 (Jetson 측 판정) | 재전송 2회 후 FAULT |
| `3-CM-001` | 프로토콜 버전 불일치 | 안전 상태 진입 |
| `3-CM-010` | **통신 두절** (§9) | §9 규칙 적용 |
| `2-CU-011` | ARM 되지 않은 상태에서 구동 명령 | 명령 거부 |
| `2-CU-012` | 목표 깊이 도달 실패 | 사용자 확인 |
| `3-CU-021` | 모터 과전류 | 즉시 정지 · latched |
| `3-CU-022` | 리미트 스위치 이탈 | 즉시 정지 · latched |
| `3-CU-023` | 엔코더 신호 상실 | 즉시 정지 · latched |
| `2-VU-004` | 음압 목표 미달 | 재시도 안내 |
| `2-VU-005` | 음압 누설 감지 | 사용자 확인 |
| `3-VU-006` | 흡착 상실 (압박 중) | 즉시 정지 · latched |
| `2-VU-007` | 음압 모터 드라이버 ERR 신호 | 사용자 확인 |
| `1-MU-010` | 구동부 온도 상승 (팬 가동) | 표시만 |
| `2-MU-011` | 구동부 온도 경고 | 사용자 확인 |
| `1-PU-001` | PPG 신호 품질 저하 | 표시만 |
| `2-PU-002` | PPG 센서 미착용/탈락 | 사용자 확인 |
| `3-MU-001` | 전원 전압 하한 미달 | 즉시 정지 |

> 코드는 추가만 하고 **의미를 바꾸지 않습니다.** 로그 해석이 깨집니다.

---

## 9. 타이밍 · 워치독 · 통신 두절

| 항목 | 값 |
|---|---|
| 텔레메트리 주기 | 50 ms (20 Hz) 기본 |
| ACK 응답 한계 | 200 ms |
| Jetson 재전송 | 2회, 그 후 FAULT |
| Jetson → Teensy 하트비트 | `PING` 1000 ms 간격 |
| Teensy 워치독 | **2000 ms** 동안 어떤 메시지도 없으면 통신 두절 판정 |
| Jetson stale 판정 | 텔레메트리 500 ms 이상 없으면 표시값을 회색 처리, 정상값으로 쓰지 않음 |

### 통신 두절 시 동작 — ⚠️ 미확정

압박 **중** 통신이 끊겼을 때 두 가지가 충돌합니다.

- 즉시 정지하면 → 무수축 시간이 생겨 환자에게 불리
- 계속 압박하면 → 감시 없는 구동

**현재 초안**: 마지막 목표값으로 압박을 **계속**하되, 두절 `T_comm` 경과 후 안전 정지 + 경보.
`T_comm` 기본값은 플레이스홀더 `30 s`로 두고, **의학적 근거로 확정 필요 (한성윤)**.

압박 중이 **아닐 때** 두절: 즉시 `FAULT`, 모터 차단, 음압 유지.

```
CPR_RUNNING 중 두절
  → 계속 압박 (T_comm)
  → 초과 시 STOP + 3-CM-010 + 부저
그 외 상태에서 두절
  → 즉시 FAULT, 모터 차단, 음압 유지
재연결 시
  → Jetson이 GET_STATUS로 상태 재동기화
  → Teensy는 자동으로 CPR을 재개하지 않는다 (반드시 명시적 START_CPR)
```

---

## 10. 대표 시퀀스

### 정상 개시

```
Jetson                              Teensy
  │── cmd PING seq=1 ───────────────>│
  │<───────────── ack seq=1 ok ──────│
  │── cmd ARM seq=2 ────────────────>│  인터록 검사
  │<───────────── ack seq=2 ok ──────│  → ARMED
  │── cmd HOME seq=3 ───────────────>│
  │<───────────── ack seq=3 ok ──────│
  │<──────── evt HOMING_DONE ────────│  → HOMED
  │── cmd VACUUM_ON kPa=-30 seq=4 ──>│
  │<───────────── ack seq=4 ok ──────│
  │<──── tel (vac_kPa 하강 중) ──────│  20 Hz
  │<──────── evt VACUUM_OK ──────────│  → VACUUM_HOLD
  │── cmd STIFFNESS_SCAN seq=5 ─────>│
  │<──────── evt SCAN_RESULT ────────│
  │── cmd SET_CP_POINT idx=4 seq=6 ─>│
  │── cmd SET_TARGET bpm=110 seq=7 ─>│  → CPR_READY
  │── cmd START_CPR seq=8 ──────────>│  → CPR_RUNNING
  │<──────── tel (압박 데이터) ───────│
```

### 과전류 발생

```
  │<──── err 3-CU-021 latched ───────│  ※ 모터 정지가 먼저, 보고가 나중
  │  UI: S10 경고 화면                │  → FAULT
  │── cmd RESET_FAULT seq=41 ───────>│
  │<──── ack seq=41 ok=false ────────│  원인 미해소 시 거부
```

### 중복 명령

```
  │── cmd START_CPR seq=8 ──────────>│  실행 → CPR_RUNNING
  │   (ACK 유실)                      │
  │── cmd START_CPR seq=8 ──────────>│  seq 중복 → 재실행 안 함
  │<───────────── ack seq=8 ok ──────│  ACK만 다시 송신
```

---

## 11. 미확정 항목

| # | 항목 | 담당 | 기한 |
|---|---|---|---|
| 1 | 통신 두절 시 압박 지속 시간 `T_comm` | 한성윤 (의학 근거) | 7/31 |
| 2 | 목표 압박 깊이·속도 확정값 | 한성윤 + 가이드라인 | 7/31 |
| ~~3~~ | ~~과전류 임계~~ → **`I_LIMIT_A = 12.0 A`, `I_LIMIT_FIRSTRUN_A = 6.0 A` 확정** | — | 완료 (2026-08-03) |
| ~~4~~ | ~~목표 음압 및 누설 판정 기준~~ → **`VAC_TARGET_KPA = -22.5 kPa`, `VAC_TOL_KPA = 2.5 kPa`, `VAC_MIN_START_KPA = -15.0 kPa`, `VAC_ABORT_KPA = -10.0 kPa` 확정** | — | 완료 (2026-08-03) |
| ~~4a~~ | ~~ESP32 reference의 -30.0 kPa, 허용오차 ±0.5 kPa 확인~~ → 위 확정값으로 대체 | — | 완료 (2026-08-03) |
| 5 | 강성 스캔 파라미터 (`points`, `probe_N`) | 신상현 + 한건민 | 8/2 |
| 6 | ESTOP 시 음압 유지 여부 | 한성윤 | 7/31 |
| ~~7~~ | ~~플랫폼 확정~~ → **Teensy 4.1 확정 (2026-08-03). PCB 미발주.** | — | 완료 |
| 7a | PCB 회로도를 Teensy 4.1 기준으로 재작성 | 한건민 | 8/6 |
| 8 | **구동계(바퀴) 제어 명령이 프로토콜에 없음.** 이전 코드에 모터 4개 존재. UI 화면 02·03 대응 명령 필요 여부 | 신상현 + 한건민 | 7/31 |
| 9 | 냉각팬 제어 주체 (Teensy 자율 vs Jetson 명령)는 미확정. **`TEMP_FAN_ON_C = 40.0 °C`, `TEMP_STOP_C = 70.0 °C`는 확정.** | 한건민 | 제어 주체 확인 필요 |

**빈칸을 추측으로 채우지 않습니다.** 미확정 값은 `config`의 플레이스홀더로 두고, 확정 전에는
그 값을 쓰는 기능을 활성화하지 않습니다.

---

## 12. 변경 이력

| 버전 | 날짜 | 내용 |
|---|---|---|
| v1.0 | 2026-07-26 | 초안 작성 (신상현) |
| v1.0a | 2026-08-03 | 저장소 이력에서 확인된 하드웨어 사실 반영 (신상현)<br>· 온도·팬 telemetry 필드 추가<br>· 음압 드라이버 ERR, 온도 오류 코드 추가<br>· 플랫폼 불일치(ESP32-S3) 경고 추가<br>· 미확정 항목 7~9 추가 |
| v1.0b | 2026-10-05 | §5 상태머신 대응표를 `state_machine_v2.json` v2.3에 맞춤(한건민 합의 1~3): 재개는 `RESUME_CPR`, 일시정지 노드의 재개는 `CPR_PAUSED` 확인 뒤, S1 오류 뒤 재점검, X_POSOUT 재정렬 재개 삭제. 메시지 형식 변경 없음 |
| | | ← 한건민 검토 의견 반영 후 v1.1 |
