# 젯슨 실기 작업 안내 (젯슨에서 실행하는 Claude Code 용)

젯슨 나노에서 `cd ~/SAVER && claude` 로 시작한 Claude Code 가 처음에 읽는 문서다.
규칙의 기준은 저장소 루트 `AGENTS.md` 하나이고(`CLAUDE.md` 가 불러온다), 이 문서는 그 규칙을 젯슨 실기에
적용할 때 필요한 사실, 명령, 현재 상태만 담는다. 목표값과 한계값 같은 수치는 적지 않는다. 필요하면
`docs/PROJECT_STATE.md` 를 읽는다.

## 1. 역할 경계

| 젯슨이 한다 | 젯슨이 하지 않는다 |
|---|---|
| 상태머신 진행(S1~S13, `protocol/state_machine_v2.json`), 키오스크 UI 제공, 기록 | PWM, 실시간 제어 루프, 인터록 |
| Teensy 에 명령을 **요청**(Teensy 는 인터록을 보고 거부할 수 있다) | 모터 전압이나 듀티 지정 |
| 레이더 판독(호흡수, 심박수, 거리 → EAPD), 압박점 탐색 지휘(목표 거리만 요청) | 비상정지(하드웨어 회로가 끊고 Teensy 는 보고만 한다) |

- 레이더 호흡수, 심박수는 표시와 기록용이다. 회복 판정 같은 판단에 쓰지 않는다.
- Teensy 는 젯슨이 멈춰도 혼자 안전 상태를 지킨다. 젯슨 쪽 문제를 풀려고 Teensy 에 동작 명령을 보내지 않는다.

## 2. 이 젯슨의 구성 (설치 스크립트 `setup/setup_jetson.sh` 기준)

| 항목 | 값 |
|---|---|
| 저장소 | `~/SAVER`, 브랜치 `claude/saver-project-onboarding-xpmdhw` |
| OS, Python | JetPack 4.6(Ubuntu 18.04), 시스템 `python3` 3.6. websockets 9.1, pyserial 3.5(사용자 영역 `~/.local`) |
| Node.js | 없다. `ui/` 를 고쳐도 젯슨에서 빌드할 수 없다. PC 에서 `cd ui && npm run bundle:jetson` 뒤 `jetson/ui_dist` 를 커밋한다 |
| Teensy 4.1 | `/dev/saver-teensy`(udev, USB 16c0:0483) |
| 레이더 키트 | `/dev/saver-radar`(udev, XIAO ESP32C6 303a:1001). 레이더 UART 를 40핀에 직결하면 `/dev/ttyTHS1` |
| 브리지 서비스 | `saver-bridge.service`: `python3 -m saver.bridge --serial /dev/saver-teensy --serve jetson/ui_dist`, UI http 8080, WebSocket 8765, 기록 `~/saver_logs`, `--radar /dev/saver-radar`. `--config jetson/config.json` 은 **설치할 때 그 파일이 있었을 때만** 붙는다 |
| 냉각 팬 | `saver-fan.service`, 설정 `/etc/saver/fan.json` |
| 키오스크 | `chromium-browser --kiosk` (`saver-kiosk`) |
| 카메라 | 미착수(코드 없음) |

## 3. 명령 (자세히는 `setup/README.md` "명령")

| 명령 | 쓰임 | 사람 확인 |
|---|---|---|
| `saver-status` | 서비스, 포트, UI 응답, 최근 기록의 S1 점검, 팬, 시각, IP | 없이 |
| `timeout 20 saver-logs` | 브리지 실시간 기록. 끝나지 않는 명령이라 `timeout` 을 붙인다 | 없이 |
| `saver-radar --seconds 15`, `saver-radar --dump --seconds 10` | 레이더 값, 원본 바이트와 자료 종류. 브리지가 돌면 실행을 거부한다 | 앞의 `saver-stop` 은 장치가 멈춰 있을 때만 |
| `saver-test` | 자동 시험(`jetson/tests/test_all.py`) | 없이 |
| `saver-start` | 브리지와 키오스크 시작. **브리지가 S1 에서 Teensy 에 ARM, HOME 을 보내 장치가 움직인다** | 필요 |
| `saver-update` | `git pull --ff-only` 뒤 브리지 재시작 | 브리지가 돌고 있으면 필요 |
| `saver-fan test` | 팬 5초 최대(비밀번호를 묻는다) | 필요 |
| `saver-off` | 젯슨 끄기 | 사람만 |

대화형 입력을 기다리는 명령은 멈춘다. 저장소 갱신은 `GIT_TERMINAL_PROMPT=0 git pull --ff-only`, 관리자 명령은
`sudo -n` 으로 실행해 비밀번호나 토큰을 물으면 바로 실패하게 하고, 사람에게 터미널에서 해 달라고 요청한다.

