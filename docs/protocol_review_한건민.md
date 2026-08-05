# Teensy USB 프로토콜 실물 대조 검토 — 한건민

- 시험일: 2026-08-05
- 기준 커밋: `807fb2c` + 미커밋 작업 트리
- 대상 명세: `protocol/teensy_usb_protocol_v1.md` v1.0a 작업본
- 입력: `protocol/examples/commands.jsonl` 21줄
- 실물: Teensy 4.1, Windows `COM6`, 현재 작업 트리의 `teensy/saver_teensy.ino`
- 비교 대상: `jetson/simulators/fake_teensy.py`

> 이 문서는 차이와 변경 제안만 기록한다. 공동 소유인 `protocol/`은 이번 작업에서
> 수정하지 않았다. 아래 변경안은 신상현에게 먼저 공유하고 합의한 뒤 반영해야 한다.
> 작업 시작 때 `protocol/teensy_usb_protocol_v1.md`에는 앞선 작업의 확정 임계값
> 변경이 이미 미커밋 상태로 존재했다. 이번 리뷰가 만든 변경은 아니지만, 이 기존
> diff도 커밋 전에 신상현에게 공유해야 한다.
>
> SAVER는 연구용 시제품이며 이 시험은 USB 통신 시험이다. 센서 정확도, 인터록,
> 모터·펌프·팬 구동 또는 의료 성능을 입증하지 않는다.

## 1. 결론

1. 실물은 정상 흐름에서 발생한 ACK를 모두 명세의 200 ms 이내에 보냈다. 첫 PING은
   48 ms, 나머지 ACK는 관찰상 0~2 ms였다.
2. 1~14번의 ACK 상태 전이는 실물과 fake가 대체로 같았다. 그러나 실물은 아직
   구현되지 않은 HOME·음압 동작을 즉시 완료로 보고했고, `SCAN_RESULT`와
   `TRIAL_RESULT`는 보내지 않았다.
3. 가장 명확한 응답 차이는 다음 세 가지다.
   - 15번 `RESET_FAULT`: 실물은 거부했지만 fake는 성공시키고 `IDLE`로 바꿨다.
   - 19번 버전 불일치 뒤 telemetry: 실물 `faults:[]`, fake
     `faults:["3-CM-001"]`.
   - 21번 깨진 JSON: 실물은 위험 오류 `3-CM-001`, fake는 명세대로 주의 오류
     `1-CM-003 PARSE_FAIL`을 보냈다.
4. 실물과 fake 모두 수정할 부분이 있으며, 명세 자체에도 서로 양립할 수 없는 규칙과
   빠진 오류·이벤트 정의가 있다. 한쪽 코드만 먼저 고치면 다시 어긋난다.

## 2. 시험 조건과 방법

### 2.1 실물 Teensy

- 모터 동력 전원은 분리돼 있다는 사용자 제공 조건을 전제로 했다. 이를 계측기로
  독립 확인하지는 못했다.
- 같은 컴파일 산출물을 다시 업로드해 `BOOT`부터 시작했다. 업로드 종료 코드는 0이고
  `COM6` 재열거를 확인했다.
- 각 입력은 파일 내용을 바꾸지 않고 LF 한 개를 붙여 순서대로 전송했다.
- HOME, VACUUM_ON, STIFFNESS_SCAN, TRIAL_COMPRESSION 뒤에는 fake의 완료 이벤트가
  나올 시간을 각각 확보했다. 최대 대기시간은 1.75 s로 두어 실물의 2 s 수신
  워치독을 넘기지 않았다. 비교 입력에 별도 PING은 삽입하지 않았다.
- ACK는 `seq`로, `evt`와 `err`는 송신 직후의 캡처 구간으로 귀속했다. 주기 telemetry는
  비동기이므로 정확한 줄 순서나 개수를 성공 조건으로 사용하지 않았다.
- 첫 `GET_STATUS`에서 실물은 `estop:false`를 보고했다. 이는 E-stop 회로의 전기적
  안전성과 극성을 검증한 결과가 아니라 해당 순간의 디지털 읽기 결과일 뿐이다.

### 2.2 fake Teensy

