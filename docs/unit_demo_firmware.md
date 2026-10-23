# 결선 시연 모드: 장치 펌웨어에 필요한 것 (D-51, 한건민 확인 요청)

작성 2026-10-18 신상현(Claude Code). 젯슨과 UI는 구현했다(`jetson/saver/unit_demo.py`, `ui/src/screens/Units.jsx`).
결선(10/24) 포스터 시연에서 유닛 화면이 실측값을 보이고 화면의 [시작]으로 장치를 움직이려면 Teensy 쪽이 아래를
지원해야 한다. **프로토콜 메시지 형식은 바꾸지 않는다.** 1번(흡착 없는 압박 허용)만 허용 상태가 v1.0 §5 와 달라
합의가 필요하다. `protocol/` 과 `teensy/` 는 고치지 않았다.

> **10/23 추가: 프로토콜 v1.1 통일.** 젯슨은 v1.1 펌웨어(`GET_INFO` 에 `proto` "1.1", `caps` 에 `AID`)하고만 동작한다.
> v1.0 펌웨어면 판 불일치(`3-CM-001`)로 보고 조회와 정지 명령 말고는 보내지 않으므로, 아래 유닛 시연도 시작하지 않는다(`FW_VERSION`).
> 메인 펌웨어의 v1.1 통신 계층은 10/23 작업 브랜치에 있다(구동은 자리표시, 한건민 검토 필요). 명세 `protocol/teensy_usb_protocol_v1.md` v1.1.

## 지금 상태 (저장소 기준, 10/18)

| 구분 | 상태 |
|---|---|
| 메인 펌웨어 `teensy/saver_teensy.ino` | JSON 통신 골격. MAX30102 읽기와 `ppg_hr_bpm`, `spo2_pct` 송신은 있다. HOME, 흡착, 압박 모터 제어는 `[구현]` 자리표시 |
| 시험 스케치(`test/compression_linear_pid_test` 등) | 문자 명령(`TARGET`, `ARM`, `RUN`, `STOP`)이라 JSON 이 아니다. 젯슨 UI 가 값을 받을 수 없다 |
| 젯슨 시험 | 모의 Teensy(`jetson/simulators/fake_teensy.py --bench`)와 모의 레이더로 메뉴 → 유닛 → 장치 명령 → 화면 값을 끝까지 확인(`simulated`) |

> 10/23: 구동은 구현되지 않아 시연 메뉴에서 빠지고 강성 측정이 들어갔다(D-53). 강성 측정은 새 명령 없이 압박 명령(`START_CPR`, `STOP_CPR`)과 텔레메트리(`cp_force_N`, `cp_depth_mm`)만 쓴다.

Teensy 하나에는 펌웨어 하나만 올라간다. 결선에서 세 시연(압박, 구동, 엣지 컴퓨팅+HMI+환자 모니터링)을 UI 와 함께 하려면
**한 펌웨어**가 아래를 모두 지원해야 한다. 지원하지 않는 유닛은 UI 개발 패널의 "유닛 화면: 모의값으로 시연"으로
화면만 보일 수 있고, 그때 화면에 "모의 데이터"가 표시된다.

## 유닛별로 젯슨이 보내는 것과 펌웨어가 할 일

젯슨은 [시작]을 누르면 먼저 `GET_STATUS` 로 장치 상태를 보고, 아래 순서로 보낸다. 상태가 이미 앞 단계를 지났으면
그 단계는 건너뛴다. 거부(`ok:false`)되면 그 사유를 화면에 그대로 보이고 멈춘다.