## 4. 자료 흐름

```
키오스크 UI ──WebSocket 8765── bridge.py ──USB JSON Lines── Teensy 4.1
                                 ├─ state_machine.py  ← protocol/state_machine_v2.json (전이 규칙의 유일한 기준)
                                 ├─ init_seq.py       S1: PING → GET_STATUS → ARM → HOME → 원점 완료 대기
                                 ├─ search.py         X_SEARCH: HOME → HEAD_HOLD → DRIVE_STEP 반복 → 말단 판정 → 압박점 이동
                                 ├─ radar.py          ← /dev/saver-radar, 1초마다 UI 로 radar 메시지
                                 └─ logger.py         → ~/saver_logs (CSV)
```

| 메시지 | 방향 | 뜻 |
|---|---|---|
| `intent` | UI → 브리지 | 전이 요청. 브리지가 상태머신의 주인이고 UI 는 받은 `state` 를 그린다 |
| `state`, `telemetry`, `error`, `rejected`, `link` | 브리지 → UI | 확정 상태, 실측값(오래되면 `stale`), 오류, 거부 사유, Teensy 연결 여부 |
| `search` | 양방향 | 탐색 시작과 중지(X_SEARCH 에서만), 진행(반력, 판정, D) |
| `radar` | 브리지 → UI | 호흡수, 심박수, 거리, EAPD. 측정하지 못했거나 오래된 값은 보내지 않는다 |

브리지 상태를 화면 없이 보려면 `ws://127.0.0.1:8765` 에 붙어 **받기만** 하는 짧은 스크립트를 쓴다.
`intent` 와 `search` 를 보내면 상태머신이 진행해 장치가 움직일 수 있으므로 사람 확인 없이 보내지 않는다.

## 5. Teensy 연결과 통신

- 기준: `protocol/teensy_usb_protocol_v1.md`(v1). 주행 탐색 명령은 `docs/protocol_v1.1_draft.md`(합의본, `protocol/` 반영 대기).
- JSON Lines, `v:1`. 모든 명령에 `seq`, 수락과 거부 모두 ACK, 중복 `seq` 는 재실행 없이 ACK 만 다시 온다. 젯슨은
  시작 seq 를 무작위로 둔다(젯슨만 재시작할 때 겹치지 않게).
- 텔레메트리 기본 20 Hz. 측정하지 못한 필드는 0 이 아니라 생략된다. UI 는 생략되거나 오래된 값을 "—" 로 둔다.
- Teensy 내부 상태(`IDLE`, `ARMED`, `HOMED`, `VACUUM_HOLD`, `CPR_READY`, `CPR_RUNNING`, `CPR_PAUSED`, `FAULT`, `ESTOP`)는
  S1~S13 과 다른 계층이다. 섞어 쓰지 않고 새 상태 이름을 만들지 않는다.
- 통신 두절, 손상된 메시지, 오래된 텔레메트리, 상태 불일치는 안전 상태로 간다. sev 3 오류는 S11 안전정지로 강제 이동한다.
- Teensy 를 연결하지 않으면 키오스크는 S1 "시스템 점검 중" 에 머문다. 설계대로다(D-44).
- 확인 수준: 가짜 Teensy(pty)로 자동 시험(tested, 소프트웨어 단독). 젯슨과 실물 Teensy 의 송수신 결과는 저장소에
  기록이 없다(계획만 있음). 현장 확인 대상이다.

## 6. 레이더(MR60BHA2 키트), 호흡, 심박, EAPD

- 경로: 레이더 UART → XIAO ESP32C6(전달 펌웨어 `jetson/radar_fw/mr60bha2_passthrough`) → USB → `/dev/saver-radar` →
  `saver/radar.py`(Seeed TinyFrame, 프레임 첫 바이트 0x01, 체크섬 검사).
- 값: 호흡수, 심박수(표시만), 거리(cm 를 mm 로), 사람 감지. 0 이나 측정하지 못한 값은 비운다.
- EAPD = `radar_floor_mm` − 최근 거리 중앙값(구간과 최소 표본 수는 `radar.py` 상수). `radar_floor_mm` 이 비어 있으면
  EAPD 를 만들지 않고 탐색도 시작하지 않는다(대체 상수 없음).
- 탐색 거리: `D = min(cp_dist_max_mm, cp_dist_ref_mm × EAPD / eapd_ref_mm)`. 설계 가설이며 인체에서 검증되지 않았다.
  `search.py` 의 설정값이 하나라도 비면 탐색을 시작하지 않는다.
