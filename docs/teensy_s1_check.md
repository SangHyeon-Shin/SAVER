# S1 장치 상태 확인(점검)을 통과하려면: Teensy 쪽 요구 (D-54, 한건민 Codex 전달용)

작성: 2026-10-23 밤, 신상현(Claude Code). 대상은 한건민의 Codex(`teensy/`)다.

- **젯슨 기준:** 브랜치 `claude/saver-project-onboarding-xpmdhw`의 `jetson/saver/init_seq.py`, `teensy_serial.py`.
- **펌웨어 기준:** 브랜치 `codex/teensy-v24-review-20261011`의 `378e058`(10/23 18:21, 수동 배치 시연 펌웨어). 근거는 이 펌웨어의 **코드를 읽은 결과**다. 실기에서 시험하지 않았다.
- **고치지 않은 것:** `protocol/`, `teensy/`는 고치지 않았다(AGENTS.md 소유권).

## 0. 요약

| 항목 | 내용 |
|---|---|
| 비상정지 | 팀장 지시(10/23): "비상정지 버튼은 연결을 안했으니까 장치상태확인(점검)에서는 빼라". 젯슨 결선 설정 `estop_wired: false`로 점검에서 `estop` 표시를 보지 않는다. **Teensy는 지금 그대로 두면 된다.** `ESTOP_AUX_INSTALLED = false`, `estop` 필드를 싣지 않고, `ESTOP` 상태로 가지 않는다 |
| 지금 펌웨어에서 S1이 멈추는 곳(코드 대조) | ① `ARM` → `ok:false`, `VACUUM_RELEASE_NOT_VERIFIED`. ② `HOME` → `ok:false`, `HOME_UNAVAILABLE_NO_VERIFIED_LIMIT`(항상). 비상정지는 원인이 아니다 |
| 화면 | 오류 화면(S10)에 늘 보이던 "비상정지 버튼을 확인하세요"를 뺐다. 대신 실제 사유를 보인다. 예: "장치가 구동 준비(ARM)를 거부했습니다 (VACUUM_RELEASE_NOT_VERIFIED)" |
| Teensy가 할 일 | 3장. ARM은 부팅 직후 음압 해제 판정이 성립해야 한다. HOME은 리미트가 없으므로 3.2의 선택이 필요하다 |

## 1. 젯슨 S1 점검 순서와 통과 조건

| 단계 | 젯슨 → Teensy | 통과 조건 | 실패 사유(화면에 그대로 나옴) |
|---|---|---|---|
| 0 연결 | `GET_INFO`(seq만) | ACK `ok:true` + `evt INFO`: `proto:"1.1"`, `caps`에 `"AID"`, `boot_id` | `FW_VERSION:<판>` |
| 1 | `PING` | ACK `ok:true`. ACK는 200 ms 안. 두 번 다시 보내도 없으면 통신 오류 | `PING_REJECTED:<why>`, `ACK_TIMEOUT…` |
| 2 | `GET_STATUS` | ACK `ok:true`, 그 뒤 **500 ms 안** `tel` 1회. `state`가 `IDLE`, `ARMED`, `HOMED` 중 하나. `faults`는 비었거나 없음. `estop`은 보지 않음(결선 설정) | `NO_STATUS`, `FAULT:<코드>`, `UNEXPECTED_MCU_STATE:<상태>` |
| 3 | `ARM`(`bid`, `aid`). `IDLE`일 때만 | ACK `ok:true`, 이후 `state:"ARMED"` | `ARM_REJECTED:<why>` |
| 4 | `HOME`(`bid`, `aid`). `ARMED`일 때 | ACK `ok:true`. 이때 젯슨은 다음 화면으로 넘어간다(`INIT_DONE`) | `HOME_REJECTED:<why>` |
| 5 | (뒤에서 기다림) | `evt HOMING_DONE` `{seq, aid, result:"OK"}` 또는 `tel`의 `state:"HOMED"`. 기다리는 동안 `state`는 `ARMED`나 `HOMED`여야 한다. 시간 제한 `home_timeout_s`는 비어 있다(제한 없음) | `HOMING_<result>`, `HOMING_LOST:<상태>` |

- **상태별 생략:** 시작 상태가 `HOMED`면 3~5단계, `ARMED`면 3단계를 건너뛴다.
- **재확인:** [재확인]을 누르면 S1에서 처음부터 다시 한다. 젯슨은 `DISARM`을 자동으로 보내지 않는다.
- **하트비트:** 젯슨은 `PING`을 1 s마다 보낸다. 펌웨어의 "2 s 동안 JSON이 없으면 정지"(3-CM-010)에는 걸리지 않는다.
- **번호:** `seq`는 1~65535다. `bid`·`aid`는 젯슨 링크가 붙인다.