- 새 인스턴스를 `IDLE`, 20 Hz, 고장 주입 없음, 난수 seed 12345로 시작했다.
- 실물과 같은 입력과 캡처 시간을 사용했다.
- 시험 PC는 Windows이고 WSL 배포판이 없어 `pty`/`termios` 기반 실행 파일 전체를
  구동할 수 없었다. 대신 `fake_teensy.py`의 실제 `handle()`, `ack()`, `err()`,
  `telemetry()` 및 `threading.Timer` 완료 콜백을 그대로 호출했다. 깨진 JSON은
  `read_loop()` 349~353행과 같은 분기로 넣었다.
- 따라서 fake의 명령 처리·상태·타이머 출력은 실행해 비교했지만, PTY 생성,
  OS 직렬 I/O, CR/LF 프레이밍과 재연결 경로는 이번 PC에서 실행 검증하지 못했다.

## 3. 21줄 응답 대조

JSON 키 순서는 비교에서 제외했다. `tel` 개수는 캡처 경계에 따라 달라질 수 있다.

| # | 명령 | 실물 Teensy | fake Teensy | 명세 판정 |
|---:|---|---|---|---|
| 1 | `PING` | 재부팅 직후 별도 `BOOT` 이벤트, 이어서 `ok:true, state:IDLE` (48 ms) | 동일 | PING ACK는 일치. BOOT는 PING의 응답이 아님 |
| 2 | `GET_STATUS` | ACK 뒤 즉시 tel. 측정값은 생략되고 상태·bool·`faults`만 존재 | ACK 뒤 tel. 임의의 압력·PPG·온도 등 포함 | ACK 흐름은 일치. 값 차이는 미구현 실물과 합성 fake의 차이 |
| 3 | `ARM` | `ARMED` | `ARMED` | 일치. 실제 인터록 검사는 아직 구현되지 않음 |
| 4 | `HOME` | ACK는 `ARMED`, 약 2 ms 뒤 `HOMING_DONE`, tel은 `HOMED` | ACK는 `ARMED`, 약 812 ms 뒤 이벤트, 전후 tel은 `ARMED→HOMED` | 형식은 일치. 실물은 원점 센서 확인 없는 완료 이벤트 |
| 5 | `VACUUM_ON -30` | ACK `HOMED`, 약 2 ms 뒤 `VACUUM_OK` | ACK `HOMED`, 약 1.20 s 뒤 이벤트 | 실물은 압력 확인 없는 완료 이벤트. `-30` 자체도 확정값과 충돌 |
| 6 | `STIFFNESS_SCAN` | ACK만, `SCAN_RESULT` 없음 | ACK 뒤 약 1.52 s에 8점 결과 | 실물 미구현으로 명세의 결과 이벤트 누락 |
| 7 | `SET_CP_POINT` | `CPR_READY` | `CPR_READY` | ACK는 일치. 실물의 물리 이동은 미구현 |
| 8 | `SET_TARGET 110/50/600` | `CPR_READY` | `CPR_READY` | 일치하지만 미확정 의료·힘 값을 예제가 활성화함 |
| 9 | `TRIAL_COMPRESSION` | ACK만, `TRIAL_RESULT` 없음 | ACK 뒤 약 1.02 s에 이벤트 | 실물 미구현으로 명세의 결과 이벤트 누락 |
| 10 | `START_CPR` | `CPR_RUNNING`; tel에 압박 측정 필드 없음 | `CPR_RUNNING`; 합성 압박값 포함 | 상태 ACK는 일치. 실물 모터 구동·측정은 수행되지 않음 |
| 11 | `PAUSE_CPR` | `CPR_PAUSED` | 동일 | 일치 |
| 12 | `RESUME_CPR` | `CPR_RUNNING` | 동일 | 일치 |
| 13 | `STOP_CPR` | `HOMED` | 동일 | 일치 |
| 14 | `VACUUM_OFF` | ACK `HOMED` + `VACUUM_RELEASED` | 동일 | 응답은 일치. 이벤트 스키마와 상태 전이는 명세에 정식 정의가 없음 |
| 15 | `RESET_FAULT` | `ok:false, err:"", why:FAULT_PERSISTS`; 상태 `HOMED` | `ok:true, state:IDLE` | 명세상 허용 상태는 `FAULT`. fake가 위반. 실물도 빈 오류 코드는 부적합 |
| 16 | `SET_TEL_RATE 10` | ACK `HOMED`; 관찰상 10 Hz로 감소 | ACK `IDLE`; 10 Hz로 감소 | ACK 상태 차이는 15번에서 파생. rate 동작은 일치 |
| 17 | 중복 seq 16 | 재실행 없이 성공 ACK `HOMED` | 성공 ACK `IDLE` | 중복 억제는 일치. 원 ACK 재전송 의미는 이 사례로 판별 불가 |
| 18 | 미지 명령 | `ok:false, 1-CM-005, UNKNOWN_COMMAND` | 동일, 단 상태 흐름은 fake `IDLE` | 양쪽의 오류 코드가 명세 코드표에 없음 |
| 19 | 버전 2 `PING` | ACK 없이 `3-CM-001`, 이후 `FAULT`, `faults:[]` | ACK 없이 같은 err, 이후 `FAULT`, `faults:["3-CM-001"]` | 안전 상태는 일치. ACK 및 latched 오류와 `faults[]` 관계는 명세가 불명확 |
| 20 | `SET_TEL_RATE 999` | `ok:false, 1-CM-004, BAD_RATE` | 동일 | 양쪽의 오류 코드가 명세 코드표에 없음 |
| 21 | 깨진 JSON | `3-CM-001`, sev 3, latched true | `1-CM-003`, sev 1, latched false, `PARSE_FAIL` | 명세 §3·§8에 따라 fake가 맞고 실물이 위반 |

