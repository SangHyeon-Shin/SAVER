# 프로토콜 v1.1 초안: 주행 탐색 명령 (검토용, 5차)

| 항목 | 내용 |
|---|---|
| 상태 | **초안, 합의 전.** `protocol/`은 공동 영역이라 이 문서에서 먼저 합의한다 |
| 작성 | 신상현 (2026-10-01 1차, 2026-10-01 2차, 2026-10-02 3차, 2026-10-03 4차, 2026-10-04 5차) |
| 검토 | 한건민 (10/1 1차 회신, 10/1 2차 회신 §0.2, 10/2 3차 회신 §0.3, 10/3 4차 회신 §0.4, 10/4 5차 회신 §0.5) |
| 범위 | 탐색 후 고정 순서(제출 보고서 6.2~6.3)에 필요한 명령, 이벤트, 텔레메트리, 오류 코드 |
| 반영 | 합의 후 `protocol/teensy_usb_protocol_v1.md`를 v1.1로 고치고 `jetson/tests/test_all.py`, `teensy/test_saver_proto.cpp`를 함께 고친다 |
| 핀맵 기준 | **GitHub `main` 1f19e1c의 `teensy/pinmap.h`**(2026-09-08 판). 2차 초안은 그 이전 핀맵을 보고 썼다(§0.3-1) |
| 와이어 버전 | `v:1` 유지. 펌웨어 기능은 `GET_INFO`로 확인한다(§2.4). v1.0 펌웨어는 `GET_INFO`를 포함한 새 명령을 `1-CM-005 UNKNOWN_COMMAND`로 거부하고, Jetson은 그때 탐색 기능을 끈다 |

> 연구용 시제품의 마네킹·모사체 벤치 시험 범위다. 아래 설정값은 비어 있으면 해당 명령을 거부한다.
> 값을 임의로 채우지 않는다. 이 문서의 어떤 항목도 실물에서 검증된 것이 아니다.

---

## 0. 합의 상태

### 0.1 합의된 것 (10/1)

1. 새 명령은 `DRIVE_STEP(dist_mm)`, `DRIVE_STOP`, `HEAD_HOLD(press_mm)` 3개다. 2차에서 조회 명령 `GET_INFO`를 추가로 제안한다(§2.4).
2. 말단 판정, 압박점 위치 계산, EAPD 계산은 **Jetson**이 한다. Teensy는 요청을 실행하고 인터록으로 거부·정지한다.
3. 압박점 이동 거리는 제출 보고서 6.2 식 그대로 Jetson이 계산한다.
   `D = 89.7 mm × EAPD / 205.8 mm`, 상한 95 mm.
   **EAPD가 없을 때 대체 상수(예: 85 mm)를 쓰지 않는다.** Teensy도 넣지 않는다. Jetson은 이동을 거부하고 재측정을 안내한다(§7.2).
4. 목표 깊이도 Jetson이 계산해 기존 `SET_TARGET(depth_mm)`으로 보낸다. 깊이 규칙은 한성윤 확정값을 따른다.
5. 새 내부 상태는 만들지 않는다. 대신 동작 표시 필드를 둔다(§3).
6. 오류 유닛 `TU`(운반 유닛)를 추가한다(§6).
7. `DRIVE_STOP`을 모든 상태에서 허용하는 것, 기준점을 구분하는 것(§5), 측정할 수 없는 반력은 생략하는 것에는 한건민도 동의했다.

### 0.2 2차 회신 반영

| # | 한건민 지적 | 반영 |
|---|---|---|
| 1 | 완료 이벤트 유실. 중복 기록은 최근 8개뿐이라 밀려난 명령이 재실행될 수 있음 | §2.5 동작 명령 기록을 일반 중복 기록과 분리, `STALE_SEQ` 거부, `boot_id`로 재부팅 감지, 중단 시 원래 seq 유지, `HEAD_HELD` 재요청 |
| 2 | `drv_busy` 범위, 접촉 탐색 중 명령 중첩 | §3 `busy` 필드 하나로 통합, 결과 저장까지 유지, 동작 명령은 한 번에 하나 |
| 3 | 정지 범위와 음압, 원인 지속, 센서 상실, `RESET_FAULT` 뒤 순서 | §6 주행·압박 축·음압 열 분리, 원인 지속 거부, 센서 상실 코드 2개, `RESET_FAULT → IDLE → ARM → HOME` |
| 4 | 엔코더 무변화만으로 정지 판단 불가, 결과 구분 | §4.3 엔코더 유효성 조건, §4.2 `result` 6종, 엔코더 무효면 거리 생략 |
| 5 | NAU7802는 최대 320 SPS | §4.3 500 Hz 루프와 ADC 신규 변환 구분, `force_n`은 신규 변환만 |
| 6 | 설정값 순환 | §7 10/6 벤치 시험용 값(근거·책임) → 10/7 검증·조정 |
| 7 | 상단 리미트 미설치, 주행 원점 초기화 시점 | §1, §5. HOME은 상단 리미트 설치·검증 뒤. 주행 원점은 새 탐색 시작에만 0 |
| 8 | WT901C 핀, 자기장, 펌웨어 식별 | §1 Serial8, §2.4 `GET_INFO` |

### 0.3 3차 회신 반영 (10/2)

한건민 동의: 완료 기록 분리, `busy` 통합, 음압 상태 보존, 신규 ADC 변환만 집계.

| # | 한건민 지적 | 반영 |
|---|---|---|
| 1 | 핀맵 기준 브랜치가 다름. main 1f19e1c는 홀 39번, 32/37번 예비, A9는 로드셀 ADC 예약, NAU7802 시험 코드는 I²C 18/19. VAC2·자이로 이동과 PPG 시험 코드의 35번 보호 설정은 미반영 | §1을 main 기준으로 다시 씀. 이동은 **VAC2 RUN 34→32, EN 35→37, WT901C Serial8 RX8 34, TX8 35, PPG 시험 코드 EN 보호 35→37**을 한 묶음으로 바꾼다 |
| 2 | 기울기 초과 위치 유지는 조건부. 리니어 센서만으로 정하지 않음, 하강 목표 취소, 위험 오류·ESTOP 우선, 출력 차단 뒤 기계적 유지 실측 | §6 `2-TU-002`에 유지 조건 5개와 기준 위치, 우선순위. §1, §10에 기계적 유지 실측 |
| 3 | `DRIVE_STOP` 중단 상태 구분. 중단된 HOME·접촉 탐색을 성공으로 보지 않음, 이후 `DRIVE_STEP` 허용 구분, 유지 중 새 seq `HEAD_HOLD` 재실행 조건 | §4.5 중단 뒤 상태표. `HEAD_HOLD`는 헤드가 HOME 위치일 때만, 유지 중이면 `ALREADY_HOLDING`, 시작 시 접촉 중이면 `IN_CONTACT` |
| 4 | `STALE_SEQ` 순환 비교 한계, 한 바퀴 순환 충돌, Jetson만 재시작 시 seq 복구, 재사용 방지, 같은 seq의 명령·파라미터 일치, 진행 기록의 원래 ACK | §2.5를 다시 씀. **순서 비교를 없애고** 동작 기록 16개와 대조, 수신 번호로 오래된 기록 만료, 명령·파라미터 불일치는 `SEQ_CONFLICT`, `INFO.last_seq`로 Jetson seq 복구, 진행 기록에 원래 ACK |
| 5 | 합성 텔레메트리 531 byte로 512 byte 초과 | §3. 512 byte는 유지하고 `tel`(기존 + 상태 4개)과 새 메시지 `mot`(주행·헤드·완료 기록)로 나눈다. 최악값 계산 `tel` 429(`faults` 6개면 470), `mot` 292 byte. `faults` 최대 6개. 양쪽에 최대 길이 시험 |
| 6 | 조회 응답 유실 시 ACK만 옴. `3-TU-009`와 `OK` 우선순위, `SETTLE_FAIL` 뒤 다음 주행 | §2.5 조회 명령은 중복 기록 없이 매번 실행. §4.3 반력 정체 오류는 `FAULT` 우선. `SETTLE_FAIL` 뒤 정지 확인 전 `DRIVE_STEP`은 `NOT_SETTLED` |

### 0.4 4차 회신 반영 (10/3)

한건민 동의(10/3): 조건부 헤드 유지, 중단 상태 구분, 로드셀 신규 변환만 집계, VAC2 32/37·WT901C 34/35 배정, `tel`/`mot` 분할 방향, 상단 리미트와 후륜 비상정지를 미검증으로 두는 것. I²C는 10/3 회신에서 공유로 정했다가, 같은 날 **별도 버스로 확정**했다(7번 아래 "4차 보완").

| # | 한건민 지적 | 반영 |
|---|---|---|
| 1 | 핀 이동 묶음에 `test/test.ino`가 빠짐. VAC2 RUN 34, EN 35가 직접 정의돼 있어 옛 배선으로 동작함 | §1 핀 이동 묶음을 main 1f19e1c 전수 검색으로 다시 씀. `test/test.ino`(상수 4개 줄과 `PINS` 출력 문자열), `teensy/pinmap.h` 주석, `docs/hardware_mapping.md` 7곳까지 6개 파일 |
| 2 | 동작 기록 16개를 넘기거나 30,000회 만료 뒤 옛 seq가 오면 새 명령으로 실행될 수 있음. 세션·증가 번호 규칙 필요, 진행 중 기록은 만료·교체 제외 | §2.5를 다시 씀. 동작 명령에 **`bid`(부팅 세션)와 `aid`(부팅 안에서 늘어나기만 하는 32비트 번호)**를 싣는다. 가장 큰 `aid`보다 작은 옛 요청은 기록에 없어도 `STALE_SEQ`로 거부한다. 16개 기록과 수신 번호 만료는 없앤다. 진행 중 기록은 새 동작이 **수락**될 때만 바뀐다 |
| 3 | `GET_INFO.last_seq`가 `GET_INFO` 자체나 재전송으로 바뀌는지 불명확. 65535→1 순환 시험 | §2.4 `last_seq`를 "처음 평가되어 기록된 비조회 명령의 seq"로 정의. 조회, 재전송, 기록하지 않는 거부는 바꾸지 않는다. `last_aid` 추가. §8에 응답 유실 재요청과 순환 시험 |
| 4 | `DISARM`은 최종 `IDLE`이어야 하는데 §4.5 표를 따르면 `ARMED`·`HOMED`에 남음. 거리 미달은 `SHORT`인데 심각도 2 `ABORTED`가 먼저 | §4.5에 `DISARM` 우선 규칙(최종 `IDLE`, 원점·영점·유지 모두 무효). §4.3 우선순위에서 `2-TU-004`는 `ABORTED`가 아니라 `SHORT`로 명시 |
| 5 | `tel`/`mot` 동기화, 메시지별 최신성, 재부팅·생략 시 이전 값 폐기, 길이 생성 시험, `faults_more` 뒤 활성 오류 재조회, 위험 오류 우선 | §3을 다시 씀. 공통 번호 `tn`, `mot`는 조건 없이 매 주기 송신, 메시지별 최신성, `boot_id` 변화·필드 생략 시 폐기. 숫자 형식과 범위를 고정해 최악값 줄을 만들어 잼(`tel` 462, `mot` 423 byte). 새 조회 명령 `GET_FAULTS`, `faults`는 심각도 순 |
| 6 | `VACUUM_HOLD`에서 `HOME`은 조건부 동의. 헤드만 복귀, 음압·상태 유지, 시작 시 압박 접촉 영점 무효화, 이후 `SET_CP_POINT` 성공 전 압박 시작 거부, 상단 리미트 검증 전 거부 | §2.2 `HOME`, `START_CPR`·`TRIAL_COMPRESSION` 행, §4.5. 거부 사유 `CP_ZERO_UNSET` 추가 |
| 7 | I²C 공유를 설계안으로 선택(MAX30102 0x57, NAU7802 0x2A, 기본 `Wire` 18/19), `A9`는 예비. 풀업 전압·병렬 저항, 동시 취득률, 버스 장애 시 제어 지연은 실물 검증 전 | **4차 보완(10/3 같은 날)**: 한건민이 통신 간섭을 피하려고 **별도 버스로 확정**했다(만능기판 배치도 10/2와 같음). MAX30102는 `Wire` 18/19, NAU7802는 `Wire1` SDA 17/SCL 16, 이 때문에 주행 엔코더 #1을 16/17에서 11/12로 옮긴다. §1 I²C 행과 핀 이동 대상 표, §8, §10 반영. 로드셀을 `A9` 아날로그 입력으로 다루지 않는 것은 그대로 |

**4차 작성 중 main 1f19e1c 코드에서 본 것**(`teensy/`는 한건민 영역이라 고치지 않고 보고만 한다)