- 현장 확인: `saver-stop` 뒤 `saver-radar`. 프레임이 0 이면 안내 줄의 `종류`와 `saver-radar --dump` 의 합계 줄로
  원인을 가린다(`jetson/README.md` "현장 확인"). `esphome` 이면 XIAO 에 전달 펌웨어가 없다.

## 7. 설정 파일

| 파일 | 쓰임 |
|---|---|
| `jetson/config.sim.json` | 시뮬레이션 전용. 의학적 근거가 없다. 실기에 쓰지 않는다 |
| `jetson/config.json` | 확정값. 없으면 미확정 값은 0 으로 두고 Teensy 에 보내지 않는다(`START_CPR` 거부가 정상). 만들거나 바꾸는 일은 사람 확인 뒤에 하고, 서비스가 읽게 하려면 설치 스크립트를 다시 실행한다 |

미확정 값은 추측으로 채우지 않는다. 0 이나 null 로 두고 그 값을 쓰는 기능을 켜지 않는다.

## 8. 시험

```bash
cd ~/SAVER/jetson && python3 tests/test_all.py
```

- 하드웨어 없이 전부 통과해야 한다. 실물 Teensy 가 꽂혀 있어도 시험은 없는 포트 경로와 빈 포트 번호를 써서
  ARM, HOME 을 보내지 않게 만들어 두었다.
- 시험 묶음을 동시에 두 개 돌리지 않는다. `/tmp` 의 가짜 포트 경로를 같이 써서 서로 방해한다.
- 실물이 필요한 시험은 따로 적고 기본 묶음에 넣지 않는다.

## 9. 사람 확인이 필요한 일

`AGENTS.md` 규칙을 젯슨 실기에 적용한 목록이다.

| 일 | 이유 |
|---|---|
| `saver-start`, 실물 Teensy 로 브리지 실행 | S1 에서 ARM, HOME 을 보내 장치가 움직인다 |
| Teensy 에 동작 명령 보내기(START_CPR, DRIVE_STEP, HEAD_HOLD, 흡착 등), 브리지에 `intent` 보내기 | 기계 동작 |
| Teensy 펌웨어 올리기, `teensy/` 수정 | 한건민 소유. 읽기만 한다 |
| `protocol/` 수정 | 양쪽 담당자 합의 |
| `config.json` 만들기와 바꾸기, 의학과 안전에 관한 값 | 임의 값 금지 |
| XIAO 펌웨어 올리기 | 키트 기본 펌웨어를 덮어쓴다 |
| `sudo`, 재부팅, `saver-off`, 패키지 설치와 삭제 | 장치 상태 변경 |
| 파일 삭제, 강제 푸시 | 되돌리기 어렵다. 강제 푸시는 하지 않는다 |

## 10. 작업 방식

- 작업 전 `GIT_TERMINAL_PROMPT=0 git pull --ff-only`. 클라우드의 Claude 도 같은 브랜치에 커밋한다.
- 커밋 메시지는 `[jetson] …` 형식. 푸시가 거부되면 `git pull --rebase` 뒤 시험을 다시 돌리고 푸시한다.
- 고친 뒤에는 시험을 다시 돌린다. Python 3.6 문법만 쓴다(f-string, dataclass 금지).
- 기록(`~/saver_logs`)과 화면 사진은 저장소에 올리지 않는다. 필요한 줄만 보고에 옮긴다.
- 보고 형식: 한 일, 실행한 명령과 출력(필요한 줄 그대로), 판단(확인된 사실과 추론을 나눔), 확인하지 못한 하드웨어
  동작, 다음 할 일. 주장 수준은 `implemented`, `tested`, `simulated`, `assumed`, `planned` 로 구분한다.

## 11. 지금 열린 일 (10/15)

| # | 일 | 메모 |
|---|---|---|
| 1 | 레이더 키트 전달 펌웨어 | **수신 확인(10/15)**: `--dump` 10초 레이더 프레임 1,190개(종류 radar), 실시간 판독 15:34 1초에 약 76개, 호흡, 심박, 거리, 사람 감지 갱신, 처음 1초 뒤 빠짐과 깨짐 증가 없음. 남은 것: 거리 자 대조. 다시 0 바이트면 진단 펌웨어(`radar_fw/mr60bha2_diag/bin`) |
| 2 | `radar_floor_mm` 실측 | 탐색 자세(바퀴 내림)에서 레이더 면의 바닥 위 높이. 값 반영은 사람 확인 뒤 |
| 3 | 실물 Teensy 연결 확인 | `saver-status` 의 S1 점검 결과. 첫 연결은 모터와 블로워 전원을 끈 상태에서 |
| 4 | 키오스크 표시 확인 | 레이더 값은 S8, S9 화면과 개발 패널(D 키) |
| 5 | 냉각 팬 실기 확인 | `saver-fan`, `saver-fan test` |