실물의 대표 원문은 다음과 같다.

```json
{"v":1,"t":"ack","seq":15,"ok":false,"err":"","why":"FAULT_PERSISTS"}
{"v":1,"t":"err","ms":10409,"code":"3-CM-001","sev":3,"latched":true,"why":"VERSION_MISMATCH"}
{"v":1,"t":"tel","ms":10444,"state":"FAULT","estop":false,"limit_top":false,"limit_bottom":false,"vac_leak":false,"faults":[]}
{"v":1,"t":"err","ms":11125,"code":"3-CM-001","sev":3,"latched":true,"why":"VERSION_MISMATCH"}
```

fake의 대응 원문은 다음과 같다.

```json
{"t":"ack","seq":15,"ok":true,"state":"IDLE","v":1}
{"t":"err","ms":9773,"code":"3-CM-001","sev":3,"latched":true,"why":"VERSION_MISMATCH","v":1}
{"t":"tel","ms":9856,"state":"FAULT","vac_kPa":-29.37,"ppg_sqi":0.87,"ppg_hr_bpm":70.92,"spo2_pct":97.59,"temp_C":28.0,"fan_pct":0.0,"estop":false,"limit_top":false,"limit_bottom":false,"vac_leak":false,"faults":["3-CM-001"],"v":1}
{"t":"err","ms":10457,"code":"1-CM-003","sev":1,"latched":false,"why":"PARSE_FAIL","v":1}
```

## 4. 실물과 fake의 응답 차이

### 4.1 실물에 없고 fake에 있는 동작

- `STIFFNESS_SCAN`의 `SCAN_RESULT`와 `TRIAL_COMPRESSION`의 `TRIAL_RESULT`.
- 압박·음압·PPG·온도·팬의 합성 telemetry 값.
- version mismatch 뒤 활성 고장 목록의 `3-CM-001`.
- HOME 0.8 s, 음압 도달 1.2 s, 스캔 1.5 s, 시험 압박 1.0 s의 모의 지연.

### 4.2 실물과 fake가 서로 다르게 처리한 동작

- FAULT가 아닌 `HOMED`에서 `RESET_FAULT`: 실물 거부, fake 성공.
- 깨진 JSON: 실물은 버전 불일치 위험 오류, fake는 파싱 실패 주의 오류.
- 15번 뒤 상태가 실물 `HOMED`, fake `IDLE`이므로 16~18번 ACK의 상태도 달라졌다.
- 실물은 HOME·VACUUM 완료 이벤트를 약 2 ms에 보냈고 fake는 타이머 뒤 보냈다.