| 위치 | 내용 | 관련 |
|---|---|---|
| `teensy/saver_teensy.ino` 340행 `DISARM` | 상태 검사 없이 `motorsOff()` 뒤 `ST_IDLE`로 둔다. `FAULT`에서 받으면 `RESET_FAULT`의 원인 확인을 거치지 않고 `IDLE`이 된다 | §2.2 `DISARM` 행 |
| `teensy/saver_teensy.ino` `sendAck`·`sendTel` 등 | `Serial.println`은 CRLF를 보낸다. v1.0 §3은 LF만 쓴다. Jetson은 `strip()`으로 받고 있어 지금은 동작한다 | §3.4 길이 정의 |
| `teensy/saver_proto.h` `buildTel` | 실수를 `%.2f`로 범위 제한 없이 쓴다. 센서값이 크게 튀면 줄 길이가 한도를 넘을 수 있다 | §3.4 숫자 범위 |

확답 대기(설치·검증 완료로 표시하지 않음): 상단 리미트 설치 담당과 일정, 후륜 비상정지의 실제 차단 범위.

### 0.5 5차 회신 반영 (10/4)

한건민 동의(10/4): `VACUUM_HOLD`의 조건부 `HOME`, `SHORT`와 `ABORTED` 구분, `tel`/`mot` 분할과 §3.3·§3.4 규칙, 상단 리미트와 후륜 비상정지를 미검증으로 두는 것, 상단 리미트 설치·검증 전 `HOME`·`HEAD_HOLD`·`DRIVE_STEP` 거부. **아래 `DISARM` 처리와 `bid`·`aid` 범위를 반영하면 `protocol/` v1.1에 반영하기로 했다.**

| # | 한건민 회신 | 반영 |
|---|---|---|
| 1 | 핀·I²C 최종안은 58b90f7의 별도 버스. MAX30102 `Wire` 18/19, NAU7802 `Wire1` 17/16, 엔코더 #1 11/12, #2 20/21, VAC2 RUN/EN 32/37, WT901C `Serial8` RX 34/TX 35, `A9` 예비. 한건민 로컬에 반영(`test/test.ino` 상수와 `PINS` 출력 포함), 커밋 전 | §1 표와 같음을 확인. 핀 이동 묶음 상태를 "로컬 반영, 커밋 전"으로 바꿈 |
| 2 | `DISARM → 항상 IDLE`은 상태와 하드웨어가 어긋난다(`motorsOff()`는 음압 유지). 음압 없는 `ARMED`/`HOMED`는 `IDLE`. 음압 유지 중이면 압박·주행 출력 차단, 음압 유지, `VACUUM_HOLD`, 원점·영점·유지 무효, 재가동 금지, `HOME`·`VACUUM_OFF`·조회만 허용, 복구는 `VACUUM_OFF → IDLE → ARM → HOME`. `FAULT`/ESTOP은 상태를 바꾸지 않고 `RESET_FAULT`·E-stop 해제 절차를 우회하지 않음. 음압 해제는 `VACUUM_OFF`나 수동 조작으로만. 한성윤 확인 필요 표시 유지 | §2.2 `DISARM` 행과 §4.5를 다시 씀. 재가동 금지는 상태 밖 플래그 `rearm_lock`으로 두고 `tel`에 싣는다(§3.1). 아래 확인 요청 1 |
| 3 | `bid`·`aid`를 조회 4개(`PING`, `GET_STATUS`, `GET_INFO`, `GET_FAULTS`)를 뺀 모든 상태·출력·설정 변경 명령으로 넓힘. `aid`가 4,294,967,295에 이르면 순환하지 않고 새 명령을 거부, 재부팅으로 새 `boot_id` | §2.5를 다시 씀. 즉시 변경 명령의 기록 칸 `cmd_last`를 따로 둬서 진행 중인 동작 기록을 밀어내지 않게 함. `aid` 소진은 `AID_EXHAUSTED`. 일반 중복 기록 8개(v1.0 §4.1)는 v1.1 펌웨어에서 쓰지 않음. 아래 확인 요청 2 |
| 4 | `tel`/`mot` 같은 `tn`·`boot_id`·`ms`, `mot` 매 주기, 생략 필드 재사용 금지, 메시지별 최신성, 본문 511 + LF, 범위 밖·NaN·Infinity 금지, 잘린 JSON 송신 금지. 길이는 실제 생성 함수와 Jetson 파서로 시험해야 구현 완료 | §3.3·§3.4 유지. §3.4에 완료 기준을 적음. `rearm_lock`을 더해 다시 잼(`tel` 481 byte, 여유 30) |
| 5 | main 코드 3건 확인. v1.1에서 `FAULT`/ESTOP을 `DISARM`으로 풀지 않음, `Serial.write()`로 LF 하나만, 모든 생성 함수에서 길이·범위·유한값·`-0.00` 검사, 버퍼 초과 시 잘린 줄 대신 송신 실패, 위험 오류는 출력 차단 뒤 보고 | §10-11을 "수정 계획 합의"로 바꿈 |

**한건민 확인 요청 2건** (정해지면 `protocol/`에 반영)

1. **`DISARM` 뒤 `VACUUM_HOLD`에서 `HOME`의 범위.** 회신은 `HOME`을 허용하면서 복구는 `VACUUM_OFF → IDLE → ARM → HOME`이라고 했다. 이 초안은 이렇게 읽었다.
   - `HOME`은 **헤드를 환자에게서 떼는 복귀로만** 허용한다(§2.2 "`VACUUM_HOLD`의 `HOME`" 규칙 그대로). `rearm_lock`은 풀지 않는다.
   - `rearm_lock`이 켜져 있으면 `SET_CP_POINT`, `SET_TARGET`, `TRIAL_COMPRESSION`, `START_CPR`, `RESUME_CPR`, `HEAD_HOLD`, `DRIVE_STEP`, `STIFFNESS_SCAN`을 `REARM_LOCKED`로 거부한다. 그래서 `HOME` 뒤 `SET_CP_POINT`로 다시 압박에 들어가는 길이 없다.
   - `rearm_lock`은 `VACUUM_OFF`로 `IDLE`이 될 때만 풀린다.
   - "음압 유지 중"은 상태명이 아니라 **음압 출력이 켜져 있는지**로 판단한다. `VACUUM_ON` 직후 아직 목표에 이르지 않았어도 블로워가 돌고 있으면 음압 유지 경로로 가고, 상태는 `VACUUM_HOLD`로 둔다. 실제 음압은 `vac_kPa`로 본다.
2. **정지 방향 명령의 예외.** `DRIVE_STOP`, `STOP_CPR`, `PAUSE_CPR`, `DISARM`도 `bid`·`aid`를 싣지만, `AID_REQUIRED`·`BOOT_MISMATCH`·`STALE_SEQ`·`AID_EXHAUSTED`로는 거부하지 않고 실행한다(상태 검사는 그대로).
   - 이유: 기록 문제로 정지를 막으면 위험하다. 옛 정지 요청을 다시 실행해도 장치는 안전한 쪽으로만 움직인다. 예를 들어 AED 충격 직전에 누른 `PAUSE_CPR`은 Jetson 재시작 직후라 `bid`가 틀려도 압박을 멈춰야 한다.
   - 반대로 `RESUME_CPR`, `START_CPR`, `RESET_FAULT`, `VACUUM_OFF`는 옛 요청이 실행되면 위험하므로(충격 중 압박 재개, 새 고장 해제, 압박 중 흡착 해제) 예외 없이 거부한다.
   - 같은 `aid` 재수신은 다시 실행하지 않고 원래 ACK를 보낸다. 유효하고 더 큰 `aid`면 `aid_max`를 올린다.

**Jetson 쪽 후속**(신상현, `protocol/` 반영 뒤)
- `S13` 진입 때 보내는 `DISARM`의 응답이 `VACUUM_HOLD`이고 `rearm_lock = true`면 UI가 "음압 유지 중"과 해제 절차(`S12`)를 안내한다. 음압이 남아 있으면 `S2`로 돌아가지 않는다. `state_machine_v2.json`에 조건을 넣어야 하므로 이번 v1.1 묶음에 포함한다.
- 조회를 뺀 모든 명령에 `bid`·`aid`. `GET_INFO` 뒤 seq·`aid`를 이어 간다. v1.0 펌웨어(`GET_INFO` 없음)와 연결할 때는 시작 seq를 무작위로 둔다(5f17985, Teensy 별도 전원 결정 반영).
- Jetson `decode`의 본문 한도를 511 byte로 맞춘다(지금은 512).
- 생략·stale 필드를 모의값이나 이전 값으로 채우지 않는 UI 규칙은 1639af7로 반영했다.

---

## 1. 하드웨어 전제와 미검증 항목

| 항목 | 현재 상태 (한건민 10/1) | 조치 | 기한 |
|---|---|---|---|
| 후륜 엔코더 | A/B 엔코더. 장착·배선·펄스 수·mm 환산 미검증 | 좌·우 각각, 정·역방향으로 외부 자 실측과 비교해 환산(§7). 핀은 아래 "핀 정합" 참조 | 10/7 |
| **후륜 비상정지** | **후륜 전원을 독립적으로 차단하는 회로 없음.** 검증 완료로 표시하지 않음 | 비상정지 회로가 후륜 모터 전원도 끊도록 배선한다(불변식 3). 물리 E-stop의 실제 차단 범위는 한건민이 확인. **확인 전에는 바닥 주행을 하지 않는다** | 10/7 사전 점검 전 |
| 제동 | 주행 PWM 0과 EN LOW로 출력만 끔. 검증 완료로 표시하지 않음 | 드라이버 단락 제동 지원 여부 확인. 10/7에 정지 거리와 헤드 접촉 중 바퀴 밀림을 함께 측정 | 10/7 |
| **상단 리미트** | **설치 확인 안 됨** | HOME은 상단 리미트 기준이다. 설치·검증 전에는 설정 `limit_top_ok = 0`으로 두고 HOME을 `HOME_UNAVAILABLE`로 거부한다. 그동안 `HEAD_HOLD`, `DRIVE_STEP`도 `HOMED`가 아니므로 실행되지 않는다. 헤드 단독 점검은 한건민 시험 코드로 한다 | 설치·검증 담당과 기한 확인 필요 |
| 자이로 | WT901C-TTL. 실물 수신, 설치 방향, 자기장 영향 미확인 | **Serial8 (RX8 = 34, TX8 = 35)**. 아래 "핀 이동 묶음"과 함께 바꾼다. 방향값은 기준각을 빼는 것만으로 자기장 영향이 없어졌다고 보지 않는다. 센서 출력 모드(지자기 사용 여부, 6축 모드 지원)를 확인하고 모터 작동 중 방향 오차를 측정한다 | 핀 10/5, 오차 10/7 |
| 로드셀 | DYMH-103 + NAU7802. 지원 속도 최대 320 SPS | 500 Hz 제어·기록 루프와 ADC 신규 변환을 구분한다(§4.3) | — |
| **I²C 별도 버스**(한건민 10/3 확정) | **MAX30102(0x57)는 `Wire` SDA 18/SCL 19, NAU7802(0x2A)는 `Wire1` SDA 17/SCL 16.** 두 버스의 SDA·SCL을 묶지 않는다. 주행 엔코더 #1은 16/17에서 **11/12**로 옮긴다. 11/12를 쓰므로 기본 SPI는 쓸 수 없다(내장 microSD는 SDIO라 영향 없음). `A9`는 **예비**로 두고 로드셀을 `A9` 아날로그 입력으로 다루지 않는다 | 실물 검증 전: 각 버스의 풀업 전압과 병렬 저항, NAU7802 신규 변환 취득률, 버스 장애 때 제어 지연. 버스를 나눠도 MCU와 전원은 공통이다. 로드셀 신규 변환이 끊기면 `3-TU-009`로 처리한다(§6) | 10/7 |
| **핀맵 기준** | main 1f19e1c: 홀 센서 **39번**, 32·37번 **예비**(음압 FG 제거로 해제), 후륜 엔코더 16/17, 20/21, 음압 모터 2는 VSP 33, RUN 34, EN 35, ERR 36 | 이 초안의 핀 번호는 main 기준이다. 2차 초안의 "37번 = 홀 센서" 서술은 이전 핀맵을 본 것이라 철회한다 | — |
| **핀 이동 묶음**(한건민 로컬 반영, 커밋 전. 10/4 확인) | 아래를 **한 커밋으로** 바꾼다. 하나라도 빠지면 그 코드는 옛 배선으로 동작하거나 35번이 음압 EN과 자이로 TX로, 16/17이 엔코더와 `Wire1`로 겹친다. 목록은 main 1f19e1c에서 `VAC2`, `M2_EN`, `M2_RUN`, `Serial8`, `ENCODER`, `Wire1`, 핀 번호 11·12·16·17·32·34·35·37을 전수 검색해 만들었다(`teensy/reference/`의 ESP32 옛 코드는 제외) | 아래 "핀 이동 대상 파일" 표. 바꾸는 동안 24 V와 USB를 끈다 | 10/5 (한건민) |
| **출력 차단 뒤 헤드 유지** | 압박 축 출력을 끊었을 때 헤드가 기계적으로 그 자리에 머무는지(볼스크류 역구동) 확인 안 됨 | 헤드를 모사체에 `press_mm` 누른 상태에서 출력을 끊고 리니어 센서로 처짐·밀림을 잰다. 결과가 나오기 전에는 "출력 차단 = 위치 유지"로 가정하지 않는다 | 10/7 |
| 주행 부호 | `dist_mm > 0` 장치 전방, `< 0` 후방 | 합의. 전방과 모사체 머리 방향의 대응은 설치 절차(UI 안내)로 고정하고 Jetson 설정 `scan_dir_sign`으로 맞춘다 | — |
| 설정값 | 미확정 | §7 순서대로 10/6 벤치값, 10/7 검증 | 10/6, 10/7 |

