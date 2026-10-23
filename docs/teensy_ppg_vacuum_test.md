# 젯슨 UI 시연 유닛 연동: 손가락 센서(PPG), 음압, 압박·강성 측정 (한건민 Codex 전달용)

작성: 2026-10-23 밤, 신상현(Claude Code). 대상은 한건민의 Codex(`teensy/`)다. 아래 "Codex 프롬프트" 블록을 그대로 붙여 넣는다.

- **펌웨어 기준:** `codex/teensy-integration-20261023`의 `3e710c8`(10/23 23:02). 근거는 이 코드를 **읽은 결과**다. 실기에서 시험하지 않았다.
- **젯슨 기준:** `claude/saver-project-onboarding-xpmdhw`. 시연 유닛 시작 절차를 고쳤다(아래 0장, 2장).
- **고치지 않은 것:** `protocol/`, `teensy/`(AGENTS.md 소유권).

**팀장 결정(10/23 밤)**

- 압박, 흡착, 강성 측정 모두 시작은 젯슨 UI에서 사용자가 터치하고, 젯슨이 Teensy에 명령한다.
- 시연 중 정지 수단은 파워서플라이 전원 차단이다(비상정지 버튼 미연결, D-54).

## 0. 막히는 곳 (코드 대조)

| 항목 | 지금(3e710c8) | 결과 | 누가 |
|---|---|---|---|
| 시작 절차의 `HOME` | 젯슨이 `ARM → HOME → …` 순서로 보내는데 펌웨어는 `HOME`을 항상 `HOME_UNAVAILABLE_NO_VERIFIED_LIMIT`로 거부 | 흡착·압박·강성 측정 시작 불가 | **젯슨에서 해결함.** `GET_INFO` `caps`에 `CP_ZERO`(v1.1 의미의 HOME)가 없으면 `HOME`을 보내지 않는다(v1.1 §4.7-4) |
| PPG | 원격 `ARM`에서만 `ppg_.enter()`. 환자 상태 화면은 명령을 보내지 않는다 | 환자 상태 화면에 값 없음 | Teensy |
| 음압 설정값 | `VAC CONFIG`가 RAM 전용 직접 명령. 젯슨 브리지가 포트를 쥐면 넣을 수 없다 | `VACUUM_ON` → `VAC_CONFIG_REQUIRED`, `ARM` → `VACUUM_RELEASE_NOT_VERIFIED` | Teensy |
| 음압 센서 영점 | README_demo.md: 대기 개방에서 −24.60 kPa | 해제 판정 불가 → **모든 유닛의 `ARM` 거부** | Teensy(하드웨어 확인 포함) |
| 압박 원격 시작 | `START_CPR`은 `CPR_READY`에서만 받고, `CPR_READY`는 직접 명령 `CP PREPARE`로만 간다. 직접 명령을 쓰면 JSON이 `LOCAL_CONTROL_ACTIVE`로 거부되고 USB 재연결은 준비를 지운다 | 젯슨 UI로 압박·강성 측정 시작 불가 | Teensy |
| 압박 설정 잠금 | `BENCH_DRIVE_CONFIRMED`, `VESC_SETTINGS_AND_TIMEOUT_VERIFIED`, `CURRENT_CONTROL_SETTINGS_VERIFIED`가 false, `START_TOLERANCE_MM` 등 되먹임 한계가 0 | 직접 명령으로도 압박이 시작되지 않는다 | **건민의 검증 절차.** 시연을 위해 풀어 달라고 요청하지 않는다 |

명세 §5 표의 허용 상태는 `VACUUM_ON`이 `HOMED`, `START_CPR`이 `CPR_READY`다. `ARMED`에서 받는 것은 리미트가 없는 수동 배치
펌웨어의 시연 동작이며, 받아들일지는 Teensy가 정한다(젯슨 명령은 요청). 리미트를 달아 `caps`에 `CP_ZERO`를 넣으면 젯슨은 다시 `HOME`을 보낸다.

## Codex 프롬프트

