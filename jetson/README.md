# jetson/ — Jetson Nano

담당: 신상현 (Claude Code). 규칙은 저장소 루트의 `AGENTS.md` 를 따릅니다.
젯슨 실기에서 Claude Code 로 작업할 때는 `AGENT_BRIEF.md`(젯슨 전용 사실, 명령, 사람 확인이 필요한 일)를 먼저 읽힙니다.

하드웨어 없이 전체를 개발·시험할 수 있습니다. 가짜 Teensy가 **가상 시리얼 포트(pty)** 를
만들기 때문에, 코드는 실제 장비와 완전히 같은 경로로 접속합니다. "시뮬레이션용 분기"가
코드에 생기지 않는 것이 이 방식의 핵심입니다.

```
React UI ──ws://:8765── bridge.py ──USB Serial── Teensy 4.1
                          │                      (또는 fake_teensy.py)
                          ├── state_machine.py  ← protocol/state_machine_v2.json
                          └── logger.py         → logs/*.csv (W7 규격)
```

## 젯슨 실기 설치

젯팩 4.6 을 새로 깐 젯슨에서는 `bash setup/setup_jetson.sh` 한 번으로 패키지, 권한(Teensy udev), 브리지 서비스,
냉각 팬 자동 제어(`saver-fan`), 키오스크, Claude Code, 자동 시험을 설정합니다. 자세한 내용은 `setup/README.md`.

## 결선 시연 모드 (`--unit-demo`, D-51)

결선(10/24) 포스터 시연용이다. UI 첫 화면이 시연 메뉴(전체 UI, 환자 상태, 압박, 강성 측정, 흡착)가 되고, 브리지는
상태머신 흐름과 S1 자동 초기화(ARM, HOME)를 돌리지 않는다. 장치는 유닛 화면의 [시작]으로만 움직인다.
기본(운용 모드)은 그대로 메뉴 없이 상태머신 흐름이다.

```bash
# 젯슨 서비스(설치 스크립트가 서비스를 다시 쓴다. 벤치 설정은 팀장 확인 뒤)
bash setup/setup_jetson.sh --unit-demo --config jetson/config.bench.json --skip-apt --no-claude
# 직접 실행
python3 -m saver.bridge --unit-demo --serial /dev/saver-teensy --radar /dev/saver-radar --config config.bench.json
# 모의 시험: 흡착 없이 압박을 허용하는 펌웨어 제안(--bench, 실물 미구현)을 흉내 낸 가짜 Teensy
python3 simulators/fake_teensy.py --port /tmp/ttyFAKE --bench
python3 -m saver.bridge --unit-demo --serial /tmp/ttyFAKE --config config.bench.json
```

| 화면 | 장치 명령 (`saver/unit_demo.py`, [시작] 뒤 `GET_STATUS` 로 상태를 보고 지난 단계는 건너뜀) | 보이는 값 |
|---|---|---|
| 전체 UI | 없음. S1~S13 화면 흐름을 처음부터 모형으로(로컬 전이) | 모의값 |
| 환자 상태 | 없음 | 레이더 호흡수, 손가락 센서 `ppg_hr_bpm`, `spo2_pct` |
| 압박 | ARM → HOME → SET_TARGET → START_CPR, 정지 STOP_CPR | `cp_depth_mm`, `cp_rate_cpm`, `cp_force_N`, 경과 시간, 횟수(속도 적분, "약") |
| 강성 측정 (10/23, D-53) | 압박과 같은 순서 → 2주기 버리고 10주기 측정 → STOP_CPR{END} | 힘-깊이 그림, 유효 강성 k = 주기 최대 힘 ÷ 최대 깊이(N/mm), 최대 힘, 최대 깊이, 진행, 편차. 결과는 기록에 `UNIT_STIFF` |
| (구동, 메뉴에서 뺌) | ARM → 압박점 탐색(`search.py`, v1.1 명령). 개발용 바로가기 `?unit=drive` | 탐색 진행(압박 위치 찾기 화면) |
| 흡착 | ARM → HOME → VACUUM_ON, 해제 VACUUM_OFF | `vac_kPa`, 목표, 고정 유지 시간, `vac_leak` |