**핀 이동 대상 파일** (main 1f19e1c 기준, 한 커밋)

| # | 파일 | 바꿀 곳 |
|---|---|---|
| 1 | `teensy/pinmap.h` | `PIN_VAC2_RUN` 34 → **32**, `PIN_VAC2_EN` 35 → **37**. WT901C `Serial8`(RX8 **34**, TX8 **35**) 추가. `PIN_DRIVE1_ENCODER_A` 16 → **11**, `_B` 17 → **12**(83·84행). NAU7802 `Wire1`(SDA **17**, SCL **16**) 추가. 머리 주석의 "Serial8, Wire1 … consumed as GPIO", "Pins 11/12/13/32/37 remain physically free"(→ 13만 남음)와 끝부분 32·37 미배정 주석 |
| 2 | `test/test.ino` | 53~56행 `PIN_VAC2_EN` 35 → **37**, `PIN_VAC2_RUN` 34 → **32**. 93행 `PINS,...,M2_EN=35,...,M2_RUN=34` 출력 문자열도 같이 |
| 3 | `test/tests/check_combined.ps1` | 고칠 곳 없음. `test/test.ino`와 `teensy/pinmap.h`의 상수를 대조하므로, 1·2를 같이 바꾸지 않으면 이 검사가 실패한다 |
| 4 | `test/max30102_sensor_test/pinmap.h` | `PIN_VAC2_EN_GUARD` 35 → **37**(19행) |
| 5 | `docs/hardware_mapping.md` | 6행 상태 문단(32/37 해제), 46·47행 음압 모터 2 RUN·EN, **68·69행 구동 모터 1 엔코더 16/17 → 11/12**, 79·80행 32·37 예비, 82행 16/17 설명, 109행 "Serial8, Wire1 기본 핀은 다른 신호가 사용", 112행 미배정 핀 목록(→ 13), **311·312행 엔코더 배선 색상표(Teensy 16/17 → 11/12)**. I²C 별도 버스와 `A9` 예비도 적는다 |
| 7 | NAU7802 시험 코드(한건민 로컬, main에 없음) | 기본 `Wire` 18/19 → **`Wire1` 17/16** |
| 6 | `teensy/saver_teensy.ino` | 상수 이름만 쓰므로 번호는 고칠 곳 없음. WT901C 수신 코드를 넣을 때 `Serial8`을 쓴다 |

---

## 2. 명령

### 2.1 새 명령

조회 명령 4개(`PING`, `GET_STATUS`, `GET_INFO`, `GET_FAULTS`)를 뺀 **모든 명령**은 공통 파라미터 **`bid`**(대상 부팅 세션, `GET_INFO`의 `boot_id`)와 **`aid`**(명령 번호, 1 이상 32비트 정수)를 싣는다(5차 확대). 규칙은 §2.5다.

```json
{"v":1,"t":"cmd","seq":1208,"cmd":"DRIVE_STEP","bid":48213,"aid":37,"dist_mm":10.00}
```

| 명령 | 파라미터 | 허용 조건 | 동작 | 완료 보고 |
|---|---|---|---|---|
| `DRIVE_STEP` | `dist_mm`(0 아님, 절댓값 ≤ `drv_step_max_mm`), `speed_mm_s`(선택, ≤ `drv_speed_max_mm_s`, 생략 시 `drv_speed_mm_s`) | `HOMED`, `busy = NONE`, 헤드가 HOME 위치이거나 `head_hold_active`, **주행 정지 확인됨(`drv_settled`)**, 원인이 남은 TU 오류 없음, §7 주행 설정값 모두 설정됨 | 엔코더 기준 주행 → 출력 차단 → 정지 확인 → 반력 평균(§4.3) | `evt DRIVE_DONE` |
| `DRIVE_STOP` | — | **모든 상태** | 진행 중인 동작 명령(§2.5)을 멈춘다. 바퀴는 출력 차단(제동 가능하면 제동). 접촉 탐색이나 HOME 이동 중인 헤드는 하강·상승 목표를 **취소**하고 그 자리에서 멈춘다. 멈춘 자리의 유지는 §6 `2-TU-002`의 유지 조건을 모두 만족할 때만 하고, 아니면 출력을 차단한다. 이 임시 정지는 **헤드 유지(`head_hold_active`)가 아니다.** 중단 뒤 상태는 §4.5. **이미 성립한 헤드 유지는 풀지 않는다.** 압박(CPR) 동작과 음압은 바꾸지 않는다(CPR은 `STOP_CPR`). **FAULT·ESTOP을 해제하거나 출력을 다시 켜지 않는다** | 자기 ACK. 중단된 명령은 **그 명령의 원래 seq·`aid`**로 완료 이벤트(`result = STOPPED`) |
| `HEAD_HOLD` | `press_mm`(> 0, ≤ `head_press_max_mm`), `drv_zero`(선택, 기본 `false`) | `HOMED`, `busy = NONE`, **헤드가 HOME 위치**(상단 리미트 검출), `head_hold_active = false`, 시작 시 접촉 없음(§2.3), 원인이 남은 TU 오류 없음, `scan_force_max_N`·`contact_force_N` 등 §7 헤드 설정값 설정됨 | §2.3 절차로 접촉점을 찾고 `press_mm`만큼 더 내려 **리니어 센서 기준 위치 유지**. 반력은 계측과 상한 감시에만 쓴다(힘 제어 아님). `drv_zero = true`이고 성공하면 주행 원점을 0으로 둔다(§5) | `evt HEAD_HELD` |

**거부 사유(`ack ok:false`, `why`)**

| `why` | 뜻 |
|---|---|
| `NOT_HOMED` | `HOMED`가 아님 |
| `BUSY` | 다른 동작 명령 실행 중(§2.5) |
| `HEAD_NOT_READY` | 헤드가 HOME 위치도 아니고 유지 중도 아님 |
| `DIST_RANGE`, `PRESS_RANGE`, `SPEED_RANGE` | 파라미터 범위 밖 |
| `LIMIT_UNSET` | 필요한 설정값 없음(`2-TU-008`) |
| `FAULT_ACTIVE` | 원인이 남아 있는 오류가 있음(§6). `err`에 그 오류 코드 |
| `STALE_SEQ` | 이 부팅에서 이미 평가한 가장 큰 `aid`보다 작은 `aid`. 기록에 남아 있지 않은 옛 요청도 거부한다(§2.5) |
| `BOOT_MISMATCH` | `bid`가 현재 `boot_id`와 다름. 재부팅 전에 보낸 요청이다 |
| `AID_REQUIRED` | 조회가 아닌 명령에 `bid`나 `aid`가 없음. 정지 방향 명령은 이 사유로 거부하지 않는다(§2.5) |
| `AID_EXHAUSTED` | `aid_max`가 4,294,967,295에 이름. 순환하지 않으므로 재부팅으로 새 `boot_id`를 받을 때까지 새 명령을 거부한다. 정지 방향 명령은 실행한다 |
| `REARM_LOCKED` | `rearm_lock`이 켜져 있음(음압 유지 중 `DISARM` 뒤). `VACUUM_OFF`로 `IDLE`이 될 때까지 재가동 명령을 거부한다(§2.2) |
| `CP_ZERO_UNSET` | 압박 접촉 영점이 없음. 마지막 `HOME` 뒤 `SET_CP_POINT`가 성공하지 않았다(§2.2) |
| `HOME_UNAVAILABLE` | 상단 리미트 미설치·미검증(`limit_top_ok = 0`) |
| `ALREADY_HOLDING` | 헤드 유지 중에 새 `aid`의 `HEAD_HOLD`. 현재 눌린 위치를 새 접촉점으로 잡아 더 내려가는 것을 막는다. 다시 잡으려면 `HOME` 뒤 보낸다 |
| `IN_CONTACT` | 접촉 탐색 시작 시점에 이미 접촉 판정 힘 이상(§2.3) |
| `NOT_SETTLED` | 앞선 주행이 `SETTLE_FAIL`로 끝난 뒤 아직 정지가 확인되지 않음(§4.3) |
| `SEQ_CONFLICT` | 기록된 seq와 같은데 명령이나 파라미터가 다름(§2.5). 실행하지 않는다 |

오류 코드는 기존 `2-CU-011` 체계를 쓰고, 설정값 없음은 `2-TU-008`로 한다.

### 2.2 기존 명령 변경

| 명령 | v1.0 | v1.1 |
|---|---|---|
| `HOME` | `ARMED`에서만 | `ARMED`, `HOMED`에서 `busy = NONE`일 때. 완료 조건은 **상단 리미트 검출**이다. `limit_top_ok = 0`이면 `HOME_UNAVAILABLE`. 헤드 유지 중이면 유지를 끝내고 복귀한다. `HOMING_DONE`에 seq, `aid`, `result`를 싣는다(§4.1). **`VACUUM_HOLD`에서도 허용**(한건민 10/3 조건부 동의). 조건은 아래 "`VACUUM_HOLD`의 `HOME`" |
| `START_CPR`, `TRIAL_COMPRESSION` | `CPR_READY` | 변경 없음에 더해, 압박 접촉 영점이 없으면 `CP_ZERO_UNSET`으로 거부한다. 상태 전이가 이미 막지만 Teensy가 한 번 더 확인한다 |
| `SET_CP_POINT` | `idx` 또는 `offset_mm` | 탐색 후 고정에서는 `VACUUM_HOLD`에서 `offset_mm = 0`을 보낸다. 뜻은 "현재 위치가 압박점"이다. Teensy는 **현재 위치를 접촉점으로 간주하지 않고** §2.3 절차로 헤드를 내려 **압박 접촉 영점**을 새로 잡는다. 헤드가 HOME 위치가 아니면 `HEAD_NOT_READY`, 시작 시 접촉 중이면 `IN_CONTACT`. 실패하면 `2-TU-006`. `CP_ZERO_SET`에 seq와 `result`를 싣는다 |
| `RESET_FAULT` | 원인 해소 확인 후 `IDLE` | 변경 없음. 재시작은 **`IDLE → ARM → HOME → 새 탐색(HEAD_HOLD drv_zero:true)`**이다. 오류 전의 탐색 좌표는 버린다 |
| `DISARM` | `ARMED` 이후. 모터 차단, `IDLE` | 동작 명령 실행 중에도 허용하고 진행 중인 명령은 `result = STOPPED`로 끝낸다. 결과는 **음압 출력에 따라** 나뉜다(한건민 10/4). **음압 출력 꺼짐**(`ARMED`·`HOMED`): 모든 출력 차단, `IDLE`. **음압 출력 켜짐**(`VACUUM_ON` 이후, `VACUUM_HOLD`·`CPR_READY`·`CPR_RUNNING`·`CPR_PAUSED` 포함): 압박 축과 주행 출력 차단, **음압 유지**, 상태 `VACUUM_HOLD`, 기구 HOME 원점·압박 접촉 영점·주행 원점·헤드 유지 무효, **`rearm_lock = true`**. 이때 허용은 `HOME`(헤드 복귀만), `VACUUM_OFF`, 정지 방향 명령, 조회뿐이고 나머지는 `REARM_LOCKED`. 복구는 `VACUUM_OFF → IDLE → ARM → HOME`. **`FAULT`·ESTOP**: 출력 차단 유지, 상태 불변(`FAULT_ACTIVE`·`ESTOP_ACTIVE`), `RESET_FAULT`나 물리 E-stop 해제 절차를 우회하지 않는다. 음압 해제는 `DISARM`에 묶지 않고 `VACUUM_OFF`나 수동 조작으로만 한다 — **한성윤 확인 필요** |
| `STIFFNESS_SCAN` | 고정 후 탐색 | 변경 없음. 결선 C안에서는 쓰지 않는다 |
| ESTOP 중 허용 명령 | `PING`, `GET_STATUS` | **`GET_INFO`, `GET_FAULTS`, `DRIVE_STOP` 추가**(`DRIVE_STOP`은 ACK만, 출력 상태 변화 없음) |

**`VACUUM_HOLD`의 `HOME`** (한건민 10/3 조건)