```text
[작업] 젯슨 UI 시연 유닛 연동: 손가락 센서(PPG), 음압 모터·센서, 압박·강성 측정

배경
- 저장소 SAVER. 작업 브랜치 codex/teensy-integration-20261023(3e710c8). 진입점 teensy/saver_teensy.ino → teensy/src/demo/demo_app.h.
- 팀장 결정(10/23): 압박, 흡착, 강성 측정은 모두 젯슨 UI 에서 사용자가 터치해 시작하고, 젯슨이 Teensy 에 명령한다.
  시연 중 정지 수단은 파워서플라이 전원 차단이다(비상정지 버튼 미연결).
- 젯슨은 결선 시연 모드(브리지 --unit-demo, jetson/config.bench.json)의 "환자 상태", "흡착", "압박", "강성 측정" 화면을 쓴다.
- 젯슨 변경(신상현, 브랜치 claude/saver-project-onboarding-xpmdhw, docs/teensy_ppg_vacuum_test.md):
  GET_INFO caps 에 CP_ZERO 가 없으면 HOME 을 보내지 않는다. 지금의 HOME 거부와 caps ["AID","MOT"] 는 그대로 둔다.
- 순서: 1) PPG, 음압(2.1~2.7) 2) 압박 원격 시작(2.8). ARM 이 음압 해제 판정을 요구하므로 압박도 2.2, 2.3 이 먼저다.
- 규칙은 AGENTS.md. protocol/, jetson/, ui/ 는 읽기만 한다. 명세에 없는 임계값을 만들지 않는다. 성공을 꾸며 보내지 않는다.
- 먼저 지금 코드가 아래 요구를 이미 만족하는지 확인한다. 만족하는 항목은 바꾸지 말고 확인 결과만 보고한다.

1. 젯슨이 보내는 것과 기대하는 응답 (protocol/teensy_usb_protocol_v1.md v1.1)
- 연결마다 GET_INFO(seq 만) → ack ok + evt INFO {proto "1.1", caps 에 "AID", boot_id, last_seq, last_aid}.
  이후 PING 1 s 간격. 모든 cmd 에 200 ms 안 ACK, 젯슨은 2회 재전송 뒤 실패 처리.
- 모든 [시작]은 먼저 GET_STATUS → ack ok, 1 s 안에 tel(같은 tn 의 mot). state 가 시작 가능한 상태이고 faults 가 비어 있어야 진행.
  실패로 보는 것: ACK 거부(why 를 화면에 그대로 보임), sev 3 err, tel state FAULT/ESTOP, 링크 두절.
- 환자 상태: 명령 없음. tel 의 ppg_hr_bpm, spo2_pct 만 본다.
- 흡착: (IDLE) ARM{bid,aid} → VACUUM_ON{bid,aid,"target_kPa":-22.5} → 30 s 안에 evt VACUUM_OK{VACUUM_ON 의 seq,aid}
  또는 state VACUUM_HOLD. 30 s 를 넘기면 젯슨이 VACUUM_OFF 를 보내고 실패(VACUUM_TIMEOUT). evt VACUUM_FAIL 도 실패.
  [해제]·화면 이탈·UI 끊김: VACUUM_OFF{bid,aid} → ack ok. 해제가 확인되면 evt VACUUM_RELEASED{seq,aid,vac_kPa}, state IDLE.
- 압박: (IDLE) ARM → SET_TARGET{bid,aid,"bpm":30,"depth_mm":30,"force_max_N":600} → START_CPR{bid,aid}. ACK ok 면 압박 중 화면.
  (팀장 10/23: 시연 속도가 너무 빨라 30 mm / 30 회/분. 설계값 110 회/분, 벤치 시험 설정 50 mm 는 그대로)
  [정지]·화면 이탈: STOP_CPR{bid,aid,"reason":"USER_STOP"}. 젯슨 실패 처리 중에는 "reason":"SAFETY".
  장치가 스스로 CPR_RUNNING 을 벗어나면 "장치가 압박을 멈췄습니다"로 끝낸다.
- 강성 측정: 압박과 같은 순서. START_CPR 뒤 30 s 안에 CPR_RUNNING 이 아니면 실패(NOT_RUNNING).
  처음 2주기를 버리고 10주기(30 회/분에서 20 s, 버리는 주기까지 약 24 s) 동안 tel 의 ms, cp_force_N, cp_depth_mm 로 주기마다
  k = 최대 힘 ÷ 최대 깊이(N/mm)를 구한 뒤 STOP_CPR{"reason":"END"}. 두 값이 오지 않으면 실패(STIFF_NO_DATA).
- 30 s 는 젯슨 화면 대기 한도이며 안전 판단이 아니다. 실측이 더 걸리면 신상현에게 알린다(젯슨 쪽에서 늘린다).

2. 할 일
2.1 PPG 를 ARM 과 무관하게 관측한다 (필수)
- 부팅 때(압박 정지 상태) PPG 취득을 한 번 시작한다. 관측만 하고 출력은 없다. 원격 ARM 의 PPG 시작과 겹치지 않게 정리한다.
- tel: 유효할 때만 ppg_hr_bpm, spo2_pct(소수 2자리). 손가락 없음, 창 미완성, 진단 모드는 필드 생략. 0 을 보내지 않는다.
- PPG 미검출(PPG_NOT_FOUND)과 손가락 없음은 이번에는 faults 나 err 에 넣지 않는다(BENCH_LOG 만).
  젯슨 시작 점검이 faults 가 비어 있기를 요구하기 때문이다. 2-PU-002 보고는 따로 합의한다.
- MAX30102(Wire, 18/19)가 다른 I2C 장치와 버스를 함께 쓰면 Wire.begin()/setClock() 이 그 장치 읽기에 영향이 없는지 확인한다.

2.2 음압 센서: 대기압에서 0 근처 (필수, 하드웨어 확인 포함)
- 대기 개방 −24.60 kPa 가 남아 있으면 해제 판정이 서지 않아 ARM 이 계속 VACUUM_RELEASE_NOT_VERIFIED 다(모든 유닛).
- 원인을 측정으로 찾는다: 센서 공급 전압(5 V 인지), 센서 OUT 전압, A8 전압, 분압 저항(4.7 k/10 k), ADC 기준.
  계산: 대기압 출력 4.5 V → A8 약 3.06 V. −24.6 kPa 는 센서 출력 약 3.52 V(A8 약 2.39 V)에 해당한다.
  센서가 비율 출력이라면 공급이 약 3.9 V 일 때 이 값이 나온다(추정, 측정으로 확인).
- 측정값과 고친 내용을 README_demo.md 에 남긴다. 영점 캡처(RelativePressureZero)로 제어에 쓰는 값을 덮지 않는다.
- 확인: 블로워 OFF, 관 대기 개방에서 |vac_kPa| 가 데이터시트 허용오차(±2 %FSS, 코드의 OUTPUT_ALLOWANCE_V, 약 ±2 kPa) 안.
- tel vac_kPa: 게이지압, 음압은 음수, 소수 2자리. 센서가 무효하면 생략(0 금지).

2.3 음압 설정값을 젯슨이 포트를 쥔 상태에서도 쓸 수 있게 (필수)
- 지금 VAC CONFIG(듀티, 최대 연속 ON, 도달 안정, 해제 대역, 해제 안정)는 RAM 전용이라 브리지 연결 중에는 넣을 수 없다.
- 방법 하나를 고른다.
  (a) 직접 명령(예: VAC SAVE)으로 EEPROM 에 저장하고 부팅 때 읽는다. 판 번호와 체크섬을 두고, 기존 EEPROM 사용 영역
      (압박 보정, 부팅 번호)과 겹치지 않게 한다. 없거나 깨졌으면 미설정(0)으로 부팅한다.
  (b) 실측값을 컴파일 상수로 둔다.
- 값은 실측한 것만 쓴다. 해제 대역·해제 시간은 명세 A.7.1 대로 블로워 정지·대기 상태의 영점과 잡음, VACUUM_OFF 뒤 압력
  복귀 곡선을 재서 정한다. 근거와 측정일을 README_demo.md 에 적는다. 모르면 비워 두고 그렇다고 보고한다.
- vac_release_timeout_ms(2-VU-008)는 미설정이면 판정하지 않는다(지금처럼).

2.4 ARM (지금 코드 유지, 확인만)
- IDLE, vac_released, configuredInputsReady(), 블로워 고장 없음일 때 ARMED. 부팅 직후 해제 안정 시간이 지나기 전의 ARM 거부는 정상이다.
- PHYSICAL_ESTOP_POWER_CUT_USER_CONFIRMED: 팀장이 시연 중 정지 수단을 파워서플라이 전원 차단으로 정했다.
  이 차단이 압박 모터 드라이버(VESC)와 블로워 전원을 실제로 끊는지 확인하고 bench_config.h 주석의 근거를 갱신한다.
  끊지 못하면 false 가 맞다. 시험을 통과시키려고 값을 바꾸지 않는다.

2.5 VACUUM_ON, VACUUM_OK (지금 코드 유지, 확인만)
- ARMED·HOMED 에서 target_kPa -22.5 만 받는다. ACK 200 ms 안.
- 유지 판정(holding)이 서면 evt VACUUM_OK{seq,aid,vac_kPa} 한 번, state VACUUM_HOLD.
- 도달 실패(BLOWER_ON_TIME_LIMIT 등)는 지금처럼 블로워를 먼저 끄고 err 를 보낸다.
  (선택) 대기 중인 VACUUM_ON 이 있으면 evt VACUUM_FAIL{seq,aid,vac_kPa(유효할 때)} 도 보낸다. 젯슨 화면 사유가 "바닥 고정 실패"가 된다.

2.6 VACUUM_OFF, VACUUM_RELEASED (지금 코드 유지, 확인만)
- 블로워를 끄고 ack ok. 해제가 확인되면 evt VACUUM_RELEASED{seq,aid,vac_kPa}, state IDLE, rearm_lock false.
- 압박 중 거부(STOP_COMPRESSION_BEFORE_VACUUM_RELEASE)는 유지한다.

2.7 텔레메트리 형식 (확인)
- tel 20 Hz, 바로 뒤에 같은 tn·ms 의 mot. GET_STATUS 를 받으면 tel 과 mot 를 한 번씩 즉시.
- tel: v, t, ms, tn, boot_id, state, vac_kPa, ppg_hr_bpm, spo2_pct, cp_depth_mm(직전 사이클 최대), cp_rate_cpm,
  cp_force_N, faults([] 또는 코드). 유효하지 않은 값은 생략. estop 은 싣지 않는다(ESTOP_AUX_INSTALLED=false 유지).
  vac_leak 은 누설 판정 기준이 없으면 생략한다.
- mot: rearm_lock, vac_released 는 항상. 지금의 "hold", "settled" 는 명세 이름(head_hold_active, drv_settled)이 아니다.
  젯슨은 모르는 필드를 무시하므로 영향은 없다. 고친다면 명세 이름으로 바꾸거나, 측정할 수 없으면 생략한다.
- 한 줄 511 byte 이하, LF 하나, 실수 소수 2자리, null·NaN 금지.

2.8 압박·강성 측정을 젯슨 UI 로 시작 (2.1~2.7 다음)
- 지금은 START_CPR 이 CPR_READY 에서만 받고, CPR_READY 는 직접 명령 CP PREPARE 로만 간다. 직접 명령을 쓰면 JSON 명령이
  LOCAL_CONTROL_ACTIVE 로 거부되고, USB 재연결은 prepared_ 를 지운다. 그래서 젯슨 UI 로는 시작할 수 없다.
- 요청: 원격 START_CPR 을 ARMED(그리고 HOMED)에서도 받는다. 받을 때 CP PREPARE 와 같은 함수로 같은 검사
  (cp_.readyIssue(), 고장, 부팅 기록, start() 의 검사)를 하고, 통과하면 준비와 시작을 한 번에 한다.
  검사를 새로 만들거나 약하게 하지 않는다. 거부하면 ack why 에 그 사유를 그대로 싣는다(젯슨 화면에 보인다).
  명세 §5(START_CPR 은 CPR_READY)와 다른 시연 빌드 동작이므로 README_demo.md 와 보고에 적는다.
  같은 결과를 내는 다른 방법이 낫다고 판단하면 그 방법을 쓰고 이유를 보고한다.
- 설정 잠금(BENCH_DRIVE_CONFIRMED, VESC_SETTINGS_AND_TIMEOUT_VERIFIED, CURRENT_CONTROL_SETTINGS_VERIFIED, 되먹임 한계
  START_TOLERANCE_MM 등)은 시연을 위해 바꾸지 않는다. 한건민의 검증 절차로 풀릴 때까지 원격 START_CPR 은 그 사유로 거부된다.
- 브리지 연결 전에 직접 명령으로 하는 준비(ZERO, CAL, TARE, FORCE_CAL 등)가 USB 재연결 뒤에도 남는지 확인한다.
  남지 않으면 브리지 연결 중에는 할 수 없으므로 EEPROM 복원이나 다른 경로가 필요하다. 어느 쪽인지 보고한다.
- SET_TARGET 은 지금처럼 TARGET, FORCELIMIT 로 넘긴다(젯슨 값 30 회/분, 30 mm, 600 N 은 설정 파일값이며 펌웨어 범위 검사를 그대로 거친다).
- 압박 중 tel: cp_depth_mm, cp_rate_cpm, cp_force_N(힘 보정이 됐을 때만), ms. 강성 측정은 cp_force_N 과 cp_depth_mm 가 모두 필요하다.
- 실행 시간은 지금처럼 MAX_RUN_MS 안에서 끝난다. STOP_CPR 은 지금처럼 출력을 먼저 끈다. 2 s 통신 감시(3-CM-010)는 그대로 둔다.
- 흡착을 요청하지 않았으면 흡착 없이 시작하는 지금 동작을 유지한다. 반력으로 장치가 들릴 수 있으므로 무부하나 모사체 대상으로만 쓴다.

2.9 하지 않을 것
- HOME 을 받아들이거나 HOMING_DONE OK 를 보내지 않는다. caps 에 CP_ZERO 를 넣지 않는다(넣으면 젯슨이 HOME 을 보낸다).
- 설정 잠금, 2 s 통신 감시, 정지 명령 우선 처리, ACK 재전송 처리, 기존 인터록, ESTOP_AUX_INSTALLED 를 시연 때문에 바꾸지 않는다.

3. 시험
- PC: teensy 에서 g++ -std=c++11 -Wall -o test test_saver_proto.cpp && ./test, 그리고 teensy/tests/demo/check_main.ps1,
  check_ppg_diag_replay.ps1.
- 추가할 시험(test_main.cpp):
  (1) 부팅 직후 ARM 없이 tel 에 PPG 값(합성 fixture)이 실리고, 손가락이 없으면 필드가 빠진다.
  (2) 음압 설정 저장·복원. 없거나 깨지면 미설정이고 VACUUM_ON 은 VAC_CONFIG_REQUIRED, ARM 은 VACUUM_RELEASE_NOT_VERIFIED.
  (3) 설정이 있을 때 GET_INFO → GET_STATUS → ARM → VACUUM_ON → VACUUM_OK → VACUUM_OFF → VACUUM_RELEASED 순서와 seq·aid.
  (4) PPG 미검출이어도 faults 가 비어 있고 흡착 절차가 진행된다.
  (5) 잠금이 풀린 시험용 설정(fixture)에서 ARM → SET_TARGET → START_CPR(ARMED) → CPR_RUNNING → STOP_CPR,
      기본 설정(잠금)에서는 START_CPR 이 잠금 사유로 거부되고 출력이 없다. CP PREPARE 와 같은 검사를 쓰는지.
- 실물(사람 확인, 젯슨 브리지를 끈 상태, 같은 포트를 둘이 열지 않는다). 시리얼 터미널:
  {"v":1,"t":"cmd","seq":1,"cmd":"GET_INFO"}     → INFO proto 1.1, caps ["AID","MOT"], boot_id = B
  {"v":1,"t":"cmd","seq":2,"cmd":"GET_STATUS"}   → tel IDLE, faults [], vac_kPa 0 근처, 손가락을 올리면 ppg_hr_bpm·spo2_pct,
                                                    mot vac_released true(해제 안정 시간 뒤)
  ARM 이후는 ARMED 에서 2 s 동안 JSON 이 없으면 3-CM-010 으로 FAULT 가 되므로 손으로 치지 말고,
  1 s 간격 PING 을 함께 보내는 스크립트나 젯슨 브리지로 확인한다. 압박은 파워서플라이 차단 담당자를 두고 무부하부터.

4. 보고
- 바꾼 파일, 실행한 시험과 결과, 실측값(전압, 대기압 vac_kPa, 해제 대역·시간)과 잰 방법.
- 압박 설정 잠금 중 남은 것과, 직접 명령 준비가 USB 재연결 뒤에도 남는지.
- 가정한 것과 실물로 확인하지 못한 것을 나눠 적는다(AGENTS.md 작업 완료 보고).
- 커밋은 [teensy] 접두어. 공유 브랜치를 강제 push 하지 않는다.
```

