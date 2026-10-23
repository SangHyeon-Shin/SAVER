# 젯슨 연동 우선 시험: 손가락 센서(PPG), 음압 모터, 음압 센서 (한건민 Codex 전달용)

작성: 2026-10-23 밤, 신상현(Claude Code). 대상은 한건민의 Codex(`teensy/`)다. 아래 "Codex 프롬프트" 블록을 그대로 붙여 넣는다.

- **펌웨어 기준:** `codex/teensy-integration-20261023`의 `3e710c8`(10/23 23:02). 근거는 이 코드를 **읽은 결과**다. 실기에서 시험하지 않았다.
- **젯슨 기준:** `claude/saver-project-onboarding-xpmdhw`. 이 문서와 같은 커밋에서 흡착 시작 절차를 고쳤다(아래 0장).
- **고치지 않은 것:** `protocol/`, `teensy/`(AGENTS.md 소유권).

## 0. 이 시험에서 막히는 곳 (코드 대조)

| 항목 | 지금(3e710c8) | 결과 | 누가 |
|---|---|---|---|
| 흡착 시작의 `HOME` | 젯슨이 `ARM → HOME → VACUUM_ON` 순서로 보내는데 펌웨어는 `HOME`을 항상 `HOME_UNAVAILABLE_NO_VERIFIED_LIMIT`로 거부 | 흡착 시작 불가 | **젯슨에서 해결함.** `GET_INFO` `caps`에 `CP_ZERO`(v1.1 의미의 HOME)가 없으면 `HOME`을 보내지 않고 `ARM → VACUUM_ON`(v1.1 §4.7-4). 펌웨어는 `ARMED`의 `VACUUM_ON`을 이미 받는다 |
| PPG | 원격 `ARM`에서만 `ppg_.enter()`. 환자 상태 화면은 명령을 보내지 않는다 | 환자 상태 화면에 값 없음 | Teensy |
| 음압 설정값 | `VAC CONFIG`가 RAM 전용 직접 명령. 젯슨 브리지가 포트를 쥐면 넣을 수 없다 | `VACUUM_ON` → `VAC_CONFIG_REQUIRED`, `ARM` → `VACUUM_RELEASE_NOT_VERIFIED` | Teensy |
| 음압 센서 영점 | README_demo.md: 대기 개방에서 −24.60 kPa | 해제 판정 불가 → `ARM` 거부 | Teensy(하드웨어 확인 포함) |

`VACUUM_ON`의 허용 상태는 명세 §5 표에서 `HOMED`다. `ARMED`에서 받는 것은 리미트가 없는 수동 배치 펌웨어의 동작이며,
받아들일지는 Teensy가 정한다(젯슨 명령은 요청). 리미트를 달아 `caps`에 `CP_ZERO`를 넣으면 젯슨은 다시 `HOME`을 보낸다.

## Codex 프롬프트