1. `VACUUM_HOLD`에서만 허용한다. `CPR_READY`, `CPR_RUNNING`, `CPR_PAUSED`에서는 v1.0대로 거부한다.
2. `limit_top_ok = 0`이면 `HOME_UNAVAILABLE`로 거부한다. 상단 리미트 설치·검증 전에는 이 경로도 쓰지 않는다.
3. **헤드만** 상단 리미트까지 올린다. 음압 출력과 `VACUUM_HOLD` 상태는 시작부터 끝까지 그대로다. 실패·중단해도 같다(§4.5).
4. 복귀를 **시작하는 순간** 압박 접촉 영점을 무효로 한다. 복귀가 끝나지 않아도 영점은 되살리지 않는다.
5. 그 뒤 `SET_CP_POINT`가 성공할 때까지 `START_CPR`, `TRIAL_COMPRESSION`, 압박을 시작하는 모든 경로를 `CP_ZERO_UNSET`으로 거부한다.
6. 넣은 이유: 중단된 `SET_CP_POINT` 뒤 헤드를 빼는 경로가 없다(§4.5).

### 2.3 접촉 확인 절차 (HEAD_HOLD, SET_CP_POINT 공통)

0. **시작 조건**: 헤드가 HOME 위치이고, 로드셀 신규 변환 샘플로 본 `cp_force_N`이 `contact_force_N` 미만이어야 한다. 아니면 움직이지 않고 `IN_CONTACT`로 거부한다. 이미 눌린 자리를 접촉점으로 잡는 것을 막는다.
1. 헤드를 `head_search_speed_mm_s`로 내린다.
2. 로드셀 **신규 변환 샘플**로 본 `cp_force_N ≥ contact_force_N`이 `contact_confirm_ms` 동안 이어지면 접촉으로 본다. 그 위치를 `head_contact_mm`로 기록한다.
3. `head_search_max_mm`까지 내려도 접촉이 없으면 멈추고 `2-TU-006`을 보낸다.
   - 복귀는 **리니어 센서가 유효하고 복귀 경로에 리미트 이탈이 없을 때만** HOME 위치로 한다.
   - 그렇지 않으면 그 자리에서 출력을 차단하고 사용자 확인을 기다린다.
4. `HEAD_HOLD`는 접촉점에서 `press_mm` 더 내려 위치 유지에 들어간다. `SET_CP_POINT`는 접촉점을 압박 영점으로 둔다.
5. 이 과정 내내 다음을 감시한다.
   - `cp_force_N > scan_force_max_N` → `3-TU-003`
   - 로드셀 신규 변환이 `force_stale_ms` 넘게 없음 → `3-TU-009`
   - 리니어 센서 상실·범위 이탈 → `3-TU-010`

**`press_mm` 기준**: 최초 접촉점(`head_contact_mm`)에서 잰 추가 하강량이다. 기구 원점 기준이 아니다.
**`press_mm = 0`**: 허용하지 않는다(`PRESS_RANGE`). 접촉만 하는 동작은 `SET_CP_POINT`가 맡는다.
**유지 중 재요청**: `head_hold_active`인 동안 새 `aid`의 `HEAD_HOLD`는 `ALREADY_HOLDING`으로 거부한다. 같은 seq·`aid`의 재전송은 §2.5대로 원래 ACK와 완료 이벤트만 다시 보낸다.
**유지 해제**: `HOME`(복귀)으로만 푼다. `DRIVE_STOP`은 유지를 풀지 않는다. FAULT·ESTOP으로 압박 축 출력이 차단되면 유지도 끝나고 `head_hold_active = false`가 된다.

### 2.4 `GET_INFO` (새 조회 명령)

모든 상태에서 허용한다. 응답은 ACK와 `evt INFO`다.

```json
{"v":1,"t":"evt","ms":184300,"evt":"INFO","seq":3,"fw":"1.1.0","proto":"1.1","caps":["DRIVE","HEAD_HOLD","CP_ZERO","MOT","AID","FAULTS"],"boot_id":48213,"last_seq":1207,"last_aid":36}
```

| 필드 | 뜻 |
|---|---|
| `fw` | 펌웨어 버전 문자열(16자 이하) |
| `proto` | 지원 프로토콜 판. 와이어 `v`는 1로 유지하되 명령 의미의 판을 따로 알린다 |
| `caps` | 지원 기능. `DRIVE`(DRIVE_STEP·DRIVE_STOP), `HEAD_HOLD`, `CP_ZERO`(v1.1 의미의 SET_CP_POINT·HOME), `MOT`(`mot` 메시지, §3.2), `AID`(§2.5 `bid`·`aid` 규칙), `FAULTS`(`GET_FAULTS`, §3.5) |
| `boot_id` | 부팅마다 바뀌는 값(EEPROM 부팅 횟수, 1~65535 순환). `tel`, `mot`에도 싣는다 |
| `last_seq` | 이 부팅에서 **처음 평가되어 기록 칸(`act_cur`, `act_rej`, `cmd_last`, §2.5)에 들어간 비조회 명령** 중 마지막 것의 seq. 수락·거부를 가리지 않는다. 기록이 없으면 생략한다 |
| `last_aid` | 이 부팅에서 평가한 가장 큰 `aid`(수락·거부 모두). 없으면 생략한다 |

**`last_seq`, `last_aid`를 바꾸지 않는 것**: 조회 명령(`GET_INFO` 자신 포함), 같은 seq·`aid`의 재전송(재실행 없이 ACK만 다시 보내는 경우), 기록하지 않는 거부(`STALE_SEQ`, `BOOT_MISMATCH`, `AID_REQUIRED`, `SEQ_CONFLICT`). 그래서 `INFO` 응답이 유실돼 `GET_INFO`를 다시 보내도, 그 사이 옛 재전송이 와도, 처음과 같은 값이 나온다.

**Jetson 규칙**: 연결·재연결 때마다(Jetson 프로그램 재시작 포함) 다른 명령보다 먼저 `GET_INFO`를 보낸다. `GET_INFO`는 조회 명령이라 seq가 무엇이든 새로 실행된다(§2.5). seq를 모르면 1을 쓴다. `UNKNOWN_COMMAND`를 받거나 `caps`에 기능이 없으면 그 기능의 명령과 UI 버튼을 끈다. v1.0 의미의 `HOME`·`SET_CP_POINT`로 v1.1 절차를 진행하지 않는다.

### 2.5 동작 명령 기록과 중복 처리

**명령 분류**(5차: `bid`·`aid`를 조회 외 모든 명령으로 확대)

| 분류 | 명령 | 중복·오래된 요청 처리 |
|---|---|---|
| 조회 | `PING`, `GET_STATUS`, `GET_INFO`, `GET_FAULTS` | **`bid`·`aid` 없음. 기록하지 않는다. 같은 seq가 다시 와도 매번 새로 실행하고 새 응답을 보낸다.** 상태를 바꾸지 않으므로 다시 실행해도 안전하다 |
| 동작(완료 이벤트 있음) | `HOME`, `HEAD_HOLD`, `DRIVE_STEP`, `SET_CP_POINT`, `TRIAL_COMPRESSION`, `STIFFNESS_SCAN` | `bid`·`aid`. 기록 칸 `act_cur`·`act_rej`. 한 번에 하나만 실행(`busy`) |
| 즉시 변경 | `ARM`, `SET_TARGET`, `VACUUM_ON`, `VACUUM_OFF`, `START_CPR`, `RESUME_CPR`, `RESET_FAULT`, `SET_TEL_RATE` | `bid`·`aid`. 기록 칸 `cmd_last` |
| 정지 방향(즉시 변경의 하위) | `DRIVE_STOP`, `STOP_CPR`, `PAUSE_CPR`, `DISARM` | `bid`·`aid`. 기록 칸 `cmd_last`. **기록 사유(`AID_REQUIRED`, `BOOT_MISMATCH`, `STALE_SEQ`, `AID_EXHAUSTED`)로 거부하지 않는다**(§0.5 확인 요청 2) |

v1.0의 일반 중복 기록(최근 seq 8개)은 v1.1 펌웨어에서 쓰지 않는다. 조회는 매번 실행하고, 나머지는 `aid`로 가린다.

동작 명령은 **한 번에 하나만** 실행한다. 실행 중 다른 동작 명령은 `BUSY`로 거부하고 큐에 쌓지 않는다.
실행 중에도 받는 명령은 정지 방향 명령 4개, 조회 4개, `SET_TEL_RATE`다.

**세션과 증가 번호**(한건민 10/3 지적 2)

| 필드 | 규칙 |
|---|---|
| `bid` | 동작 명령을 보낼 대상 부팅. `GET_INFO`·`tel`의 `boot_id`와 같아야 한다. 다르면 재부팅 전에 만든 요청이므로 실행하지 않는다 |
| `aid` | 이 부팅 안에서 Jetson이 동작 명령마다 하나씩 늘리는 번호(1~4,294,967,295, 순환 없음). Teensy는 평가한 가장 큰 `aid`를 `aid_max`로 기억한다. **`aid_max`보다 작은 `aid`는 기록에 있든 없든 새 명령으로 실행하지 않는다** |

seq는 v1.0대로 1~65535 순환이고 ACK 짝 맞추기에만 쓴다. 오래된 요청인지는 seq가 아니라 `aid`로 가린다. 2차 초안의 seq 순환 비교, 3차 초안의 16개 기록과 수신 번호 만료는 **없앤다.** `aid`는 순환하지 않으므로 만료가 필요 없다.

**기록 칸**: 세 칸을 둔다.

| 칸 | 내용 | 바뀌는 때 |
|---|---|---|
| `act_cur` | 수락된 가장 최근 동작. `aid`, seq, 명령명, 정규화한 파라미터, 원래 ACK, 완료 이벤트 전문(끝났을 때) | **새 동작이 수락될 때만.** 동작은 하나씩만 실행하므로 그때는 이전 동작이 이미 끝나 결과가 저장돼 있다. 즉시 변경 명령, 거부된 동작, 조회로는 바뀌지 않는다. 진행 중인 기록은 교체·삭제되지 않는다 |
| `act_rej` | 마지막으로 거부한 동작(`BUSY`, `NOT_HOMED` 등 상태 거부). `aid`, seq, 명령명, 파라미터, 원래 ACK | 새 동작이 거부될 때 |
| `cmd_last` | 마지막으로 평가한 즉시 변경 명령(수락·거부 모두). `aid`, seq, 명령명, 파라미터, 원래 ACK | 새 즉시 변경 명령을 평가할 때. 동작 실행 중 받은 정지 명령이나 `SET_TEL_RATE`가 `act_cur`를 밀어내지 않도록 따로 둔다 |

파라미터 정규화: 정수는 그대로, mm 값은 0.01 단위로 반올림, 생략한 선택 파라미터는 기본값으로 채운다.

**판단 순서**(조회가 아닌 모든 명령)

0. 정지 방향 명령이면 아래 1·2·5·7번 사유가 생겨도 **거부하지 않고** 6번처럼 평가·실행한다. 이때 `aid`가 유효하고 `aid_max`보다 크면 `aid_max`를 올리고, 아니면 `aid_max`와 기록 칸은 그대로 둔다.
1. `bid` 또는 `aid`가 없거나 `aid = 0`이면 `AID_REQUIRED`. 실행하지 않고 기록하지 않는다.
2. `bid ≠ boot_id`면 `BOOT_MISMATCH`. 실행하지 않고 기록하지 않는다.
3. `aid`가 `act_cur.aid` 또는 `cmd_last.aid`와 같으면 seq·명령명·정규화 파라미터를 대조한다.
   - 같으면 재실행하지 않는다. 진행 중이면 원래 ACK를, 끝났으면 원래 ACK와 저장된 완료 이벤트를 다시 보낸다(`cmd_last`는 원래 ACK만).
   - 다르면 `SEQ_CONFLICT`. 실행하지 않는다.
4. `aid = act_rej.aid`면 같은 방식으로 대조해, 같으면 **원래 거부 ACK를 다시 보낸다**(다시 평가하지 않는다). 다르면 `SEQ_CONFLICT`.
5. `aid < aid_max`면 `STALE_SEQ`. 실행하지 않고 기록하지 않는다.
6. `aid > aid_max`면 새 명령으로 평가한다. `aid_max = aid`로 둔다. 동작이면 수락 시 `act_cur`, 거부 시 `act_rej`에, 즉시 변경이면 `cmd_last`에 넣는다.
7. `aid_max = 4,294,967,295`에 이른 뒤의 새 `aid`는 `AID_EXHAUSTED`. 순환하지 않는다. Jetson은 사용자에게 재시작을 안내하고, Teensy는 재부팅 뒤 새 `boot_id`로 `aid_max`를 비운다.

같은 `aid`는 한 번만 평가된다. 3·4번은 `aid_max`보다 작아도 5번보다 먼저 본다. 그래서 그 뒤에 다른 동작이 거부됐더라도 진행 중이거나 마지막으로 끝난 동작의 결과는 다시 받을 수 있다(§4.4).

**Jetson 규칙**
- 연결하면(Jetson 프로그램 재시작 포함) 먼저 `GET_INFO`를 보낸다. `UNKNOWN_COMMAND`면 v1.0 펌웨어이므로 v1.0 규칙을 따르고, 이전 세션 seq와 겹치지 않게 시작 seq를 무작위로 둔다(5f17985).
  - 다음 seq는 `last_seq + 1`(65535 다음은 1)로 이어 간다. `last_seq`가 없으면 1부터 시작한다.
  - 다음 `aid`는 `last_aid + 1`로, `bid`는 `boot_id`로 둔다. `last_aid`가 없으면 1부터 시작한다.