압박, 강성 측정, 흡착 모두 `GET_INFO` `caps`에 `CP_ZERO`(v1.1 의미의 HOME)가 없으면(리미트 없는 수동 배치 펌웨어) HOME 을 빼고
ARM 다음 단계로 간다(10/23 팀장: 모든 시작은 젯슨 UI 터치, `docs/teensy_ppg_vacuum_test.md`). 정지가 거부되면 화면은
파워서플라이 전원 차단을 안내한다(비상정지 버튼 미연결, D-54).

- 진입만으로는 명령이 없다. 압박 시작은 1초 길게 누르기. 다른 화면으로 가거나 UI 연결이 모두 끊기면 진행 중인 동작을 멈춘다.
  비상정지, 고장(sev 3), 링크 두절, 응답 없음은 실패로 끝내고 사유를 화면에 보인다. 목표 깊이가 비면 아무 명령도 보내지 않는다.
- 회복(ROSC)은 장치가 판정하지 않는다. 손가락 센서 맥박 신호가 잡히면 확인하라고 띄우고, 회복 표시는 사람이 누른다.
- 브리지 미연결이면 유닛 화면은 모의값("모의 데이터")이다. 개발 패널(왼쪽 위 1.5초)에서 연결 중에도 모의값을 강제할 수 있다.
- **펌웨어 의존**: 메인 펌웨어는 PPG 송신만 있고 HOME, 흡착, 압박 제어는 자리표시다. 흡착 없이 압박하려면
  `START_CPR` 허용 상태 변경이 필요하다(합의 전). 요청 사항은 `docs/unit_demo_firmware.md`.

## MR60BHA2 레이더 (D-50)

`saver/radar.py` 가 레이더 원시 프레임을 읽어 호흡수, 심박수, 거리를 받는다. 브리지에 `--radar PORT` 를 주면 켜지고
(설치 스크립트 기본 `/dev/saver-radar`), 1초마다 UI 로 보내고 기록한다.

| 값 | 쓰는 곳 |
|---|---|
| 호흡수, 심박수 | 일시정지(S8), 회복 신호 확인(S9) 화면. 장치가 멈춤을 보고한 뒤에만 수치. 개발 패널(D 키)은 언제나. 판단에는 쓰지 않음 |
| 거리 → 가슴 두께(EAPD) | `EAPD = radar_floor_mm − 거리`. 압박점 탐색이 D 를 계산해 Teensy 에 주행 거리로 요청 |

- `radar_floor_mm`(탐색 자세에서 레이더 면의 바닥 위 높이, mm)은 실측해 `jetson/config.json` 에 넣는다. 비어 있으면
  EAPD 를 만들지 않아 탐색이 "가슴 두께를 재지 못했습니다"로 시작하지 않는다(대체 상수 없음).
- 하드웨어 없이: `python3 simulators/fake_radar.py --port /tmp/ttyRADAR` 뒤 `python3 -m saver.bridge --radar /tmp/ttyRADAR --config config.sim.json`.
  `--text` 를 주면 프레임 대신 글자를 보내 키트 기본 펌웨어 상황을 흉내 낸다.

### 연결

| 연결 | 포트 | 준비 |
|---|---|---|
| XIAO ESP32C6 키트를 USB 로 | `/dev/saver-radar`(udev 고정 이름, 실제 `/dev/ttyACM*`) | 키트 기본 펌웨어(ESPHome)는 원시 프레임을 USB 로 보내지 않는다. 전달 펌웨어를 한 번 올린다(아래) |
| 레이더 UART 를 40핀에 직접 | `/dev/ttyTHS1`(8번 TX, 10번 RX, 3.3 V) | 설치 때 `--radar-port /dev/ttyTHS1` |