```text
[작업] 젯슨 연동 우선 시험: 손가락 센서(PPG), 음압 모터(블로워), 음압 센서만

배경
- 저장소 SAVER. 작업 브랜치 codex/teensy-integration-20261023(3e710c8). 진입점 teensy/saver_teensy.ino → teensy/src/demo/demo_app.h.
- 압박, 구동, HOME 없이 위 세 가지만 젯슨과 통신해 확인한다. 젯슨은 결선 시연 모드(브리지 --unit-demo,
  jetson/config.bench.json)의 "환자 상태"와 "흡착" 화면을 쓴다.
- 젯슨 변경(신상현, 브랜치 claude/saver-project-onboarding-xpmdhw, docs/teensy_ppg_vacuum_test.md):
  GET_INFO caps 에 CP_ZERO 가 없으면 흡착 시작에서 HOME 을 보내지 않고 ARM → VACUUM_ON 으로 간다.
  따라서 지금의 HOME 거부와 caps ["AID","MOT"] 는 그대로 둔다.
- 규칙은 AGENTS.md. protocol/, jetson/, ui/ 는 읽기만 한다. 명세에 없는 임계값을 만들지 않는다. 성공을 꾸며 보내지 않는다.
- 먼저 지금 코드가 아래 요구를 이미 만족하는지 확인한다. 만족하는 항목은 바꾸지 말고 확인 결과만 보고한다.

1. 젯슨이 보내는 것과 기대하는 응답 (protocol/teensy_usb_protocol_v1.md v1.1)
- 연결마다 GET_INFO(seq 만) → ack ok + evt INFO {proto "1.1", caps 에 "AID", boot_id, last_seq, last_aid}.
  이후 PING 1 s 간격. 모든 cmd 에 200 ms 안 ACK, 젯슨은 2회 재전송 뒤 실패 처리.
- 환자 상태 화면: 명령 없음. tel 의 ppg_hr_bpm, spo2_pct 만 본다.
- 흡착 [흡착 시작]:
  1) GET_STATUS → ack ok, 1 s 안에 tel(같은 tn 의 mot). state 가 IDLE/ARMED/HOMED 이고 faults 가 비어 있어야 진행.
  2) IDLE 이면 ARM{bid,aid} → ack ok, state ARMED.
  3) VACUUM_ON{bid,aid,"target_kPa":-22.5} → ack ok.
  4) 30 s 안에 evt VACUUM_OK{seq,aid = VACUUM_ON 의 값} 또는 tel state VACUUM_HOLD → 화면 "고정 중".
     30 s 를 넘기면 젯슨이 VACUUM_OFF 를 보내고 실패(VACUUM_TIMEOUT). 30 s 는 젯슨 화면 대기 한도이며 안전 판단이 아니다.
     실측 도달 시간이 이보다 길면 값을 신상현에게 알린다(젯슨 쪽에서 늘린다).
  실패로 보는 것: ACK 거부(why 를 화면에 그대로 보임), evt VACUUM_FAIL, sev 3 err, tel state FAULT/ESTOP, 링크 두절.
- [해제], 다른 화면으로 이동, UI 연결 끊김: VACUUM_OFF{bid,aid} → ack ok 로 젯슨 절차는 끝난다.
  해제가 확인되면 evt VACUUM_RELEASED{seq,aid,vac_kPa}, state IDLE.

2. 할 일
2.1 PPG 를 ARM 과 무관하게 관측한다 (필수)
- 부팅 때(압박 정지 상태) PPG 취득을 한 번 시작한다. 관측만 하고 출력은 없다. 원격 ARM 의 PPG 시작과 겹치지 않게 정리한다.
- tel: 유효할 때만 ppg_hr_bpm, spo2_pct(소수 2자리). 손가락 없음, 창 미완성, 진단 모드는 필드 생략. 0 을 보내지 않는다.
- PPG 미검출(PPG_NOT_FOUND)과 손가락 없음은 이번에는 faults 나 err 에 넣지 않는다(BENCH_LOG 만).
  젯슨 흡착 시작이 faults 가 비어 있기를 요구하기 때문이다. 2-PU-002 보고는 따로 합의한다.
- MAX30102(Wire, 18/19)가 다른 I2C 장치와 버스를 함께 쓰면 Wire.begin()/setClock() 이 그 장치 읽기에 영향이 없는지 확인한다.

2.2 음압 센서: 대기압에서 0 근처 (필수, 하드웨어 확인 포함)
- 대기 개방 −24.60 kPa 가 남아 있으면 해제 판정이 서지 않아 ARM 이 계속 VACUUM_RELEASE_NOT_VERIFIED 다.
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
- PHYSICAL_ESTOP_POWER_CUT_USER_CONFIRMED 는 시험을 통과시키려고 바꾸지 않는다(docs/teensy_s1_check.md 2장).

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
- tel: v, t, ms, tn, boot_id, state, vac_kPa, ppg_hr_bpm, spo2_pct, faults([] 또는 코드). estop 은 싣지 않는다
  (ESTOP_AUX_INSTALLED=false 유지). vac_leak 은 누설 판정 기준이 없으면 생략한다.
- mot: rearm_lock, vac_released 는 항상. 지금의 "hold", "settled" 는 명세 이름(head_hold_active, drv_settled)이 아니다.
  젯슨은 모르는 필드를 무시하므로 이번 시험에는 영향이 없다. 고친다면 명세 이름으로 바꾸거나, 측정할 수 없으면 생략한다.
- 한 줄 511 byte 이하, LF 하나, 실수 소수 2자리, null·NaN 금지.

2.8 하지 않을 것
- HOME 을 받아들이거나 HOMING_DONE OK 를 보내지 않는다. caps 에 CP_ZERO 를 넣지 않는다(넣으면 젯슨이 HOME 을 보낸다).
- 이 시험 때문에 압박 모터를 움직이는 경로를 열지 않는다.
- 2 s 통신 감시(3-CM-010), 정지 명령 우선 처리, ACK 재전송 처리, 기존 인터록, ESTOP_AUX_INSTALLED 는 그대로 둔다.

3. 시험
- PC: teensy 에서 g++ -std=c++11 -Wall -o test test_saver_proto.cpp && ./test, 그리고 teensy/tests/demo/check_main.ps1,
  check_ppg_diag_replay.ps1.
- 추가할 시험(test_main.cpp):
  (1) 부팅 직후 ARM 없이 tel 에 PPG 값(합성 fixture)이 실리고, 손가락이 없으면 필드가 빠진다.
  (2) 음압 설정 저장·복원. 없거나 깨지면 미설정이고 VACUUM_ON 은 VAC_CONFIG_REQUIRED, ARM 은 VACUUM_RELEASE_NOT_VERIFIED.
  (3) 설정이 있을 때 GET_INFO → GET_STATUS → ARM → VACUUM_ON → VACUUM_OK → VACUUM_OFF → VACUUM_RELEASED 순서와 seq·aid.
  (4) PPG 미검출이어도 faults 가 비어 있고 흡착 절차가 진행된다.
- 실물(사람 확인, 젯슨 브리지를 끈 상태, 같은 포트를 둘이 열지 않는다). 시리얼 터미널:
  {"v":1,"t":"cmd","seq":1,"cmd":"GET_INFO"}     → INFO proto 1.1, caps ["AID","MOT"], boot_id = B
  {"v":1,"t":"cmd","seq":2,"cmd":"GET_STATUS"}   → tel IDLE, faults [], vac_kPa 0 근처, 손가락을 올리면 ppg_hr_bpm·spo2_pct,
                                                    mot vac_released true(해제 안정 시간 뒤)
  ARM 이후는 ARMED 에서 2 s 동안 JSON 이 없으면 3-CM-010 으로 FAULT 가 되므로 손으로 치지 말고,
  1 s 간격 PING 을 함께 보내는 스크립트나 젯슨 브리지로 확인한다.

4. 보고
- 바꾼 파일, 실행한 시험과 결과, 실측값(전압, 대기압 vac_kPa, 해제 대역·시간)과 잰 방법.
- 가정한 것과 실물로 확인하지 못한 것을 나눠 적는다(AGENTS.md 작업 완료 보고).
- 커밋은 [teensy] 접두어. 공유 브랜치를 강제 push 하지 않는다.
```