## 1. 젯슨에서 확인하는 방법 (신상현, 펌웨어 반영 뒤)

1. 젯슨 저장소를 이 브랜치로 갱신하고 브리지를 다시 띄운다. UI 묶음(`jetson/ui_dist`)도 이번에 바뀌었으므로 키오스크도 다시 띄운다.
   설치 스크립트로 서비스를 쓴 젯슨이면 `saver-update` 뒤 `saver-stop`, `saver-start`(키오스크까지 새로).
   먼저 서비스가 시연 모드인지 본다: `systemctl cat saver-bridge | grep ExecStart` 에 `--unit-demo` 와 `config.bench.json`.
   없으면 운용 모드라 재시작한 브리지가 S1 에서 `ARM`, `HOME` 을 보낸다. 그때는
   `bash ~/SAVER/jetson/setup/setup_jetson.sh --unit-demo --config jetson/config.bench.json --skip-apt --no-claude`.
   서비스 없이 직접 띄우려면:
   ```bash
   python3 -m saver.bridge --unit-demo --serial /dev/saver-teensy --config config.bench.json
   ```
2. **환자 상태:** 손가락을 올리면 심박수·산소포화도가 나온다. 손가락을 떼면 값이 빠진다(0이 아니라 표시 없음).
3. **흡착:** [흡착 시작] → "장치 상태 확인 중" → "구동 허가 중" → "바닥에 붙이는 중" → "고정 중". 게이지에 `vac_kPa`.
   세션 기록(sys)에 `UNIT_START:vacuum`, `UNIT_VACUUM_NO_HOME:CAPS`, `UNIT_RUNNING:vacuum`.