### 전달 펌웨어 올리기 (XIAO ESP32C6, 한 번)

`radar_fw/mr60bha2_passthrough/` 는 레이더 UART(115200)를 USB 로 그대로 넘긴다. 값을 해석하거나 바꾸지 않는다
(Seeed-mmWave-library `passthrough_mode` 와 같은 동작).

0. (지금 펌웨어가 필요할 수 있으면) 백업: PC 에서 `python -m pip install esptool` 뒤
   `python -m esptool --chip esp32c6 --port <포트> read_flash 0 0x400000 xiao_backup.bin`(XIAO ESP32C6 플래시 4 MB 전체).
   되돌릴 때는 이 파일을 아래와 같은 방법으로 `0x0` 에 올린다.
1. 설치 없이: Chrome 또는 Edge 에서 `https://espressif.github.io/esptool-js/` → Connect(키트 포트) → Flash Address `0x0` 에
   `radar_fw/mr60bha2_passthrough/bin/mr60bha2_passthrough_xiao_esp32c6.bin` → Program → USB 를 다시 꽂는다.
   연결이 안 되면 XIAO 의 BOOT 버튼을 누른 채 USB 를 꽂고 다시 Connect.
2. 또는 Arduino IDE: 스케치 머리말 순서(보드 XIAO_ESP32C6, USB CDC On Boot Enabled).
3. 확인: `saver-stop` 뒤 `saver-radar` 에서 "포트 열림", 프레임 수 증가, 호흡, 심박, 거리.
4. 되돌리기: Seeed 위키의 MR60BHA2 키트 안내대로 기본(ESPHome) 펌웨어를 다시 올린다.

빌드는 확인했고(`bin/BUILD_INFO.txt`), 키트에 올려 보지는 않았다.

10/15 실기(젯슨 Chromium 의 esptool-js 로 기록, MD5 일치): 기록 직후에는 `--dump` 받은 바이트가 0 이었고, ESP Tool 탭을 닫고
USB 를 다시 꽂은 뒤 정상 동작했다. `saver-radar --dump --seconds 10` 에서 23,069 바이트, 레이더 프레임 1,190개, 종류 radar.
레이더 모듈(칩 표기 Andar ADT6101P)은 켜질 때 글자 부팅 기록을 먼저 낸다. 실시간 판독(15:34, `saver-radar --seconds 30`)에서
프레임은 1초에 약 76개(약 1.5 KB/s)였고, 호흡수, 심박수, 거리, 사람 감지가 1초마다 갱신됐다. 거리 값은 40~57 cm 였다(자 대조 전).

| TYPE | 내용 | 처리 |
|---|---|---|
| 0A14, 0A15 | 호흡수, 심박수(float32) | 표시 |
| 0A16 | 거리(유효 표시 + float32, cm) | 표시, EAPD |
| 0F09 | 사람 감지 | 표시 |
| 0A13 | 위상 3개(float32) | 넘김 |
| 0A04 | 표적 정보(20 바이트) | 넘김 |
| 0A17, 0A29 | 뜻을 모름(8, 2 바이트) | 넘김 |
| 0100 | 글자(펌웨어 이름, `breath rate = …` 같은 디버그 글자) | 넘김 |

포트를 연 직후에는 키트(XIAO)가 앞 연결이 끝난 뒤 쌓아 둔 묵은 자료(최대 수 KB)가 한꺼번에 온다. 15:34 실기에서 첫 1초에
`빠짐 2356`, `깨짐 4` 가 찍혔고 그 뒤로는 늘지 않았다. 그래서 `saver-radar` 와 브리지는 연 뒤 1초(`SETTLE_S`)에 받은 바이트를
해석하지 않고, `--dump` 는 처음 1초를 뺀 프레임 수, 번호 빠짐, 깨짐을 따로 보인다. 받으며 화면에 쓰던 이전 `--dump` 에서는
프레임 번호 775개가 빠졌다(화면 출력으로 읽기가 밀린 것과 묵은 자료가 겹친 것으로 추정). 지금 `--dump` 는 다 받은 뒤 출력한다.