### 4.3 비교는 했지만 동등성 판정에서 제외한 것

- `ms`, 정확한 telemetry 줄 수와 난수 센서값.
- JSON 객체의 키 순서.
- 실제 센서가 없는 실물의 필드 생략과 시험용 fake의 합성값 자체. 다만 fake의 기본
  음압 약 -29.5 kPa는 현재 확정값 -22.5 kPa와 별도로 수정해야 한다.

## 5. 명세 위반 주체

### 5.1 Teensy 구현 위반 또는 미구현

1. **깨진 JSON 오분류:** `handleCommand()`는 `v` 파싱 실패도 version mismatch로
   취급해 `3-CM-001`/sev 3/latched를 보낸다. 명세는 `1-CM-003`으로 줄만 버리도록
   한다 (`saver_teensy.ino:150-155`, 명세 §3·§8). 현재 파서는 JSON 문법 전체를
   검증하지 않고 키 문자열을 찾으므로, 구조가 깨졌어도 `v`/`seq`/`cmd`가 검색되면
   명령을 실행할 가능성도 있다.
2. **센서 확인 없는 완료 이벤트:** HOME은 원점 센서를 확인하지 않고
   `HOMING_DONE`, VACUUM_ON은 압력을 읽지 않고 `VACUUM_OK`, VACUUM_OFF는 실제
   해제를 확인하지 않고 `VACUUM_RELEASED`를 보낸다. 현재 통신 골격의 `[구현]`
   자리라는 사실은 확인했지만 명세상 완료 이벤트로는 사용할 수 없다.
3. **필수 결과 누락:** `SCAN_RESULT`, `TRIAL_RESULT`가 없다.
4. **latched fault 표현 불일치 후보:** version mismatch err는 `latched:true`지만
   `g_fault`에 저장하지 않아 직후 telemetry가 `faults:[]`이다. 활성 latched 오류라면
   불일치로 보이지만, 명세가 `latched:true`와 `faults[]`의 관계를 명시해야 확정할 수 있다.
5. **위험 오류 순서:** version mismatch 경로는 코드상 `sendErr()` 뒤에
   `motorsOff()`를 호출한다 (`saver_teensy.ino:152-155`). 명세 §4.5와 D-08의
   “정지 먼저, 보고 나중”을 위반한다. 이 순서는 코드로 확인했으며 핀 파형으로
   실측하지는 않았다.
6. **잘못된 RESET 거부 형식:** FAULT가 아닌 상태에서도 handler에 들어가며,
   `g_fault`가 비어 `err:""`인 ACK를 보낸다.
7. **측정 불가 bool의 정상값 위장:** 리미트와 `vac_leak`을 아직 읽지 않지만
   telemetry에 항상 `false`로 보낸다. 명세 §6의 “측정 불가 필드 생략” 취지와 다르다.
8. **미확정 의료 타이밍 활성화:** `T_COMM_MS=30000`이 실제 CPR_RUNNING 통신 두절
   경로에서 사용된다. 명세 §9·§11과 저장소 규칙상 의학 근거 전에는 활성화하면 안 된다.
   값은 이번 작업에서 바꾸지 않았으며 한성윤 결정이 필요하다.
9. **VACUUM_OFF 상태 불일치:** `VACUUM_HOLD` 또는 `CPR_READY`에서 호출해도
   논리 상태를 `HOMED`로 바꾸지 않는다. fake는 이 두 상태에서 `HOMED`로 전이한다.

### 5.2 fake 구현 위반 또는 모델 불일치

1. **RESET_FAULT 상태 제한 위반:** `HOMED`에서도 성공시키고 `IDLE`로 바꾼다
   (`fake_teensy.py:271-276`).
2. **수신 워치독 없음:** 명세 §9의 1 s PING/2 s Teensy 워치독을 모델링하지 않는다
   (`fake_teensy.py:357-363`). 따라서 사람이 천천히 입력할 때 실물만 FAULT가 된다.