- 한 연결 안에서 seq와 `aid`를 다시 쓰지 않는다. 재전송만 같은 seq·`aid`를 쓰고, 그때는 명령과 파라미터를 바꾸지 않는다.
- 재전송은 가장 최근 동작 명령 하나와 가장 최근 즉시 변경 명령 하나에만 한다. 새 동작 명령은 이전 동작의 완료 이벤트(또는 §4.4 복구)를 확인한 뒤에만 보낸다.
- ACK로 거부를 받은 동작은 재전송하지 않는다. 다시 하려면 새 seq와 새 `aid`로 보낸다.

**`busy` 유지 범위**: 명령을 수락한 순간부터 결과(반력 평균 포함)를 **`act_cur`에 저장한 순간**까지다. USB 송신 성공을 기다리지 않는다. 송신이 늦거나 실패해도 결과는 `act_cur`에서 다시 받을 수 있다.

**재부팅**
- 동작 기록과 `aid_max`는 RAM에 두고 부팅 때 비운다. `boot_id`가 바뀌므로 재부팅 전에 만든 동작 요청은 2번에서 `BOOT_MISMATCH`로 거부된다. 상태도 `IDLE`이다.
- Teensy는 부팅 뒤 어떤 동작도 자동으로 재실행하지 않는다.
- Jetson은 `boot_id`가 바뀐 것을 보면 진행 중이던 동작의 결과를 "불명"으로 처리하고 **이전 요청을 재전송하지 않는다.** `GET_INFO`로 새 `bid`를 받고, 탐색은 `ARM → HOME → 새 탐색`으로 처음부터 한다.

---

## 3. 텔레메트리: `tel` 추가 필드와 새 메시지 `mot` (측정할 수 없으면 생략)

**한 줄 512 byte 한도는 유지한다.** 4차에서는 `tel`에 v1.0 필드와 동기화 필드만 두고, 동작 상태(`busy` 등)는 매 주기 함께 보내는 `mot`로 옮긴다. 아래 길이는 §3.4 형식 규칙의 최댓값으로 실제 줄을 만들어 잰 값이다(본문, LF 제외).

| 메시지 | 최악값 길이 | 여유(511 기준) |
|---|---:|---:|
| v1.0 `tel`(`faults` 6개) | 411 byte | 100 |
| **5차 `tel`** = v1.0 + `tn`, `boot_id`, `faults_more`, `rearm_lock` | **481 byte** | 30 |
| **5차 `mot`**(4차와 같음) | **423 byte** | 88 |
| `DRIVE_DONE` | 225 byte | 286 |
| `HEAD_HELD`, `HOMING_DONE`, `CP_ZERO_SET` | 194 byte | 317 |
| `INFO` | 211 byte | 300 |
| `ack` | 104 byte | 407 |
| `err`(`why` 24자) | 107 byte | 404 |
| `FAULTS_END` | 74 byte | 437 |

3차의 계산값(`tel` 429·470, `mot` 292 byte)은 숫자 범위를 고정하지 않고 낸 값이라 위 표로 바꾼다. 3차 배치대로 상태 4개를 `tel`에 두면 525 byte로 한도를 넘는다.

### 3.1 `tel`에 더하는 필드 (20 Hz, 기존 주기)

| 필드 | 단위 | 설명 |
|---|---|---|
| `tn` | int | 텔레메트리 주기 번호. 부팅 때 1부터 주기마다 1씩 늘린다(0~4,294,967,295). **같은 주기의 `tel`과 `mot`는 같은 `tn`과 같은 `ms`를 싣는다** |
| `boot_id` | int | §2.4 |
| `faults_more` | bool | `faults`를 6개로 잘랐을 때만 `true`로 싣는다. 자르지 않았으면 생략 |
| `rearm_lock` | bool | 음압 유지 중 `DISARM` 뒤 재가동 금지(§2.2). **항상 싣는다.** `VACUUM_OFF`로 `IDLE`이 될 때 `false`. 재부팅 뒤 `false`(상태 `IDLE`) |

**`faults` 순서**: 심각도 3 → 2 → 1, 같은 심각도 안에서는 먼저 생긴 것부터. 6개를 넘으면 앞의 6개만 싣는다. 그래서 위험 오류가 먼저 보인다. 나머지는 `GET_FAULTS`로 받는다(§3.5).

3차에서 `tel`에 넣었던 `busy`, `busy_seq`, `head_hold_active`는 `mot`로 옮긴다.

### 3.2 새 메시지 `mot` (매 주기)

```json
{"v":1,"t":"mot","ms":184320,"tn":3687,"boot_id":48213,"busy":"NONE","head_hold_active":true,"drv_settled":true,"drv_pos_mm":40.10,"drv_yaw_deg":0.60,"head_pos_mm":41.20,"head_contact_mm":31.20,"mot_last_seq":1208,"mot_last_aid":37,"mot_last_cmd":"DRIVE_STEP","mot_last_result":"OK"}
```

- **보내는 때**: `caps`에 `MOT`가 있으면 **모든 상태에서 매 주기** `tel` 바로 뒤에 보낸다. 3차의 조건부 송신(동작 중일 때만)은 없앤다. 짝이 맞지 않는 주기가 생기지 않게 하려는 것이다. `GET_STATUS`를 받으면 같은 `tn`으로 `tel`과 `mot`를 한 번씩 즉시 보낸다.
- v1.0 Jetson은 모르는 `t`를 무시해야 한다. v1.1 Jetson은 `GET_INFO`의 `caps`로 `mot` 지원을 확인한다.

| 필드 | 단위 | 설명 |
|---|---|---|
| `tn`, `boot_id` | int | §3.1과 같다 |
| `busy` | string | `NONE` `DRIVE` `HOMING` `HEAD_SEARCH` `CP_ZERO`. 동작 명령 수락부터 결과 저장까지(§2.5). 접촉 탐색 중에는 `HEAD_SEARCH`라서 `head_hold_active = false`여도 다른 동작 명령은 거부된다 |
| `busy_seq`, `busy_aid` | int | 실행 중인 동작 명령의 seq와 `aid`. `busy = NONE`이면 생략 |
| `head_hold_active` | bool | 헤드 위치 유지 중. §4.5의 임시 정지는 `false` |
| `drv_settled` | bool | 주행이 정지 확인된 상태(§4.3). 엔코더가 무효면 생략 |
| `drv_pos_mm` | mm | 주행 원점(§5) 기준 누적 위치, 엔코더 두 바퀴 평균. **원점이 없거나 엔코더가 무효면 생략** |
| `drv_yaw_deg` | ° | 주행 원점에서의 방향 변화. IMU 무효면 생략 |
| `drv_pitch_deg`, `drv_roll_deg` | ° | 기울기. 신뢰할 수 없으면 생략 |
| `head_pos_mm` | mm | 기구 HOME 원점 기준 헤드 위치, 하강이 양수. HOME 전이거나 리니어 센서 무효면 생략 |
| `head_contact_mm` | mm | 마지막 접촉점(HOME 원점 기준) |
| `mot_last_seq`, `mot_last_aid` | int | `act_cur`의 seq와 `aid`(가장 최근에 수락된 동작). 끝나지 않았으면 생략 |
| `mot_last_cmd` | string | 그 명령명 |
| `mot_last_result` | string | 그 `result`(§4) |

1차의 `drv_busy`, `homing`, `drv_last_seq`, `drv_last_result`는 위 `busy`, `busy_seq`, `mot_last_*`로 합친다.

### 3.3 동기화와 최신성 (Jetson 규칙)

1. **짝**: `tel`과 `mot`는 `boot_id`와 `tn`이 같을 때만 같은 측정 시점으로 본다. 두 메시지의 값을 함께 쓰는 판단(예: `state`와 `busy`)은 같은 `tn` 짝에서만 한다. 짝이 없으면 그 판단을 다음 주기로 미룬다.
2. **메시지별 최신성**: `tel`과 `mot`의 마지막 수신 시각을 따로 잰다. 한도는 v1.0의 오래된 텔레메트리 한도와 같은 값을 쓴다.
   - `tel`이 오래됨: v1.0 규칙대로 안전 상태로 전이한다.
   - `mot`만 오래됨: `mot` 값을 모두 "불명"으로 표시하고 **새 동작 명령을 보내지 않는다.** 진행 중인 동작은 완료 이벤트와 §4.4로 판단한다.
3. **재부팅**: `tel`, `mot`, `evt` 어디서든 `boot_id`가 바뀌면 이전 `tel`·`mot` 값, 짝 대기, 주행 원점과 헤드 상태 표시를 모두 버린다(§2.5 재부팅).
4. **필드 생략**: 각 메시지는 그 주기의 전체 스냅숏이다. 생략된 필드는 "그 주기에 불명"이고, **이전 값을 이어 쓰지 않는다.**
5. **순서**: `tn`이 건너뛰면 기록만 한다. 같은 `boot_id`에서 `tn`이 줄어들면 순서가 뒤바뀐 것으로 보고 그 메시지를 버린다.
6. **판단 근거**: 동작 결과(이동 거리, 반력)는 완료 이벤트의 값을 쓴다. `mot`는 표시와 §4.4 복구에 쓴다.
7. `state = HOMED`만 보고 정지로 판단하지 않는다. 동작 종료는 `busy = NONE`과 완료 이벤트로, 헤드 상태는 `head_hold_active`로 판단한다.

### 3.4 줄 길이와 숫자 형식 (양쪽 생성 시험의 기준)

| 항목 | 규칙 |
|---|---|
| 길이 | **JSON 본문 ≤ 511 byte, LF 1 byte를 더해 ≤ 512 byte.** CR은 보내지 않는다(v1.0 §3). Teensy 수신 버퍼 512 byte(본문 511 + NUL), Jetson `encode`의 "본문 + 1 ≤ 512"와 같은 정의다 |
| 실수 | 소수점 아래 **정확히 2자리**. 범위 **−9999.99 ~ 9999.99**. 범위 밖이면 그 필드를 생략하고(측정 불가) 해당 센서의 오류 규칙을 따른다. `-0.00`은 `0.00`으로 쓴다. `NaN`, `Infinity` 금지(v1.0) |
| 정수 | 10진수, 앞자리 0 없음. `ms`, `tn`, `aid`는 0~4,294,967,295(10자). seq, `boot_id`, `force_n`, `n`은 0~65535(5자) |
| 불리언 | `true`, `false` |
| 문자열 | ASCII. 따옴표·역슬래시·제어문자 없음. 상태명, `busy`, `result`, 명령명은 이 문서의 열거값만(최장 16자). `why` 24자 이하, `fw` 16자 이하 |
| 배열 | `faults`, `caps`만. 오류 코드는 `s-UU-nnn` 8자 |
| 공백 | 없음 |
| 송신 초과 | 만든 줄이 511 byte를 넘으면 **잘린 JSON을 보내지 않는다.** 구현 결함이므로 아래 생성 시험에서 미리 막는다 |

**최대 길이 생성 시험**(양쪽): 위 규칙의 최댓값(음수 최대 자릿수, 정수 최댓값, 최장 문자열, `faults` 6개와 `faults_more`)으로 **모든 메시지 종류**를 만들어 본문이 511 byte 이하인지, 상대 쪽 파서가 받는지 확인한다. 위 길이 표가 그 결과이며, 계산 스크립트는 `docs/protocol_v1.1_worst_len.py`다(초안 검토용, 합의 뒤 양쪽 시험으로 옮긴다). Jetson은 같은 생성 함수로 만든 문자열을 `jetson/tests`에, Teensy는 `buildTel` 등 실제 생성 함수의 출력을 `test_saver_proto.cpp`에 넣는다.

**구현 완료 기준**(한건민 10/4): 위 계산 스크립트 결과는 초안 값이다. Teensy 실제 생성 함수의 최댓값 출력이 Jetson 파서(`protocol.decode`)를 통과하고 본문 511 byte 이하, 줄 끝 LF 하나임을 양쪽 시험이 확인해야 구현 완료로 본다.

### 3.5 `GET_FAULTS` (새 조회 명령)

모든 상태에서 허용한다(ESTOP 포함). 응답 순서는 다음과 같다.

1. ACK
2. 활성 오류마다 `err` 한 줄씩. 순서는 `faults`와 같다(심각도 3 → 2 → 1, 같은 심각도는 먼저 생긴 순).
3. `{"v":1,"t":"evt","ms":184400,"evt":"FAULTS_END","seq":1210,"n":8}`. `n`은 보낸 `err` 줄 수다.

조회 명령이라 기록하지 않는다. 응답이 유실되면 같은 seq로 다시 보내면 된다.

**Jetson 규칙**: 다음 경우에 `GET_FAULTS`를 보낸다.
- `faults_more: true`를 받았을 때
- `faults`에 있는 코드인데 그 `err`를 받지 못했을 때