진단 펌웨어 `radar_fw/mr60bha2_diag/`(빌드 `bin/mr60bha2_diag_xiao_esp32c6.bin`)는 전달 펌웨어가 다시 0 바이트를 낼 때 원인을
가르는 용도로 둔다. 레이더 자료는 전달하지 않고 1초마다 `SAVER-DIAG …` 글자 줄만 낸다. 읽는 법은 스케치 머리말에 있고,
실기에 올려 보지는 않았다.

### 현장 확인 (`saver-radar`)

`saver-stop` 뒤 `saver-radar`(또는 `python3 -m saver.radar /dev/saver-radar --floor 520`). 1초마다 포트 상태, 받은 바이트,
프레임 수와 값을 보이고, 원인을 연결마다 한 번씩 안내한다. 포트가 열리면 `포트 정보:` 줄에 실제 장치와 by-id 이름이 나온다.
이름에 `Espressif` 가 있으면 레이더 키트(XIAO), `Teensyduino` 면 Teensy 다.

| 보이는 것 | 뜻 |
|---|---|
| `포트 없음` | 그 경로에 장치가 없다. 지금 있는 직렬 포트 목록(by-id 이름 포함)을 함께 보인다 |
| `열기 실패` | 권한(dialout 그룹)이 없거나 다른 프로그램(브리지 등)이 쓰는 중 |
| `포트 열림 받음 0 B` | 장치는 있으나 자료가 없다. 레이더 전원, 배선, 펌웨어 확인 |
| `받음 N B 프레임 0 깨짐 0` | 받은 자료에 레이더 프레임 시작 바이트(0x01)가 없다. 레이더 프레임이 아니다. 아래 `종류` 로 원인을 가린다 |
| 프레임이 늘고 값이 보임 | 정상. 거리 단위(cm → mm)는 자로 잰 값과 대조한다 |
| `빠짐` 이 계속 늘어남 | 프레임 ID 가 건너뛴다(전송 중 손실). 브리지와 같이 읽고 있지 않은지(`saver-stop`), USB 선, 젯슨 부하를 확인 |

프레임이 0 이면 안내 줄의 `종류`(앞 256 바이트로 판별)를 본다.

| 종류 | 뜻 | 할 일 |
|---|---|---|
| `esphome` | 키트 기본 펌웨어(ESPHome)의 기록 글자 | 전달 펌웨어를 올린다(위) |
| `seeed_example` | Seeed 예제 펌웨어가 값을 글자로 내보냄 | 전달 펌웨어로 바꾼다 |
| `csv` | 값을 쉼표로 나눈 글자 줄(예: `74.00, 6.00, 10.00, NORMAL, LOW, 45.92`). XIAO 에 다른 펌웨어(아두이노 스케치 등)가 있다 | 전달 펌웨어로 바꾼다. 지금 펌웨어가 필요하면 먼저 백업 |
| `teensy` | SAVER Teensy 의 JSON 줄. 레이더가 아닌 Teensy 포트를 열었다 | `/dev/saver-radar`(이름에 Espressif)를 연다 |
| `boot` | XIAO(ESP32) 부팅 기록 | 몇 초 뒤에도 같으면 재부팅 반복을 의심한다 |
| `text` | 그 밖의 글자 | 펌웨어와 포트를 확인한다 |
| `binary_nosof`, `binary` | 이진 자료지만 레이더 프레임이 아님 | 통신 속도(`--baud`, 기본 115200)와 장치를 확인한다 |