| 유닛 | 젯슨 → Teensy | 펌웨어가 할 일 | UI 가 쓰는 텔레메트리 |
|---|---|---|---|
| 환자 상태 | 없음 | MAX30102 읽기(이미 있음) | `ppg_hr_bpm`, `spo2_pct`. 손가락이 없거나 유효하지 않으면 **생략** |
| 압박 | (IDLE) `ARM` → (ARMED) `HOME` → `SET_TARGET{bpm, depth_mm, force_max_N}` → `START_CPR`. 정지 `STOP_CPR{reason}` | `HOME` 실제 원점 복귀와 `HOMING_DONE`. **흡착 없이 `START_CPR` 허용(1번)**. 압박 제어, `STOP_CPR` 즉시 정지 | `state`, `cp_depth_mm`(직전 사이클 최대), `cp_rate_cpm`, `cp_force_N` |
| 흡착 | (IDLE) `ARM` → (ARMED) `HOME` → `VACUUM_ON{target_kPa}`. 정지 `VACUUM_OFF`. 10/23: `caps`에 `CP_ZERO`가 없으면 `HOME` 없이 `ARM` → `VACUUM_ON`(`docs/teensy_ppg_vacuum_test.md`) | 블로워 제어, 목표 도달 시 `VACUUM_HOLD` 와 `VACUUM_OK`, 실패 `VACUUM_FAIL`, 해제 `VACUUM_RELEASED` | `vac_kPa`(음수), `vac_leak` |
| 구동 | (IDLE) `ARM` → 압박점 탐색(`jetson/saver/search.py`): `HOME`, `HEAD_HOLD`, `DRIVE_STEP`, `DRIVE_STOP` | 프로토콜 v1.1 명령과 `GET_INFO` caps `DRIVE`, `HEAD_HOLD`, `MOT`, `AID` | v1.1 `tel`, `mot` |

목표값은 브리지 설정 파일에서만 온다. 결선 벤치 설정 `jetson/config.bench.json`(팀장 확인 뒤 사용):
`target_bpm` 110, `target_depth_mm` 50, `force_max_N` 600, `vacuum_target_kPa` -22.5
(`docs/PROJECT_STATE.md` §2, D-30, D-23. 임상 적용값 아님). 목표 깊이가 비어 있으면 젯슨은 아무 명령도 보내지 않는다.

## 1. 흡착 없는 압박 허용 (합의 필요)

v1.0 §5 와 §7 에서 `START_CPR` 은 `CPR_READY`(흡착 `VACUUM_HOLD` 뒤 `SET_CP_POINT`)에서만 받는다. 결선은 소음 때문에
흡착을 켜지 않고 압박만 보이므로, 표준 펌웨어는 `START_CPR` 을 `NOT_READY` 로 거부한다(젯슨은 이 거부를 화면에 보인다).

제안: **시연 빌드에서만** `START_CPR` 의 허용 상태에 `HOMED` 를 더한다(예: 컴파일 옵션). 메시지 형식은 그대로다.
모의 Teensy 의 `--bench` 가 이 동작을 흉내 낸다(실물 미구현). 흡착 없이 누르면 반력으로 장치가 들릴 수 있으므로
무부하나 연습용 대상으로 하고, 과전류, 리밋, 비상정지 인터록은 그대로 둔다. 허용 여부와 방식은 한건민 판단이다.

## 2. 압박 횟수

텔레메트리에 압박 횟수 필드가 없다(v1.0 §6). UI 는 받은 `cp_rate_cpm` 을 시간에 대해 더해 "약 N회"로 보인다.
정확한 횟수가 필요하면 `cp_count` 필드 추가를 제안한다(프로토콜 변경이라 합의 뒤).

## 3. 젯슨 쪽 안전 동작 (구현됨)

- 화면에 들어가기만 해서는 아무 명령도 보내지 않는다. 압박 시작은 1초 길게 누르기.
- 다른 화면으로 가거나 UI 연결이 모두 끊기면 진행 중인 동작을 멈춘다(`STOP_CPR`, 탐색 중지, `VACUUM_OFF`).
- 비상정지, 고장(sev 3), 링크 두절, 응답 없음은 실패로 끝내고 화면에 사유를 보인다. 압박 중 실패는 `STOP_CPR` 을 한 번 더 보낸다.
- 브리지 결선 시연 모드(`--unit-demo`)는 상태머신 흐름과 S1 자동 초기화(`ARM`, `HOME`)를 돌리지 않는다.

## 4. 확인 순서 (실물, 사람 확인 아래)

1. 모터와 블로워 전원을 끈 채 메인 펌웨어를 연결하고 환자 상태 화면에서 손가락 센서 값(`ppg_hr_bpm`, `spo2_pct`) 확인.
2. 압박: 전원을 켜고 압박 화면에서 길게 눌러 시작, `cp_depth_mm`, `cp_rate_cpm`, `cp_force_N` 이 오는지, [정지]로 바로 멈추는지.
3. 흡착(선택): 소음이 괜찮은 곳에서 `VACUUM_ON` 과 `VACUUM_OK`, [해제].
4. 구동: v1.1 명령과 탐색 설정값이 생긴 뒤.