## 2. 지금 펌웨어(378e058)가 각 단계에 하는 응답 (코드 대조)

| 단계 | 응답(`src/demo/demo_app.h`) | 결과 |
|---|---|---|
| `GET_INFO` | `proto "1.1"`, `caps ["AID","MOT"]`, `boot_id` | 통과 |
| `PING` | ACK `ok:true` | 통과 |
| `GET_STATUS` | ACK `ok:true`, `tel`(부팅 직후 `state "IDLE"`, `faults []`, `estop` 없음)과 `mot` | 통과 |
| `ARM` | `execute()`: `rearmLock_ \|\| !vac_.released()`이면 `VACUUM_RELEASE_NOT_VERIFIED` | **막힘** |
| `HOME` | `execute()`: 항상 `HOME_UNAVAILABLE_NO_VERIFIED_LIMIT` | **막힘** |

**ARM이 막히는 이유:** `vacuum_module.h`의 `released_`는 처음에 `false`다. 아래 두 조건이 모두 맞아야 `true`가 된다.

1. `VAC CONFIG`로 해제 대역(kPa)과 해제 시간(ms)이 설정돼 있다(`configured()`).
2. 압력이 그 대역 안에서 해제 시간 이상 머문다.

두 가지가 걸린다.

- `VAC CONFIG`는 RAM에만 남는 직접 명령이다. 젯슨 브리지가 포트를 쥐고 있는 동안에는 넣을 수 없다.
- README_demo.md에 적힌 대로, 대기압에 열어 둔 상태에서 압력이 **−24.60 kPa**로 읽힌다. 이 값이면 해제 대역에 들 수 없다.

**ARM의 다른 조건(`configuredInputsReady()`):** 지금은 `PHYSICAL_ESTOP_POWER_CUT_USER_CONFIRMED = true`라서 통과한다. 이 값의 근거는 10/15 보고("독립 물리 전원 차단 있음")다. 오늘 팀장은 "비상정지 버튼은 연결을 안 했다"고 했다.

- 버튼의 Teensy 보조 접점만 미연결인지, 전원 차단 수단 자체가 없는지 **건민이 확인한다.**
- 전원 차단 수단이 없다면 이 값은 `false`가 맞다. 그러면 ARM은 `SAFETY_INPUTS_UNKNOWN`으로 거부된다.
- 전원 장치 스위치처럼 손 닿는 곳의 차단 수단을 정해 두면, 그 사실을 근거로 이 값을 유지할 수 있다. 판단은 건민이 한다.
- **점검을 통과시키려고 이 값을 바꾸지 않는다.**

## 3. Teensy가 해야 할 일

### 3.1 ARM: 부팅 직후 음압 해제 판정이 성립해야 한다

**요구:** 전원을 넣은 뒤 블로워를 켜지 않고 압력관이 대기에 열린 상태라면, ARM이 수락돼야 한다.

**방법(명세 A.7.1 그대로, 젯슨 수정 없음):**

- 압력센서 대기압 오프셋(−24.60 kPa)의 원인을 고친다(분압, 공급전압, 환산식).
- 해제 대역과 해제 시간을 **실측해 펌웨어 기본값(또는 EEPROM)으로** 둔다. 젯슨이 연결된 세션에서는 직접 명령을 넣을 수 없기 때문이다.

**주의:**

- 부팅 뒤 해제 시간이 지나기 전에 ARM을 받으면 거부될 수 있다. 그러면 화면에 사유가 나오고, [재확인]으로 다시 하면 된다.
- 실측값이 없으면 지금처럼 거부하는 것이 맞다. 해제 판정을 건너뛰거나 꾸미지 않는다.

### 3.2 HOME: 리미트가 없는 지금 하드웨어에서의 처리 (선택 필요)

**요구:** HOME을 받아 ACK `ok:true`를 보내고, 그 뒤 `HOMING_DONE result:"OK"`를 보내거나 `state:"HOMED"`가 된다.

**현재 명세:** 명세 §5에서 HOME은 **상단 리미트 기준**이고, `limit_top_ok = 0`이면 `HOME_UNAVAILABLE`로 거부한다. 따라서 지금의 거부는 명세대로다.