받은 바이트를 그대로 보려면 `saver-radar --dump`(5초, `--seconds 10` 으로 늘림). 16진수와 글자 줄 뒤에
`합계 N 바이트, 0x01 N개, 레이더 프레임 N개. 종류: …` 를 보인다. 전달 펌웨어가 제대로 돌면 레이더 프레임 수가 0 보다
크고 `종류: radar` 다.

### 시험 화면 (`saver-radar --ui`)

호흡수, 심박수, 거리(cm)를 큰 숫자와 최근 3분 추이로 보는 웹 화면(`saver/radar_view.py`, `saver/radar_view.html`).
레이더 포트만 읽고 브리지, Teensy 와 무관하다(장치를 움직이지 않는다). 브리지와 같은 포트를 읽으므로 `saver-stop` 뒤에 쓴다.

| 명령 | 쓰임 |
|---|---|
| `saver-radar --ui --open` | 젯슨 화면에 창을 띄운다(SSH 로 들어와도 젯슨 화면에). Ctrl+C 로 끝내면 창도 닫힌다 |
| `saver-radar --ui --floor 520` | 레이더 높이(mm, `radar_floor_mm` 과 같은 뜻, 자로 잰 값)를 주면 EAPD 도 보인다 |
| `saver-radar --ui --host 0.0.0.0` | 같은 망의 노트북, 휴대폰에서 `http://<젯슨 IP>:8090` 으로 본다(기본은 젯슨 안에서만) |
| `saver-radar --ui --http-port 8091` | 8090 을 다른 프로그램이 쓰고 있을 때 |

- 거리 칸: 지금 거리, EAPD 가 쓰는 최근 5초(`EAPD_WINDOW_S`) 거리 중앙값, `EAPD = 높이 − 중앙값`(`--floor` 를 줬을 때).
  거리 단위는 레이더 앞에 판을 두고 자로 잰 거리와 거리 칸을 대조해 확인한다.
- `표로 보기`: 최근 30초 값. `CSV 받기`: 최근 10분, 1초 간격(시각은 UTC ISO 8601).
- 터미널에도 5초마다 `호흡 … 심박 … 거리 … cm EAPD … cm` 한 줄을 찍는다.
- 값은 표시와 시험용이다. 판단에 쓰지 않고, 정상 범위 같은 기준을 화면에 두지 않는다.

## 노트북 대체 실행 (젯슨이 안 될 때)

브리지와 UI 는 노트북에서도 돕니다. 레이더, 카메라, 키오스크, `saver-*` 명령은 젯슨 전용입니다.

```bash
cd SAVER/jetson
python -m pip install websockets pyserial     # Python 3.7~3.14 (시험: 리눅스 3.11, 3.13, 3.14)
python -m saver.bridge --serve ui_dist        # Teensy 는 VID/PID 로 자동 탐색. 지정하려면 --serial COM5
# 브라우저에서 http://127.0.0.1:8080
```

- **Windows 실행은 아직 확인하지 않았습니다(10/12).** 한국어 Windows 기본 인코딩(cp949)으로 상태머신 JSON 을 읽다가
  멈추는 문제와 Python 3.14 에서 시작하자마자 멈추는 문제는 고쳤고, 리눅스에서 같은 조건을 만든 시험으로 막아 두었습니다.
- 가짜 Teensy(`simulators/fake_teensy.py`)는 pty 를 써서 Windows 에서 돌지 않습니다. Windows 에서는 실물 Teensy 로만 봅니다.
- 안전 규칙은 젯슨과 같습니다. 브리지는 S1 에서 `ARM`, `HOME` 을 보내므로 처음 연결은 모터와 블로워 전원을 끈 상태에서
  하고, `config.sim.json` 은 쓰지 않습니다.

## 프로토콜 v1.1 (10/23 통일)

젯슨은 **프로토콜 v1.1 펌웨어하고만 동작합니다**(`protocol/teensy_usb_protocol_v1.md` v1.1, 팀장 지시 10/23).
연결할 때마다 `GET_INFO` 로 펌웨어 판(`proto` "1.1", `caps` 의 `AID`)을 확인합니다.