3. **E-stop 주입 상태 오류:** `ESTOP`으로 둔 직후 공통 sev 3 처리에서 `FAULT`로
   덮어쓴다 (`fake_teensy.py:306-309`). 명세 §7의 ESTOP 최우선과 다르다.
4. **확정 음압 불일치:** 정상 흡착을 약 -29.5 kPa로 합성한다. 현재 확정값은
   -22.5 kPa다.

### 5.3 양쪽 구현의 공통 문제

1. **상태 제한 누락:** 양쪽 모두 `DISARM`, `SET_TARGET`, `STOP_CPR`의 명세상 허용
   상태를 충분히 검사하지 않는다. 특히 STOP_CPR은 잘못된 상태에서도 성공 ACK를 보낸다.
   “ARMED 이후/이상”처럼 명세 자체가 모호한 항목은 §6에서 먼저 정리해야 한다.
2. **중복 ACK를 캐시하지 않음:** 중복 seq이면 원래 성공/거부 결과 대신 현재 상태의
   무조건 성공 ACK를 새로 만든다. 원 명령이 거부됐다면 재전송 결과가 성공으로 뒤집힌다
   (`saver_teensy.ino:163-165`, `fake_teensy.py:186-189`).
3. **오류 코드 재사용:** 목표가 설정되지 않은 START를 양쪽 모두 명세상 “목표 깊이
   도달 실패”인 `2-CU-012`로 보고한다. 별도 코드가 필요하다.
4. **ESTOP에서 VACUUM_OFF 거부:** 양쪽 모두 ESTOP에서는 PING/GET_STATUS 외 명령을
   먼저 차단하므로 VACUUM_OFF도 거부한다. 이는 §5의 “모두 허용” 및 §7의
   “VACUUM_OFF 또는 수동 조작으로 해제”와 충돌한다.
5. **입력 검증 공백:** seq 범위, 평면 객체, null, 소수 자릿수, 필수 파라미터를
   명세대로 검사하지 않는다. 또한 양쪽 모두 CR을 버리거나 `.strip()`하므로 명세가
   금지한 CRLF를 코드상 수락한다. 이번 실기 입력은 LF만 사용해 CRLF는 실측하지 않았다.
6. **복수 fault 모델 없음:** 명세는 `faults[]` 배열이지만 양쪽 모두 활성 fault를
   사실상 한 개만 저장한다.

### 5.4 양쪽 구현보다 명세가 먼저 정리돼야 하는 부분

- 버전 불일치 때 양쪽 모두 ACK 없이 err만 보낸다. 이는 §3의 동작에는 맞지만
  §4.2의 “모든 cmd에 ACK”와 충돌하므로 어느 쪽 위반인지 현재 계약만으로 확정할 수 없다.
- `1-CM-004 BAD_RATE`, `1-CM-005 UNKNOWN_COMMAND`, `3-MU-002 ESTOP_ACTIVE`가
  코드에는 있지만 §8 오류 코드표에 없다. 양쪽 코드가 계약보다 앞서 있다.
- VACUUM_ON의 허용 상태는 §5 표의 `ARMED`와 §7 상태도·§10 정상 흐름의 `HOMED`가
  충돌한다. 양쪽 구현은 `HOMED`를 요구한다.

## 6. 명세 자체에서 고쳐야 할 부분 — 신상현 합의 요청

아래는 `protocol/` 변경 전 합의 항목이다. 우선순위 순이다.

1. **예제의 미확정·폐기 값 제거:** `commands.jsonl`과 §10의 `-30 kPa`를 확정값
   `-22.5 kPa` 정책과 맞춘다. §11에서 깊이와 속도가 모두 미확정이므로
   `bpm:110`, `depth_mm:50`, 힘·스캔 값은 실물 실행 예제로 두지 말고 시뮬레이션
   전용이라고 명시하거나 기능 거부 예제로 바꾼다. 펌웨어와 fake의 기본 110 cpm도
   별도 합의 근거가 명세에 들어오기 전에는 확정값으로 취급하지 않는다.