`FAULTS_END`의 `n`과 받은 `err` 줄 수가 다르면 다시 요청한다. UI는 심각도 3을 맨 위에 둔다.

---

## 4. 이벤트

모든 동작 명령의 완료 이벤트는 **원래 명령의 seq와 `aid`**, `result`를 싣는다. evt에 seq·`aid`를 싣는 것은 v1.1에서 새로 생기는 규칙이다.

### 4.1 `HEAD_HELD`, `HOMING_DONE`, `CP_ZERO_SET`

```json
{"v":1,"t":"evt","ms":184100,"evt":"HEAD_HELD","seq":1201,"aid":31,"result":"OK","head_contact_mm":31.20,"head_pos_mm":41.20,"cp_force_N":18.40}
```

| `result` | 뜻 |
|---|---|
| `OK` | 성공 |
| `NO_CONTACT` | 접촉 실패(`2-TU-006`). HEAD_HELD, CP_ZERO_SET만 |
| `STOPPED` | `DRIVE_STOP` 또는 `DISARM`으로 중단 |
| `ABORTED` | 심각도 2 오류로 중단. `code`에 오류 코드 |
| `FAULT` | 심각도 3 오류로 정지. `err`를 먼저 보내고 `code`에 오류 코드 |

측정할 수 없는 필드(`head_contact_mm`, `head_pos_mm`, `cp_force_N`)는 생략한다.

### 4.2 `DRIVE_DONE`

```json
{"v":1,"t":"evt","ms":184320,"evt":"DRIVE_DONE","seq":1208,"aid":37,"result":"OK","drive_mm":9.96,"drv_pos_mm":40.10,"drv_yaw_deg":0.60,"cp_force_N":28.40,"force_n":40}
```

| `result` | 뜻 |
|---|---|
| `OK` | 목표 거리 ± `drv_pos_tol_mm` 도달, 정지 확인 완료 |
| `SETTLE_FAIL` | 출력은 껐으나 `drv_settle_max_ms` 안에 정지가 확인되지 않음(관성, 밀림) |
| `SHORT` | 엔코더는 유효하나 `drv_timeout_ms` 안에 거리 미달(막힘, 미끄러짐). `code`는 `2-TU-004`. 심각도 2지만 `ABORTED`로 올리지 않는다(§4.3) |
| `STOPPED` | `DRIVE_STOP` 또는 `DISARM`으로 중단 |
| `ABORTED` | 심각도 2 오류로 중단(방향 이탈, 기울기, IMU 끊김). `code`에 오류 코드 |
| `FAULT` | 심각도 3 오류로 정지(엔코더 무효, 반력 상한, 센서 상실). `err`를 먼저 보내고 `code`에 오류 코드 |

| 필드 | 뜻 |
|---|---|
| `seq`, `aid` | 원래 `DRIVE_STEP`의 seq와 `aid`. `DRIVE_STOP`으로 중단돼도 같다. `DRIVE_STOP` 자체는 자기 ACK만 받는다 |
| `code` | `ABORTED`, `FAULT`, `SHORT`일 때 오류 코드. 여러 오류가 겹치면 우선순위표에서 이긴 쪽의 코드 하나를 싣고, 나머지는 `err`와 `faults`로 본다 |
| `drive_mm` | 이번 명령으로 실제 이동한 거리. **엔코더가 유효할 때만** 싣는다 |
| `drv_pos_mm` | 정지 후 누적 위치. 엔코더가 유효하고 주행 원점이 있을 때만 |
| `drv_yaw_deg` | IMU가 유효할 때만 |
| `cp_force_N` | `OK`이고 반력 평균이 유효할 때만. 신규 변환 샘플이 `drv_avg_min_n`보다 적으면 생략한다. **0 N으로 대신 보내지 않는다.** `OK`인데 `cp_force_N`이 없으면 "거리는 도달, 반력 샘플 부족"이다. 로드셀 정체(`3-TU-009`)는 생략으로 처리하지 않고 `FAULT`다(§4.3 우선순위) |
| `force_n` | 평균에 쓴 신규 변환 샘플 수 |

### 4.3 완료 조건과 센서 유효성

1. 목표 거리에 이르거나 정지 요청·오류가 생기면 주행 출력을 끈다(제동 가능하면 제동).
2. **엔코더 유효성**: 출력 구간에서 좌·우 엔코더가 모두 펄스를 냈고, 좌·우 이동량 차이가 `drv_enc_diff_max_mm` 이하여야 한다.
   - 출력 중 `drv_enc_timeout_ms` 동안 한쪽이라도 펄스가 없으면 무효로 본다. 이 시간은 **최저 속도에서의 정상 펄스 간격**을 반영해 정한다.
   - 무효면 `3-TU-005`, `result = FAULT`이고 `drive_mm`, `drv_pos_mm`, `cp_force_N`을 생략한다.
3. **정지 확인**: 엔코더가 유효할 때만, 출력을 끈 뒤 `drv_settle_ms` 동안 변화가 없으면 정지로 본다. 엔코더 무변화만으로 정지를 확인하지 않는다. `drv_settle_max_ms` 안에 확인되지 않으면 `SETTLE_FAIL`.
4. **반력 평균**: 정지 확인 뒤 `drv_avg_ms` 동안 NAU7802의 **신규 변환 샘플만** 평균한다.
   - 500 Hz 제어·기록 루프에서 같은 변환값을 다시 읽은 것은 세지 않는다. NAU7802 지원 속도는 최대 320 SPS다.
   - `drv_avg_ms`, `drv_avg_min_n`은 실측 취득률로 정한다(§7).
   - 신규 변환이 `force_stale_ms` 넘게 없으면 `3-TU-009`.
5. 결과를 `act_cur`에 저장하고 `busy = NONE`으로 둔 뒤 `DRIVE_DONE`을 보낸다. ACK는 접수만 뜻한다.
6. 주행 구간이 `drv_timeout_ms` 안에 끝나지 않으면 출력을 끄고 `SHORT`, `2-TU-004`.

**`result` 우선순위**(여러 조건이 겹칠 때 위가 이긴다)

| 순위 | 조건 | `result` |
|---|---|---|
| 1 | ESTOP, 심각도 3 오류(`3-TU-003`, `3-TU-005`, `3-TU-009`, `3-TU-010` 등) | `FAULT` |
| 2 | 심각도 2 오류로 중단. **`2-TU-004`는 제외**(`2-TU-001`, `2-TU-002`, `2-TU-007` 등) | `ABORTED` |
| 3 | `DRIVE_STOP`, `DISARM` | `STOPPED` |
| 4 | 거리 미달·시간 초과(`2-TU-004`) | `SHORT` |
| 5 | 정지 미확인 | `SETTLE_FAIL` |
| 6 | 그 밖(반력 샘플 부족이면 `cp_force_N`만 생략) | `OK` |

**`2-TU-004`의 예외**: 거리 미달은 장치가 막히거나 미끄러진 것을 알리는 결과라서 `SHORT`로 보고한다(한건민 10/3 지적 4). 같은 주행에서 다른 심각도 2 오류가 함께 생기면 순위 2가 이겨 `ABORTED`다. 이때 `code`는 그 오류이고 `2-TU-004`는 `err`로 따로 나간다.

**`SETTLE_FAIL` 뒤**: Teensy는 정지 확인을 계속한다. 엔코더 변화가 `drv_settle_ms` 동안 없으면 `drv_settled = true`가 된다. 그 전에는 `DRIVE_STEP`을 `NOT_SETTLED`로 거부한다. 엔코더가 무효가 되면 `3-TU-005`다. 헤드 유지 상태는 바꾸지 않는다.

### 4.4 완료 이벤트 유실 (DRIVE_DONE, HEAD_HELD 공통)

Jetson의 대기 한도는 다음과 같다.
- `DRIVE_STEP`: `drv_timeout_ms + drv_settle_max_ms + drv_avg_ms + 1000 ms`
- `HEAD_HOLD`: `head_timeout_ms + 1000 ms`

한도를 넘으면 다음 순서를 따른다.

1. `GET_STATUS`로 같은 `tn`의 `tel`과 `mot`를 받아 `boot_id`, `busy`, `busy_seq`·`busy_aid`, `mot_last_seq`·`mot_last_aid`, `mot_last_result`를 본다. `GET_STATUS`는 조회 명령이라 응답이 없으면 같은 seq로 다시 보내면 된다(§2.5).
2. `boot_id`가 바뀌었으면 결과 불명으로 처리하고 재전송하지 않는다. 처음부터 다시 한다(§2.5).
3. `mot_last_aid`가 기다리던 `aid`면 **같은 seq·`aid`·파라미터로 원래 명령을 다시 보낸다.** Teensy는 재실행하지 않고 원래 ACK와 저장된 완료 이벤트를 다시 보낸다(§2.5 판단 순서 3).
4. `busy_aid`가 기다리던 `aid`면 한도까지 한 번 더 기다린다. 그래도 끝나지 않으면 `DRIVE_STOP`을 보내고 `STOPPED` 완료 이벤트를 기다린다. 그것도 없으면 FAULT를 표시하고 탐색을 중단한다.
5. 둘 다 아니면(ACK는 받았는데 동작 기록에 없음) 상태 불일치로 보고 FAULT를 표시하고 탐색을 중단한다.

### 4.5 중단(`STOPPED`, `ABORTED`, `FAULT`) 뒤 상태

중단된 동작은 **성공으로 처리하지 않는다.** 중단 뒤 허용되는 다음 동작을 명령별로 정한다.

| 중단된 명령 | 완료 이벤트 | 중단 뒤 상태 | 다음에 허용 | 거부 |
|---|---|---|---|---|
| `HOME`(`ARMED`·`HOMED`에서) | `HOMING_DONE result≠OK` | **`ARMED`**(HOME 원점 무효, `HOMED` 해제). 헤드는 임시 정지 또는 출력 차단(§2.1 `DRIVE_STOP`) | `HOME` | `HEAD_HOLD`, `DRIVE_STEP`(`NOT_HOMED`) |
| `HOME`(`VACUUM_HOLD`에서) | `HOMING_DONE result≠OK` | `VACUUM_HOLD`와 음압 유지, 헤드가 HOME 위치 아님. **압박 접촉 영점은 복귀 시작 때 이미 무효**(§2.2) | `HOME` | `SET_CP_POINT`(`HEAD_NOT_READY`), `START_CPR`·`TRIAL_COMPRESSION`(`CP_ZERO_UNSET`) |
| `HEAD_HOLD` 접촉 탐색 중 | `HEAD_HELD result≠OK` | `HOMED`, **`head_hold_active = false`**, 헤드 임시 정지 또는 출력 차단. 주행 원점은 만들지 않는다(`drv_zero` 무시) | `HOME` | `HEAD_HOLD`(`HEAD_NOT_READY`), `DRIVE_STEP`(`HEAD_NOT_READY`) |
| `HEAD_HOLD` 접촉 뒤 `press_mm` 하강 중 | 위와 같음 | 위와 같음. 접촉점은 기록하지만 유지는 성립하지 않음 | `HOME` | 위와 같음 |
| `DRIVE_STEP` | `DRIVE_DONE result=STOPPED` 등 | `HOMED`, 헤드 유지는 그대로 | 정지 확인 뒤(`drv_settled`) `DRIVE_STEP`, `HOME` | 정지 확인 전 `DRIVE_STEP`(`NOT_SETTLED`) |
| `SET_CP_POINT` | `CP_ZERO_SET result≠OK` | `VACUUM_HOLD`, 압박 접촉 영점 없음 | `HOME`(헤드 복귀) 뒤 `SET_CP_POINT` | `START_CPR`·`TRIAL_COMPRESSION`(`CP_ZERO_UNSET`), 헤드가 HOME이 아니면 `SET_CP_POINT`(`HEAD_NOT_READY`) |
| **어느 동작이든 `DISARM`으로 중단** | 원래 seq·`aid`로 `result = STOPPED` | **음압 출력 꺼짐**: `IDLE`, 모든 출력 차단. **음압 출력 켜짐**: `VACUUM_HOLD`, 압박·주행 출력 차단, 음압 유지, `rearm_lock = true`. 두 경우 모두 기구 HOME 원점, 탐색 접촉점, 압박 접촉 영점, 주행 원점, 헤드 유지를 무효로 한다 | `IDLE`: `ARM`. `VACUUM_HOLD`: `HOME`(헤드 복귀만), `VACUUM_OFF` | `IDLE`: 모든 동작 명령(`NOT_HOMED`). `VACUUM_HOLD`: 재가동 명령(`REARM_LOCKED`) |

- **우선순위**: `FAULT`·ESTOP > `DISARM` > 위 명령별 행. `FAULT`(심각도 3)로 끝났으면 §6 복구 순서(`RESET_FAULT → IDLE → ARM → HOME`)를 따른다. `DISARM`으로 끝났으면 명령별 행과 관계없이 `DISARM` 행을 따른다(한건민 10/3 지적 4, 10/4 음압 구분). 같은 동작이 `DRIVE_STOP`과 `DISARM`을 함께 받으면 `DISARM` 행을 따른다.
- Jetson은 `STOPPED`를 받으면 같은 `tn`의 `tel`·`mot`로 실제 상태를 확인한다. `STOPPED`만으로 `HOMED`가 남았다고 보지 않는다.
- "임시 정지"는 §6 `2-TU-002`의 유지 조건을 모두 만족할 때만 하는 정지 위치 유지이며, `head_hold_active`를 켜지 않는다.