| 펌웨어 | 젯슨 동작 |
|---|---|
| v1.1 | 비조회 명령에 `bid`·`aid`, `tel`·`mot` 짝, `faults` 가 잘리거나 `err` 를 못 받은 코드가 있으면 `GET_FAULTS` |
| v1.0 (`GET_INFO` 를 `UNKNOWN_COMMAND` 로 거부) 또는 `caps` 에 `AID` 없음 | 판 불일치 `3-CM-001`(`FW_NOT_V11`)을 한 번 알리고, 조회와 정지 방향 명령 말고는 보내지 않음. S1 은 `INIT_ERROR`(`FW_VERSION`), 화면은 "장치 펌웨어 판이 맞지 않습니다" |
| v1.1 이지만 `caps` 에 `DRIVE`·`HEAD_HOLD` 없음 | 압박점 탐색을 시작하지 않음(`UNSUPPORTED`) |

펌웨어 판은 기록(`~/saver_logs/saver_*.csv`)의 `FW:1.1:…` 또는 `PROTO_MISMATCH_1.0` 줄로 확인합니다.

## 실행

**터미널 1 — 가짜 Teensy**

```bash
python3 simulators/fake_teensy.py --port /tmp/ttyFAKE
```

기본은 v1.1 모의 장치에 시뮬레이션 설정(`simulators/teensy_settings.sim.json`)입니다. `--no-settings` 는 설정값을 비운
v1.1(실물 펌웨어 기본처럼 `ARM`·`HOME` 거부), `--fw 1.0` 은 v1.0 펌웨어(젯슨이 판 불일치로 처리)를 흉내 냅니다.

실행 중 키보드로 고장을 주입할 수 있습니다.

| 입력 | 재현되는 상황 |
|---|---|
| `oc` | 모터 과전류 `3-CU-021` → 안전정지 |
| `estop` | 비상정지 |
| `leak` | 음압 누설 `2-VU-005` |
| `vloss` | 압박 중 흡착 상실 `3-VU-006` |
| `drop` | 송신 중단 = USB 통신 두절 |
| `slow` | 텔레메트리 1 Hz 저하 |
| `ok` | 정상 복귀 |

**터미널 2 — 브리지**

```bash
pip3 install websockets pyserial
python3 -m saver.bridge --serial /tmp/ttyFAKE --config config.sim.json
```

**터미널 3 — UI**

```bash
cd ../ui && npm install && npm run dev
```

## 시험

```bash
python3 tests/test_all.py
```

프로토콜·상태머신·실제 시리얼 연결·고장 주입·자동 재연결·Jetson 재시작(Teensy 전원 유지)·정적 UI 서빙(Python 3.6 경로 포함)·브리지 기동·UTF-8 아닌 기본 인코딩까지 검사합니다.
하드웨어가 하나도 없어도 전부 통과해야 합니다.

## 설정값 — 중요

`saver/bridge.py`의 `CONFIG`는 기본이 **0**입니다. 0인 값은 Teensy로 보내지 않고,
목표 깊이가 없으면 Teensy가 `START_CPR`을 거부합니다. **이게 정상 동작입니다.**

| 값 | 상태 | 담당 |
|---|---|---|
| `target_bpm` | 확정 110 | — |
| `target_depth_mm` | **미확정** | 한성윤 (가이드라인 근거) |
| `force_max_N` | **미확정** | 한성윤 / 서정준 |
| `vacuum_target_kPa` | **미확정** | 서정준 + 한건민 |

`config.sim.json`은 **시뮬레이션 전용**이며 의학적 근거가 없습니다. 불러오면 경고가 출력됩니다.
확정값이 나오면 `config.json`을 따로 만들어 쓰고, 실기에는 절대 `config.sim.json`을 쓰지 마세요.