2. **완료 ACK와 완료 이벤트 의미 분리:** HOME/VACUUM/SCAN/TRIAL의 ACK는 요청 수락인지,
   완료인지 명시하고 완료 이벤트의 필수 필드·타임아웃·실패 응답을 표로 정의한다.
3. **버전/파싱 실패 응답 확정:** 유효 JSON의 버전 불일치는 ACK+err인지 err만인지,
   깨진 JSON처럼 seq를 신뢰할 수 없을 때는 ACK 예외인지 명시한다.
4. **허용 상태 표 통일:** 특히 VACUUM_ON은 `HOMED` 여부를 확정한다. DISARM,
   SET_TARGET, STOP_CPR, RESET_FAULT, VACUUM_OFF의 모든 상태별 결과와 전이를 적는다.
   CPR_RUNNING/ESTOP에서 VACUUM_OFF를 허용하면 압박 정지와 해제 순서도 필요하다.
5. **오류 코드 완성:** bad rate, unknown command, E-stop active, 목표 미설정,
   잘못된 상태, 필수 파라미터 누락·범위 오류 코드를 §8에 등록한다. `2-CU-011` 하나를
   NOT_ARMED/NOT_HOMED/NOT_FIXED/NOT_READY에 모두 쓰는지도 결정한다.
6. **중복 ACK 정의:** “ACK를 다시 보낸다”를 원래 ACK 바이트/의미의 캐시·재전송으로
   명시한다. 상태가 바뀌어도 거부가 성공으로 변하면 안 된다.
7. **RESET_FAULT 계약:** 원인 해소와 latch 해제를 어떻게 구별·검사하는지, FAULT가
   아닐 때의 오류, 성공 뒤 상태를 정의한다.
8. **telemetry 유효성:** 측정되지 않은 bool도 생략할 수 있도록 필드 선택성을 명시하고,
   `latched:true` 오류와 `faults[]`의 관계를 정의한다.
9. **배열 규칙 수정:** §3은 문자열 배열만 허용하지만 `SCAN_RESULT.stiff`는 숫자 배열이다.
   `faults:string[]`, `stiff:number[]`처럼 허용 위치와 길이를 명시한다.
10. **CPR_READY 조건 통일:** §7 설명은 압박점과 목표값이 모두 확정된 상태라 하지만
    상태도와 양쪽 구현은 SET_CP_POINT만으로 진입한다. 목표 설정 전 READY를 허용할지
    별도 상태/플래그를 둘지 결정한다.
11. **E-stop 책임 문구 정리:** §1 표의 “Teensy 즉시 모터 차단”을 D-09와 맞게
    “독립 하드웨어 회로가 차단, Teensy는 감지·보고”로 고친다. ESTOP 음압 유지·해제는
    §7에서 규범처럼 쓰면서 §11에서는 미확정이므로 한쪽으로 통일한다.
12. **T_comm 플레이스홀더:** §9의 30 s는 의학적 미확정인데 활성 기본값처럼 적혀 있다.
    실물 펌웨어도 현재 이 값을 CPR_RUNNING 두절 경로에서 실제 사용한다. 저장소 규칙대로
    0/비활성으로 둘지, 연구용 시험값으로만 분리할지 한성윤 근거와 함께 결정한다.
13. **프레이밍·파라미터 검증표:** CRLF 금지의 실제 처리, seq 범위, 필수 필드,
    수치 범위, unknown field, null/배열/중첩 객체의 거부 또는 무시 규칙을 명령별로 적는다.
    현재 양쪽 구현은 CRLF를 수락하므로 금지를 유지할지 수락으로 계약을 바꿀지도 결정한다.
14. **이벤트 레지스트리 추가:** `BOOT`, `HOMING_DONE`, `VACUUM_OK`,
    `VACUUM_RELEASED`, `SCAN_RESULT`, `TRIAL_RESULT`의 발생 조건과 payload를 정의한다.
15. **예제 파일 성격 명시:** 21줄이 하나의 순차 시나리오인지 독립 예제 모음인지,
    정상/의도적 오류 줄을 표시한다. 현재 15번 RESET_FAULT는 순차 실행 시 명세상
    잘못된 상태인데 정상 예제처럼 놓여 있다.