---

## 5. 기준점 구분

| 기준 | 정의 | 바뀌는 때 | 프로토콜로 바꿀 수 있나 |
|---|---|---|---|
| 센서 영구 보정값 | 리니어 센서·로드셀 변환식 | 교정 작업 때만(펌웨어 설정) | **아니오.** 어떤 명령도 덮어쓰지 않는다 |
| 기구 HOME 원점 | 상단 리미트 검출 위치 | `HOME` 완료. **상단 리미트 설치·검증 전에는 없음** | `HOME` |
| 탐색 접촉점 | 모사체 표면 | `HEAD_HOLD` | `HEAD_HOLD` |
| 압박 접촉 영점 | 흡착 고정 뒤 압박 시작 기준 | `SET_CP_POINT(offset_mm=0)` | `SET_CP_POINT` |
| 주행 원점 | `drv_pos_mm = 0`, `drv_yaw_deg = 0` | **`HEAD_HOLD(drv_zero:true)` 성공 때만.** ARM이나 재부팅 뒤에는 원점이 없다(`drv_pos_mm` 생략) | `HEAD_HOLD`의 `drv_zero` |

탐색 중 헤드를 다시 내리려고 `HEAD_HOLD`를 보낼 때는 `drv_zero`를 생략한다. 좌표는 유지된다.
Jetson은 **새 탐색의 첫 `HEAD_HOLD`에만** `drv_zero:true`를 보낸다.

---

## 6. 오류 코드 (`TU` = 운반 유닛, 추가만 한다)

**음압**: 모든 TU 오류에서 음압 상태를 바꾸지 않는다. 켜져 있으면 유지하고, 꺼져 있으면 켜지 않는다. 기존 FAULT·ESTOP의 음압 유지 규칙(v1.0 §7)과 같다. 탐색은 흡착 전에 하므로 보통 꺼져 있다.
**출력 차단 범위**: "주행"은 후륜 구동 출력, "압박 축"은 압박 모터 출력이다. 물리 E-stop 회로가 실제로 무엇을 끊는지는 한건민이 별도로 확인한다.

| 코드 | 의미 | 주행 | 압박 축 | latched | 원인이 남은 동안 | 복구 |
|---|---|---|---|---|---|---|
| `2-TU-001` | 방향 이탈(`\|drv_yaw_deg\| > drv_yaw_max_deg`) | 출력 차단 | 현재 상태 유지 | 아니오 | `DRIVE_STEP` 거부 | 사용자 재정렬 뒤 새 탐색(`drv_zero:true`) |
| `2-TU-002` | 기울기 초과(`> drv_tilt_max_deg`) | 출력 차단 | **진행 중인 하강 목표를 취소**하고, 아래 유지 조건을 **모두** 만족하면 **정지 시점의 실제 위치**를 유지. 하나라도 어긋나면 출력 차단 | 아니오 | `HOME` 외 동작 명령 거부 | 사용자 확인 뒤 `HOME` |
| `3-TU-003` | 탐색 반력 상한 초과(`> scan_force_max_N`) | **출력 차단(먼저)** | **출력 차단(먼저)** | 예 | 모든 동작 명령 거부 | 원인 해소 확인 → `RESET_FAULT` → `IDLE` → `ARM` → `HOME` → 새 탐색 |
| `2-TU-004` | 거리 미달·시간 초과(막힘, 미끄러짐) | 출력 차단 | 현재 상태 유지 | 아니오 | — | 사용자 확인 뒤 새 명령 |
| `3-TU-005` | 주행 엔코더 무효(§4.3 조건) | **출력 차단(먼저)** | **출력 차단(먼저)** | 예 | 모든 동작 명령 거부 | 엔코더 점검 → `RESET_FAULT` → `ARM` → `HOME` → 새 탐색 |
| `2-TU-006` | 접촉 실패 | — | 조건부 복귀(§2.3), 아니면 출력 차단 | 아니오 | — | 위치 재정렬 |
| `2-TU-007` | 자이로 수신 끊김(`drv_imu_timeout_ms`) | 출력 차단 | 현재 상태 유지 | 아니오 | `DRIVE_STEP` 거부 | 수신 회복 뒤 사용자 확인 |
| `2-TU-008` | 설정값 미설정으로 명령 거부 | 동작 없음 | 동작 없음 | 아니오 | 해당 명령 거부 | 설정값 입력 |
| `3-TU-009` | 로드셀 데이터 상실·정체(신규 변환 없음 `> force_stale_ms`). 동작 명령 중이나 헤드 유지 중 | **출력 차단(먼저)** | **출력 차단(먼저)** | 예 | 모든 동작 명령 거부 | 신규 변환 확인 → `RESET_FAULT` → `ARM` → `HOME` |
| `3-TU-010` | 리니어 센서 상실·범위 이탈. 동작 명령 중이나 헤드 유지 중 | **출력 차단(먼저)** | **출력 차단(먼저)** | 예 | 모든 동작 명령 거부 | 센서 점검 → `RESET_FAULT` → `ARM` → `HOME` |

**헤드 위치 유지 조건**(`2-TU-002`, §2.1 `DRIVE_STOP`의 임시 정지 공통. 한건민 10/2 조건부 동의)

1. 리니어 센서 유효(신호 범위 안, 변화 정상)
2. 로드셀 신규 변환이 `force_stale_ms` 안에 있음
3. 반력이 `scan_force_max_N` 이하
4. 압박 드라이버 고장 신호 없음
5. 헤드가 이동 가능 범위 안(상·하단 리미트 미검출, 리니어 센서 기준 기구 범위 안)

기준 위치는 기존 하강 목표가 아니라 **정지 시점에 리니어 센서로 잰 실제 위치**다. 유지 중에도 1~5를 계속 확인하고, 어긋나는 순간 출력을 차단한다.
**우선순위**: 심각도 3 오류나 ESTOP이 겹치면 그 차단 규칙이 이긴다(유지하지 않고 차단). 출력을 끊은 뒤 헤드가 기계적으로 그 자리에 머무는지는 실측 전이다(§1, §10).

- 심각도 3은 v1.0 §4.5대로 **Teensy가 먼저 정지하고 나중에 보고**한다.
- **원인 지속 거부**: `latched = false`여도 오류 원인이 남아 `faults` 배열에 있는 동안은 위 열의 명령을 `FAULT_ACTIVE`로 거부한다.
- `RESET_FAULT` 뒤 상태는 `IDLE`이다(현재 Teensy 코드와 같음). 바로 `HOME`이 아니라 `ARM`을 먼저 거친다.
- 압박(CPR) 중 로드셀·리니어 센서 상실 처리는 이 초안의 범위 밖이다. 기존 규칙을 바꾸지 않고 §10 미확정으로 둔다.
- 동작 명령 중 통신 두절은 기존 §9를 따른다. 압박 중이 아니므로 즉시 FAULT, 모터 차단, 음압 유지다.

---

## 7. 설정값 (0 = 미설정 → 관련 명령 거부)

**순서**: 10/6까지 모든 필수 항목에 **벤치 시험용 제한값**을 근거·책임자와 함께 정한다. 10/7에 그 값으로 인터록을 켠 채 검증하고 조정한다.
10/6 값이 없는 항목은 그 명령을 구동하지 않는다. **값이 없는 상태에서 인터록을 해제하지 않는다.**

### 7.1 Teensy

| 이름 | 단위 | 쓰임 | 10/6 벤치값 근거 | 10/7 검증 | 책임 |
|---|---|---|---|---|---|
| `limit_top_ok` | 0/1 | 상단 리미트 설치·검증 여부 | 설치·동작 확인 | — | 확인 필요 |
| `enc_counts_per_mm_l`, `_r` | 개/mm | 엔코더 환산(좌·우 따로) | 엔코더 사양 × 감속비 / 바퀴 둘레 계산값 | 좌·우 각각, 정·역방향, 외부 자로 잰 실제 이동거리와 비교 | 한건민 |
| `drv_speed_mm_s`, `drv_speed_max_mm_s`, `drv_accel_mm_s2` | mm/s, mm/s² | 주행 속도·가감속 | 바퀴를 띄운 벤치 기준 저속값 | 정지 거리와 헤드 접촉 중 밀림 측정 뒤 조정 | 한건민 |
| `drv_step_max_mm` | mm | 1회 이동 상한 | 탐색 간격 `scan_step_mm` 이상의 작은 값 | — | 한건민, 신상현 |
| `drv_timeout_ms` | ms | 주행 구간 시간 제한 | 상한 거리 / 속도에 여유 | 실측 | 한건민 |
| `drv_settle_ms`, `drv_settle_max_ms` | ms | 정지 확인 | 저속 정상 펄스 간격 | 제동 시험 | 한건민 |
| `drv_avg_ms`, `drv_avg_min_n` | ms, 개 | 반력 평균 | NAU7802 설정 SPS × 평균 시간 | **실측 취득률**로 조정 | 한건민 |
| `force_stale_ms` | ms | 로드셀 정체 판정 | NAU7802 변환 주기의 배수 | 실측 취득률 | 한건민 |
| `drv_pos_tol_mm` | mm | 도달 판정 허용 오차 | 엔코더 분해능 | 환산 시험 | 한건민 |
| `drv_enc_timeout_ms` | ms | 엔코더 무효 판정 | **최저 속도의 정상 펄스 간격** 반영 | 저속 주행 | 한건민 |
| `drv_enc_diff_max_mm` | mm | 좌·우 이동량 차 허용 | 탐색 구간 직진 허용 | 직진 시험 | 한건민 |
| `drv_yaw_max_deg`, `drv_tilt_max_deg` | ° | 방향·기울기 상한 | **기구·시험대의 허용 범위** 기준(측정 최대값이 아님) | 확인 | 서정준(기구), 한건민 |
| `drv_imu_timeout_ms` | ms | 자이로 끊김 판정 | WT901C 출력 주기의 배수 | 수신 확인 | 한건민 |
| `scan_force_max_N` | N | 탐색·헤드 유지·주행 중 반력 상한 | 모사체·고정구·기구의 허용 하중. 로드셀 정격(100 kg)을 쓰지 않는다 | 보정된 로드셀로 확인 | 서정준(기구), 한건민 |
| `contact_force_N`, `contact_confirm_ms` | N, ms | 접촉 판정 | 무부하 잡음, 영점 드리프트 | **실제 접촉, 모터 작동 중 잡음** 확인 | 한건민 |
| `head_search_speed_mm_s`, `head_search_max_mm`, `head_timeout_ms` | mm/s, mm, ms | 접촉 탐색 | 기구와 마네킹 치수 | 확인 | 서정준(치수), 한건민 |
| `head_press_max_mm` | mm | `press_mm` 상한 | 기구 | 확인 | 서정준, 한건민 |

### 7.2 Jetson (신상현)

| 이름 | 값 | 근거 |
|---|---|---|
| `press_mm` | 10.0 | 제출 보고서 6.2 (δ₀) |
| `cp_dist_ref_mm`, `eapd_ref_mm`, `cp_dist_max_mm` | 89.7, 205.8, 95 | 제출 보고서 6.2 식. 설계 가설이며 인체 검증 없음 |
| 판정 기준 | 직전 이동평균 대비 30 % 감소 | 제출 보고서 6.2 |
| `scan_step_mm`, `scan_roi_mm` | 미정 | 모사체 실측 후(선정용 반복) |
| `scan_dir_sign` | 미정 | 설치 방향 확인 후 |
| **EAPD 미측정 시** | **이동 거부** | 대체 상수를 쓰지 않는다. 압박점 이동을 하지 않고 재측정을 안내한다. 보고서에 대체 규칙이 없으므로 값이 비어 있으면 기능을 켜지 않는다는 저장소 규칙을 따른다 |

---

## 8. 시험

**기본 묶음(하드웨어 없음)**

Jetson `tests/test_all.py` + 모의 Teensy:
- 상태 불일치 거부, 설정값 미설정 거부
- `GET_INFO` 미지원(v1.0) → 탐색 기능 끔
- 동작 명령 중첩: 주행 중·접촉 탐색 중·HOME 중 다른 동작 명령 → `BUSY`
- 중복과 오래된 요청(§2.5):
  - 진행 중 재수신(같은 seq·`aid`) → 원래 ACK만
  - 완료 뒤 재수신 → 원래 ACK와 저장된 완료 이벤트
  - **동작 17개 이상 수락 뒤 첫 동작의 seq·`aid` 재수신 → `STALE_SEQ`, 실행 안 함**(기록 밖의 옛 요청)
  - **조회 명령 40,000개 뒤 옛 동작 요청 → `STALE_SEQ`.** 같은 뒤 새 `aid` 동작 → 정상 실행
  - 진행 중인 동작(`aid` 5) 뒤 다른 동작(`aid` 6)이 `BUSY`로 거부 → `aid` 5 재전송은 여전히 원래 ACK·완료 이벤트(진행 기록은 교체되지 않음)
  - 거부된 동작 재전송(같은 seq·`aid`) → 원래 거부 ACK, 다시 평가하지 않음
  - 같은 `aid`, 다른 seq·명령·파라미터 → `SEQ_CONFLICT`, 실행 안 함
  - `bid ≠ boot_id` → `BOOT_MISMATCH`. `bid`·`aid` 없음 → `AID_REQUIRED`
  - 조회 명령(`GET_INFO`, `GET_STATUS`, `PING`, `GET_FAULTS`) 같은 seq 재전송 → 매번 새 응답