4. **해제:** [해제] → `VACUUM_OFF`. 다시 시작하기 전에 장치가 `IDLE`로 돌아왔는지(해제 확인) 본다.
   해제가 확인되지 않아 장치가 `VACUUM_HOLD`에 남아 있으면 젯슨은 이미 고정된 것으로 보고 바로 "고정 중"을 띄운다(알려진 한계).
5. **압박·강성 측정:** 파워서플라이 차단 담당자를 두고 무부하부터. [시작] → "구동 허가 중" → "목표값 설정 중" → "압박 시작 중".
   펌웨어가 거부하면 화면에 사유가 그대로 나온다(예: 설정 잠금 `BENCH_DRIVE_LOCKED`, 시작 상태 `WRONG_CPR_STATE`).
   기록에 `UNIT_COMPRESS_NO_HOME:CAPS` 또는 `UNIT_STIFF_NO_HOME:CAPS`.
6. **실패 뒤:** 장치가 `FAULT`면 젯슨 시연 화면에는 고장 해제(`RESET_FAULT`) 버튼이 없다. 브리지를 끄고 직접 명령으로 해제하거나
   Teensy를 다시 켠다. 정지가 거부되면 화면에 "파워서플라이 전원을 차단하세요"가 나온다.

## 2. 젯슨 변경

- `jetson/saver/unit_demo.py`: 압박, 강성 측정, 흡착 시작에서 `caps`에 `CP_ZERO`가 없으면 `HOME`을 보내지 않는다(`_home`).
  시작할 수 없는 장치 상태는 명령을 보내기 전에 `STATE:<상태>`로 끝낸다(이전과 같은 결과).
- `ui/src/units.js`: 정지 거부 문구를 "정지 요청이 거부됐습니다 · 파워서플라이 전원을 차단하세요"로(비상정지 버튼 안내 삭제).
- 시험: Jetson 416/416, UI smoke 173/173(PC, 모의). 실기에서는 확인하지 않았다.