16. **수동 시험 heartbeat 규칙:** 완료 이벤트를 기다리며 사람이 한 줄씩 입력하면 실물의
    2 s 워치독을 넘을 수 있다. 수동 절차에는 1 s PING을 병행하도록 적고, exact fixture
    회귀시험은 각 줄 간격을 2 s 미만으로 자동화하도록 구분한다.

## 7. 변경 권장 순서

1. 이 문서를 신상현에게 공유하고 §6의 계약 변경을 합의한다. 의료값·T_comm·ESTOP
   음압 정책은 한성윤 확인 전 확정하지 않는다.
2. 합의된 내용으로 `protocol/teensy_usb_protocol_v1.md`와 example을 먼저 수정한다.
3. 같은 변경 묶음에서 Teensy handler와 fake를 함께 맞춘다.
4. `commands.jsonl` 전체를 양쪽에 실행하는 자동 회귀시험을 추가한다. 중복된 **거부**
   명령, 깨진 JSON, version mismatch, E-stop 우선순위, 2 s 워치독도 포함한다.
5. protocol 변경 규칙에 따라 `jetson/tests/test_all.py`와
   `teensy/test_saver_proto.cpp`를 모두 실행한다.

## 8. 실행 결과와 검증 한계

### 실행한 주요 명령/동작

- Arduino CLI로 기존 컴파일 산출물을 `COM6`에 재업로드해 상태를 초기화했다.
- PowerShell `System.IO.Ports.SerialPort`로 `commands.jsonl` 21줄을 LF 단위로
  순차 전송하고 ACK/evt/err/tel을 분리 수집했다.
- 설치된 Python 3.7.2로 `fake_teensy.py`의 실제 command handler와 timer를 호출해
  동일 21줄을 비교 실행했다.
- `git diff -- protocol/teensy_usb_protocol_v1.md`와 `git status --short`로 공동 소유
  파일 및 작업 트리 상태를 읽기 전용 확인했다.

### 시험 결과 요약

- 실물: 21줄 전송 완료, 업로드 성공, 정상 ACK는 모두 200 ms 이내.
- fake handler: 21줄 실행 완료.
- 실물과 fake의 주요 차이 및 명세 판정은 §3~§6과 같다.
- Windows에 PC용 `g++`와 WSL 배포판이 없어 저장소의 전체 PC 시험 묶음은 이번
  환경에서 실행하지 못했다. 이 비교는 해당 단위시험의 통과를 뜻하지 않는다.

### 가정 및 실물로 검증하지 못한 동작

- 모터 동력 전원이 분리돼 있다는 조건은 사용자가 제공했으며 계측하지 않았다.
- 실물에서 확인한 것은 USB 업로드, JSON Lines 수신, 응답 및 논리 상태 전이뿐이다.
- 모터 EN/PWM의 실제 전압, 모터·펌프·팬 동작, HOME 이동, 음압 도달·해제,
  스캔·시험 압박, 엔코더·홀·FG, 리미트, 압력·로드셀·변위·전류 ADC,
  MAX30102, DS18B20은 검증하지 않았다.
- fake의 센서값은 합성값이며 실물 센서 성능의 근거가 아니다.
- 예제의 110 cpm, 50 mm, 600 N을 Teensy가 논리 목표로 받아들인 것만 확인했다.
  이 값의 의학적 타당성, 실제 도달 여부와 안전성은 검증하지 않았다.
- CPR_RUNNING에서 통신을 30 s 끊는 `T_comm` 경로는 실행하지 않았다. 현재 활성값의
  동작과 안전성을 실물로 검증하지 않았으며, 의학 근거가 확정됐다는 뜻도 아니다.
- version mismatch에서 “정지 후 보고”의 핀/시리얼 시간 순서는 코드에서 위반을
  확인했지만 오실로스코프로 실측하지 않았다.
- 시험 종료 후 host 입력이 끊기면 실물은 코드상 약 2 s 뒤 통신 두절 FAULT로
  전이한다. 종료 뒤의 추가 FAULT 메시지는 21번 명령 응답에 포함하지 않았다.