결선 벤치 시연 설정 `config.bench.json`: 압박 30 회/분, 깊이 30 mm, 강성 측정 30 회/분, 30 mm(`stiff_bpm`, `stiff_depth_mm`.
비우면 압박 값을 씀)(팀장 10/23 시연 지시, D-51), 힘 상한 600 N, 음압 -22.5 kPa
(`docs/PROJECT_STATE.md` §2, D-30, D-23. 임상 적용값 아님). 팀장 확인 뒤 결선 시연 모드에서만 씁니다(D-51).
`estop_wired: false`(10/23 팀장, 비상정지 버튼 미연결): S1 점검과 시연 시작 점검에서 `estop` 표시를 보지 않습니다.
장치가 `ESTOP` 상태면 점검은 그대로 실패합니다(D-54). 버튼을 연결하면 `true` 로 바꿉니다. Teensy 쪽 S1 통과 조건은
`docs/teensy_s1_check.md`.

## UI 연동

브리지는 상태머신의 **주인**입니다. UI는 화면입니다.

```jsonc
UI → {"type":"intent","name":"CPR_START"}
UI ← {"type":"state","node":"S7","state":"S7","alarm":"normal","allowed":[...]}
UI ← {"type":"telemetry","cp_depth_mm":48.76,...,"stale":false}
UI ← {"type":"error","code":"3-CU-021","sev":3,"desc":"모터 과전류"}
UI ← {"type":"rejected","name":"CPR_START","reason":"device_not_ready","mcu":"VACUUM_HOLD"}
```

**구현 완료** (`ui/src/bridge.js`, `ui/src/App.jsx`).

- 브리지 연결 시 → `intent` 만 보내고 받은 `state` 로 화면 결정
- 미연결 시 → 로컬 전이 (오프라인 시연 백업)
- `rejected` 수신 시 "장치 준비 중" 안내 표시

실제로 UI 가 장치보다 빨리 진행하는 경우가 시험에서 확인됐습니다
(흡착 완료 전 [재확인] 연타 등). `requires_mcu` 가 이를 막습니다.

## 알려진 설계 결정

1. **장치 선행조건 검사** — `state_machine_v2.json`의 `requires_mcu`. UI가 앞서가면 전이를 거부합니다.
2. **압박점 확정은 탐색 뒤 장치 주도** — 탐색(`search.py`)이 압박점 위로 옮긴 뒤 S6 에서 `SET_CP_POINT(offset_mm=0)` 을
   보내고, 장치가 접촉 영점을 잡아 `CPR_READY` 에 올라갑니다(v1.1). 탐색 결과가 없으면 보내지 않습니다.
3. **sev 3 오류 → 안전정지** — 전이표에 `STOP`이 없는 상태에서는 `force("S11")`로 강제 이동하고
   그 사유를 로그에 남깁니다.
4. **통신 두절** — Teensy가 `T_comm`까지 압박을 유지합니다(명세 §9). 브리지는 경고만 띄웁니다.
   `T_comm`은 한성윤 회신(10/1, 25 s)을 v1.1 구현 때 반영합니다. Teensy를 외부 5 V로 켜면
   USB를 뽑아도 Teensy가 꺼지지 않으므로 이 동작이 실기에서 실제로 작동합니다.
5. **오래된 요청은 `aid` 로 가림(v1.1)** — 비조회 명령에 `bid`(부팅 세션)와 `aid`(늘어나기만 하는 번호)를 싣고,
   Jetson 만 재시작하면 `GET_INFO` 의 `last_seq`·`last_aid` 다음부터 이어 갑니다. 그 전에 나가는 조회의 seq 는
   무작위로 시작합니다(`protocol.initial_seq`).
6. **실측 표시 규칙** — UI는 연결 중 받은 숫자만 보여 주고, 생략되거나 오래된 필드는 "—"로 둡니다.
   오프라인 시연용 모의값이나 이전 값으로 채우지 않습니다(`ui/src/vitalsView.js`).