- `GET_INFO`(§2.4):
  - `INFO` 응답 유실 뒤 같은 seq로 재요청 → 처음과 같은 `last_seq`, `last_aid`
  - 조회 명령, 재전송, `STALE_SEQ`·`BOOT_MISMATCH`·`SEQ_CONFLICT` 거부 뒤에도 `last_seq`, `last_aid`가 바뀌지 않음
  - **seq 순환**: `last_seq = 65535` → Jetson 다음 seq 1. 순환 뒤 동작 명령이 `aid`로 정상 판정됨
- Jetson 재시작: `GET_INFO`의 `last_seq + 1`, `last_aid + 1`, `boot_id`로 이어 감. 이전 seq·`aid` 재사용 없음
- 접촉 탐색 시작 조건: 헤드가 HOME이 아니면 `HEAD_NOT_READY`, 시작 시 힘이 접촉 판정 이상이면 `IN_CONTACT`, 유지 중 새 `aid` `HEAD_HOLD` → `ALREADY_HOLDING`
- 중단 뒤 상태(§4.5): 중단된 HOME → `ARMED`, `HEAD_HOLD`·`DRIVE_STEP` 거부. 중단된 접촉 탐색 → `head_hold_active = false`, `DRIVE_STEP` 거부. 중단된 `SET_CP_POINT` → `START_CPR` 거부
- `SETTLE_FAIL` 뒤 정지 확인 전 `DRIVE_STEP` → `NOT_SETTLED`
- 반력 평균 중 로드셀 정체 → `OK`가 아니라 `FAULT`(`3-TU-009`). 샘플 수만 부족 → `OK`, `cp_force_N` 생략
- 기울기 초과: 유지 조건 5개 중 하나라도 어긋나면 출력 차단, 심각도 3·ESTOP이 겹치면 차단
- `DRIVE_STOP` 중단: 원래 seq·`aid`로 `STOPPED`, STOP 자체는 ACK만
- **`DISARM`**(5차): 음압 출력 꺼짐 → `IDLE`. 음압 출력 켜짐(`VACUUM_ON` 직후 포함, `CPR_RUNNING` 포함) → `VACUUM_HOLD`, 음압 유지, `rearm_lock = true`, 원점·영점·유지 무효. 그 뒤 `SET_CP_POINT`·`SET_TARGET`·`START_CPR`·`RESUME_CPR`·`HEAD_HOLD`·`DRIVE_STEP` → `REARM_LOCKED`. `HOME`은 헤드 복귀만 하고 `rearm_lock` 유지. `VACUUM_OFF` → `IDLE`, `rearm_lock = false`. `FAULT`·ESTOP에서 `DISARM` → 상태·출력 불변
- **`bid`·`aid` 확대**(5차): 즉시 변경 명령(`ARM`, `START_CPR`, `RESET_FAULT` 등)의 옛 `aid` → `STALE_SEQ`, 실행 안 함. 같은 `aid` 재전송 → `cmd_last`의 원래 ACK. 동작 실행 중 `SET_TEL_RATE`·`PAUSE_CPR`을 받아도 `act_cur`가 바뀌지 않아 동작 결과를 다시 받을 수 있음
- **정지 방향 예외**: `bid` 틀림·`aid` 없음·옛 `aid`의 `PAUSE_CPR`·`STOP_CPR`·`DRIVE_STOP`·`DISARM`도 실행(상태 검사는 함). 같은 조건의 `RESUME_CPR`·`START_CPR`·`RESET_FAULT`·`VACUUM_OFF`는 거부
- **`aid` 소진**: `aid_max = 4,294,967,295` 뒤 새 `aid` → `AID_EXHAUSTED`, 정지 방향 명령은 실행
- 조회 명령은 `bid`·`aid` 없이 실행되고 기록에 남지 않음
- **결과 우선순위**: 거리 미달만 → `SHORT`(`code` `2-TU-004`). 거리 미달과 방향 이탈이 함께 → `ABORTED`(`code` `2-TU-001`)
- **`VACUUM_HOLD`의 `HOME`**: 시작 즉시 압박 접촉 영점 무효, 음압·상태 유지, 끝난 뒤(성공·중단 모두) `START_CPR`·`TRIAL_COMPRESSION` → `CP_ZERO_UNSET`, `SET_CP_POINT` 성공 뒤 허용. `limit_top_ok = 0` → `HOME_UNAVAILABLE`
- `tel`/`mot` 동기화(§3.3): 같은 `tn` 짝만 함께 판단, `mot`만 오래되면 새 동작 명령 안 보냄, `boot_id` 변화 시 이전 값 폐기, 필드 생략 시 이전 값 이어 쓰지 않음, `tn` 역전 메시지 폐기
- `faults_more` → `GET_FAULTS` → `err` 줄(심각도 3 먼저)과 `FAULTS_END n` 일치, 불일치면 재요청
- `DRIVE_DONE`·`HEAD_HELD` 유실 → `GET_STATUS` → 같은 seq·`aid` 재요청
- 재부팅(`boot_id` 변화) → 재전송하지 않음
- FAULT·ESTOP 중 `DRIVE_STOP`이 상태를 바꾸지 않음
- 원인이 남은 비래치 오류 중 동작 명령 → `FAULT_ACTIVE`
- 엔코더 무효 → `FAULT`, 거리·반력 필드 없음
- `OK`인데 반력 무효 → `cp_force_N` 생략
- 주행 중 통신 두절 → FAULT
- `RESET_FAULT` 뒤 `IDLE`, `HOME` 거부, `ARM` 뒤 `HOME`

Teensy `test_saver_proto.cpp`(한건민):
- 음수·소수 `dist_mm` 파싱, 필드 생략 규칙, evt의 seq
- 파라미터 정규화와 `SEQ_CONFLICT`, `aid_max`·`act_cur`·`act_rej` 판단 순서, `BOOT_MISMATCH`, `GET_INFO`의 `last_seq`·`last_aid` 불변 조건
- 중복 요청, 재부팅, 센서 단절, 명령 충돌, 정지 우선순위
- **최대 길이 생성 시험**(§3.4): 실제 생성 함수(`buildTel` 등)에 범위 최댓값을 넣어 모든 메시지 종류의 본문 ≤ 511 byte. 범위 밖 실수는 필드 생략. 송신 줄 끝이 LF 하나인지(CR 없음)
- `DISARM`이 `FAULT`·ESTOP에서 상태를 바꾸지 않는지, 음압 출력 여부에 따라 `IDLE`·`VACUUM_HOLD`로 나뉘는지, `rearm_lock`이 `VACUUM_OFF`로만 풀리는지
- `bid`·`aid` 판단 순서(정지 방향 예외, `cmd_last`, `AID_EXHAUSTED`)
- 실제 생성 함수 출력이 `Serial.write()` 기준 LF 하나로 끝나는지, 버퍼 초과 시 잘린 줄 없이 송신 실패로 처리하는지

**하드웨어 시험(기본 묶음 밖, 실물 필요)**
- 10/7: 바퀴를 띄운 벤치 → 바닥 주행 순서로 진행한다.
  - 엔코더 환산: 좌·우, 정·역방향
  - 정지 거리, 헤드 접촉 중 밀림
  - 자이로 수신과 모터 작동 중 방향 오차
  - 로드셀 실측 취득률
- 비상정지가 후륜 전원을 끊는지 확인한다. 끊지 않으면 바닥 주행을 하지 않는다.
- 출력 차단 뒤 헤드 기계적 유지: 모사체를 `press_mm` 누른 상태에서 압박 축 출력을 끊고 리니어 센서로 처짐을 잰다.
- 핀 이동 묶음(§1 표 6개 파일) 뒤 음압 모터 2 RUN·EN, WT901C 수신, PPG 시험 코드, `test/test.ino`(`check_combined.ps1` 포함)를 다시 확인한다.
- I²C 별도 버스(§1): 각 버스의 풀업 전압과 병렬 저항, NAU7802 신규 변환 취득률(PPG 읽기와 동시), `Wire1`을 일부러 멈췄을 때 `3-TU-009`까지 걸리는 시간과 제어 루프 지연을 잰다. 주행 엔코더 #1을 11/12로 옮긴 뒤 정·역방향 펄스를 다시 확인한다.
- 상단 리미트가 설치·검증되지 않으면 HOME, `HEAD_HOLD`, `DRIVE_STEP`의 프로토콜 시험을 하지 않는다.

---

## 9. 일정

| 날짜 | 신상현 | 한건민 |
|---|---|---|
| 10/1 | 2차 초안 공유 | — |
| 10/2 | 3차 초안 공유(3차 의견 반영) | 2차 초안 검토(완료), 3차 초안 검토 |
| 10/3 | 4차 초안 공유(4차 의견 반영) | 3차 초안 검토(완료), 4차 초안 검토 |
| 10/4 | 5차 초안 공유(5차 회신 반영) | 4차 초안 검토(완료), 5차 확인 요청 2건 회신 |
| 10/5 | 합의 | 합의, 핀 이동 묶음 커밋 |
| 10/5~6 | `protocol/` v1.1 반영(한건민 검토 뒤 커밋), `state_machine_v2.json` `S13` 조건, Jetson 모의 Teensy와 시험 | 펌웨어 구현과 시험, 후륜 비상정지 배선, **핀 이동 묶음**(§1 표 6개 파일, `test/test.ino` 포함) |
| 10/6 | 7.1 벤치값 확정(팀, 근거·책임 기록) | 같이 |
| 10/7 | — | Teensy 단독 주행·제동·헤드 유지 점검, 7.1 검증·조정 |
| 10/11 | 벤치 통신 시험(새 명령 포함, 모터 없이) | 같이 |
| 10/12 | 장치 연결 | 같이 |

---

## 10. 이 초안의 미확정 항목

| # | 항목 | 담당 |
|---|---|---|
| 1 | 상단 리미트 설치·검증 담당과 기한 | 서정준, 한건민 |
| 2 | 후륜 비상정지 회로의 실제 차단 범위, 제동 방식 | 한건민 |
| 3 | WT901C 출력 모드(지자기 사용 여부)와 모터 작동 중 방향 오차 | 한건민 |
| 4 | 압박(CPR) 중 로드셀·리니어 센서 상실 시 동작 | 한건민, 신상현 (기존 규칙 변경이므로 별도 합의) |
| 5 | 출력 차단 뒤 헤드의 기계적 유지(볼스크류 역구동) 실측. 결과에 따라 §6 유지 조건과 임시 정지 규칙을 다시 본다 | 한건민 |
| 6 | `HOME`을 `VACUUM_HOLD`에서 허용하는 상태표 변경(§2.2) | 한건민, 신상현 |
| 7 | I²C는 별도 버스로 결정(10/3, MAX30102 `Wire` 18/19, NAU7802 `Wire1` 17/16, 엔코더 #1 11/12, `A9` 예비). 남은 것은 실물 검증: 각 버스 풀업 전압·병렬 저항, 취득률, 버스 장애 시 제어 지연 | 한건민 |
| 8 | 텔레메트리 분할 방향은 합의(10/3). 4차 배치(상태 필드를 `mot`로, `mot` 매 주기 송신)와 §3.4 형식 규칙 확인 | 한건민, 신상현 |
| 9 | `DISARM` 때 음압 처리: **한건민 안으로 정함(10/4)**. 음압 출력이 켜져 있으면 음압을 유지하고 `VACUUM_HOLD` + `rearm_lock`. 해제는 `VACUUM_OFF`나 수동 조작으로만. 남은 것: 확인 요청 1(§0.5), 의학적 타당성 **한성윤 확인 필요** | 한건민, 신상현, 한성윤 |
| 10 | `bid`·`aid` 확대: **조회 4개 외 모든 명령으로 정함(10/4)**. 남은 것: 정지 방향 예외(§0.5 확인 요청 2) | 한건민, 신상현 |
| 11 | main 코드 3건(§0.4): 한건민 확인(10/4), v1.1 구현 때 수정하기로 합의(§0.5-5) | 한건민 |
| 12 | `S13`에서 `DISARM` 뒤 음압이 남으면 `S2`로 돌아가지 않고 해제 절차로 안내(`state_machine_v2.json` 변경, v1.1 묶음) | 신상현, 한건민 |