| 선택 | 내용 | 합의 |
|---|---|---|
| A. 상단 리미트 설치 | 리미트를 달고 극성과 동작을 확인한 뒤, 명세대로 원점 이동 | 필요 없음(명세 그대로). 결선 전에는 어렵다 |
| **B. 수동 기준점 확인(제안)** | HOME에서 **모터를 움직이지 않고 확인만** 한다. 확인되면 ACK `ok:true` 뒤 바로 `{"v":1,"t":"evt","ms":…,"evt":"HOMING_DONE","seq":<HOME seq>,"aid":<aid>,"result":"OK"}`를 보내고 `state:"HOMED"`가 된다. 아니면 구체적인 사유로 거부한다 | **명세 §5와 다르다.** 팀장과 건민이 합의한 뒤 신상현이 `protocol/` §5에 "수동 배치 빌드의 HOME"을 적는다 |
| C. 젯슨이 HOME을 건너뜀 | Teensy는 그대로 두고, 젯슨 결선 설정에서 S1의 HOME 단계를 뺀다(신상현 작업) | 팀장 결정 |

**선택 B에서 HOME이 확인할 것:**

1. 리니어 보정이 유효하다. 부팅 때 EEPROM에서 읽었거나, 이번 부팅에서 ZERO/CAL을 마쳤어야 한다.
2. 헤드가 보정 영점에서 **실측한 허용오차** 안에 있다.
3. 헤드가 정지 상태다. 속도가 **실측한 정지 대역** 안이어야 한다.

**선택 B의 거부 사유 예:** `HOME_CAL_INVALID`, `HOME_NOT_AT_REFERENCE`, `HOME_NOT_AT_REST`, `HOME_REF_TOL_UNSET`.

**선택 B의 규칙:**

- 지금 `START_TOLERANCE_MM`, `MAX_START_VELOCITY_MM_S`는 0(미설정)이다. 실측하기 전에는 `HOME_REF_TOL_UNSET`으로 거부하는 것이 맞다.
- 리미트 없이 모터를 돌려 원점을 찾지 않는다.
- 확인하지 않고 `HOMING_DONE OK`를 보내지 않는다.

### 3.3 그대로 둘 것

- `ESTOP_AUX_INSTALLED = false`를 유지한다. `estop` 필드는 싣지 않는다. 0이나 `false`로 채우지 않는다(측정 불가 필드는 생략).
- `faults`는 고장이 없으면 지금처럼 `[]`로 보낸다. 미할당 벤치 고장일 때 필드를 생략하는 지금 방식도 유지한다. 둘 다 젯슨 점검과 맞는다.
- 정지 명령을 우선하는 처리, ACK 재전송 처리, 인터록, `BENCH_DRIVE_CONFIRMED` 잠금은 그대로 둔다. S1 점검은 압박 구동을 요청하지 않는다.
- `protocol/`, `jetson/`, `ui/`는 고치지 않는다.

## 4. 확인 방법

**① 시리얼 터미널로 JSON 확인.** 젯슨 브리지는 꺼 둔다. 같은 포트를 둘이 동시에 열지 않는다.

```text
{"v":1,"t":"cmd","seq":1,"cmd":"GET_INFO"}                    → ack ok, evt INFO proto 1.1, caps AID, boot_id = B
{"v":1,"t":"cmd","seq":2,"cmd":"PING"}                        → ack ok
{"v":1,"t":"cmd","seq":3,"cmd":"GET_STATUS"}                  → ack ok, tel state IDLE, faults []
{"v":1,"t":"cmd","seq":4,"cmd":"ARM","bid":B,"aid":1}         → ack ok, state ARMED
{"v":1,"t":"cmd","seq":5,"cmd":"HOME","bid":B,"aid":2}        → ack ok, evt HOMING_DONE seq 5 aid 2 result OK, tel state HOMED
```

**② 젯슨에서 확인.** 브리지를 결선 설정(`--config jetson/config.bench.json`)으로 띄운다.

- **세션 기록(sys):** `INIT_START`, `INIT_ESTOP_NOT_WIRED`, `INIT_OK:HOMING`, `HOMING_OK` 순서로 남으면 통과다.
- **실패하면:** `INIT_ERROR:<사유>`가 남고, 오류 화면에 같은 사유가 나온다.

## 5. 이 문서와 같은 시점의 젯슨 변경 (참고)

- `estop_wired` 설정을 추가했다(기본 `true`, `config.bench.json`은 `false`). 이 설정은 S1 점검과 시연 메뉴 시작 점검에만 쓰인다. 동작 중 비상정지 표시와 `ESTOP` 상태는 그대로 정지 사유다.
- 상태머신 스냅숏에 `error_why`를 추가했다. 화면이 초기화 오류의 실제 사유를 보인다(`ui/src/initReason.js`).
- 시험: Jetson 410/410, UI smoke 172/172(PC, 모의). 실기에서는 확인하지 않았다.