## 1. 젯슨에서 확인하는 방법 (신상현, 펌웨어 반영 뒤)

1. 젯슨 저장소를 이 브랜치로 갱신하고 브리지를 다시 띄운다(UI 묶음 `jetson/ui_dist`는 이번에 바뀌지 않았다).
   ```bash
   python3 -m saver.bridge --unit-demo --serial /dev/saver-teensy --config config.bench.json
   ```
2. **환자 상태:** 손가락을 올리면 심박수·산소포화도가 나온다. 손가락을 떼면 값이 빠진다(0이 아니라 표시 없음).
3. **흡착:** [흡착 시작] → "장치 상태 확인 중" → "구동 허가 중" → "바닥에 붙이는 중" → "고정 중". 게이지에 `vac_kPa`.
   세션 기록(sys)에 `UNIT_START:vacuum`, `UNIT_VACUUM_NO_HOME:CAPS`, `UNIT_RUNNING:vacuum`.
4. **해제:** [해제] → `VACUUM_OFF`. 다시 시작하기 전에 장치가 `IDLE`로 돌아왔는지(해제 확인) 본다.
   해제가 확인되지 않아 장치가 `VACUUM_HOLD`에 남아 있으면 젯슨은 이미 고정된 것으로 보고 바로 "고정 중"을 띄운다(알려진 한계).
5. **실패 뒤:** 장치가 `FAULT`면 젯슨 시연 화면에는 고장 해제(`RESET_FAULT`) 버튼이 없다. 브리지를 끄고 직접 명령으로 해제하거나
   Teensy를 다시 켠다.

## 2. 이 문서와 같은 커밋의 젯슨 변경

- `jetson/saver/unit_demo.py`: 흡착 시작에서 `caps`에 `CP_ZERO`가 없으면 `HOME`을 보내지 않는다. 시작할 수 없는 장치 상태는
  명령을 보내기 전에 `STATE:<상태>`로 끝낸다(이전과 같은 결과).
- 시험: Jetson 413/413(PC, 모의). 실기에서는 확인하지 않았다.
