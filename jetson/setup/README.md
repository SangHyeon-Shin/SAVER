# 젯슨 나노 일괄 설정

담당: 신상현. 대상: JetPack 4.6.x(L4T R32), Ubuntu 18.04, aarch64, Python 3.6.

젯팩을 새로 깐 젯슨에서 저장소를 받고 스크립트 하나를 실행하면 SAVER 실행에 필요한 설정이 끝난다.

```bash
# 1) 저장소 받기 (비공개 저장소: 비밀번호 자리에 GitHub 개인 액세스 토큰)
git clone --branch claude/saver-project-onboarding-xpmdhw https://github.com/SangHyeon-Shin/SAVER.git ~/SAVER

# 2) 설정 (sudo 없이 실행. 필요할 때 비밀번호를 묻는다)
bash ~/SAVER/jetson/setup/setup_jetson.sh

# 3) 재부팅
sudo reboot
```

git 이 없는 새 젯팩이면 `sudo apt-get install -y git` 을 먼저 하거나, 스크립트 파일만 옮겨 실행해도 된다
(저장소가 없으면 스스로 `~/SAVER` 로 복제한다).

## 하는 일

| 순서 | 내용 |
|---|---|
| 1 | 시스템 확인: aarch64, L4T, Ubuntu 18.04, Python 3.6, 인터넷, 저장 공간 |
| 2 | apt: Chromium(mp3 코덱 포함), 한글 글꼴(Noto CJK, 나눔), 시리얼(picocom), 오디오, 카메라 점검(v4l-utils), 빌드 도구, SSH |
| 3 | 저장소: 스크립트가 들어 있는 저장소를 쓰고, 없으면 `~/SAVER` 로 복제 |
| 4 | Python: `websockets==9.1`(3.6 고정, AGENTS.md), `pyserial==3.5`, pip 21.3.1 (사용자 영역 `--user`) |
| 5 | 장치 권한: dialout, video, audio, plugdev 그룹. Teensy udev 규칙(`/etc/udev/rules.d/49-saver-teensy.rules`, 고정 이름 `/dev/saver-teensy`). ModemManager 끔(ttyACM 에 AT 명령을 보내지 않게). nvgetty 끔(40핀 UART `/dev/ttyTHS1` 을 레이더용으로 비움) |
| 6 | 브리지 서비스 `saver-bridge.service`: UI `http://127.0.0.1:8080`, WebSocket 8765, 기록 `~/saver_logs`, UTF-8 로캘 |
| 7 | 키오스크 `saver-kiosk`: Chromium 전체 화면, 화면 꺼짐과 잠금 끔, 커서 숨김, 1024x600, 음성 자동 재생 허용 |
| 8 | 명령과 바탕화면 아이콘(아래 표) |
| 9 | Claude Code (공식 설치기, `~/.local/bin/claude`) |
| 10 | 자동 시험 `jetson/tests/test_all.py` (하드웨어 없이) |

## 명령

| 명령 | 내용 |
|---|---|
| `saver-start` | 브리지 서비스와 키오스크 시작 (바탕화면 "SAVER 시작") |
| `saver-stop` | 키오스크와 브리지 정지 (바탕화면 "SAVER 정지") |
| `saver-status` | 서비스, `/dev/saver-teensy`, 시리얼 포트, UI 응답, 최근 기록, 소리 출력 |
| `saver-logs` | 브리지 실시간 기록 (`journalctl -u saver-bridge -f`) |
| `saver-link` | 최근 세션 기록의 링크 품질(처음과 마지막). 모터 끔 30분과 모터 켬 30분 비교용(`docs/noise_emi.md` 6절) |
| `saver-test` | 자동 시험 |
| `saver-update` | `git pull --ff-only` 뒤 브리지 다시 시작 |
| `saver-kiosk` | 키오스크만 다시 띄우기 |

## 옵션

| 옵션 | 내용 |
|---|---|
| `--autostart` | 부팅하면 자동 로그인, 브리지 서비스, 키오스크 자동 시작. 자동 업데이트 타이머와 업데이트 알림 끔 |
| `--no-claude` | Claude Code 설치 안 함 |
| `--no-test` | 마지막 자동 시험 건너뜀 |
| `--skip-apt` | apt 설치 건너뜀 |
| `--maxn` | 전원 모드 MAXN(10 W). 5 V 4 A 배럴잭 전원(J48 점퍼)일 때만 |
| `--branch`, `--repo-dir` | 복제 브랜치, 저장소 위치 |
| `--dry-run` | 바꾸지 않고 할 일만 출력 |

여러 번 실행해도 된다. `--autostart` 없이 다시 실행하면 키오스크 자동 시작 항목을 지우고 브리지 부팅 자동 시작을 끈다.
자동 로그인은 되돌리지 않는다. 원본은 `/etc/gdm3/custom.conf.saver.bak` 에 있다.

## 안전

- 브리지는 장치에 연결되면 S1 초기화에서 `PING`, `GET_STATUS`, `ARM`, `HOME` 을 자동으로 보낸다(상태머신 v2.4).
  실물 펌웨어의 `HOME` 동작과 상단 리미트를 확인하기 전에는 `--autostart` 를 쓰지 않고, 처음 연결은 모터와
  블로워 전원을 끈 상태에서 한다.
- 이 스크립트와 서비스는 압박(CPR) 명령을 보내지 않는다. 압박은 화면에서 사람이 시작해야만 하고,
  목표 깊이가 정해지지 않으면 Teensy 가 `START_CPR` 을 거부한다(정상).
- `config.sim.json` 은 쓰지 않는다. 확정값 `jetson/config.json` 이 있을 때만 `--config` 로 넘긴다.
- 물리 비상정지는 소프트웨어와 무관하다. `saver-stop` 은 비상정지가 아니다.

## UI 묶음 (`jetson/ui_dist`)

젯슨(glibc 2.27)은 Vite 5 가 요구하는 Node 18 을 돌리지 못해 UI 를 젯슨에서 빌드하지 않는다.
PC 에서 실기용 빌드를 만들어 저장소에 넣는다. `ui/src` 나 `protocol/state_machine_v2.json` 을 고치면 다시 만든다.

```bash
cd ui && npm run bundle:jetson      # build:kiosk 뒤 jetson/ui_dist 로 복사, BUILD_INFO.json 기록
git add ../jetson/ui_dist && git commit -m "[ui] 젯슨 UI 묶음 갱신"
```

설치 스크립트는 `BUILD_INFO.json` 의 ui/, protocol/ 트리 해시가 현재 저장소와 다르면 경고한다.

## Claude Code

공식 지원은 Ubuntu 20.04 이상, ARM64, 메모리 4 GB 이상이다(https://code.claude.com/docs/en/setup).
젯팩 4.6(Ubuntu 18.04, glibc 2.27)은 지원 목록에 없어, 스크립트는 공식 설치기로 설치를 시도하고
`claude --version` 이 안 되면 경고만 남긴다. 되면 재부팅 뒤 새 터미널에서 `claude` 를 한 번 실행해 로그인하고
`claude doctor` 로 점검한다(SSH 로 접속했으면 화면의 주소를 다른 기기 브라우저로 열고 받은 코드를 붙여 넣는다).
안 되면 노트북의 Claude Code 로 젯슨에 SSH 접속해 작업한다.

## 문제 해결

| 증상 | 확인 |
|---|---|
| `/dev/saver-teensy` 없음 | `lsusb` 에 `16c0:0483`. 다른 USB 케이블(데이터선) |
| 터치 좌표가 어긋남 | `xinput list` 로 터치 장치 ID, `xinput map-to-output <ID> HDMI-0` |
| 소리 안 남 | `pactl list short sinks`, `pactl set-default-sink <이름>` |
| 기록 시각이 틀림 | 젯슨에 RTC 전지가 없어 인터넷 없이 켜면 시각이 어긋난다. 인터넷 연결 뒤 `timedatectl` |
| 한글이 네모로 보임 | `fc-list : family \| grep -i -e pretendard -e "noto sans cjk"` |
| UI 가 옛 화면 | `saver-update`, 키오스크 다시 시작(`saver-stop` 뒤 `saver-start`) |
| SSH 에서 `saver-start` 를 했는데 화면에 키오스크가 안 뜸 | SSH 세션에는 화면 권한이 없다. 젯슨 화면의 바탕화면 아이콘 "SAVER 시작"으로 띄운다 |

## 검증 범위

- Python 3.6.15 에서 자동 시험 전체 통과, 브리지를 서비스와 같은 명령(`--serve jetson/ui_dist`, 절대 경로 기록)으로
  띄워 UI(8080), 자산, 음성 파일 응답과 키오스크 화면의 브리지 연결(S1 → 환자에게 이동)을 확인했다(PC, 가짜 Teensy).
- 스크립트는 `bash -n`, shellcheck, `--dry-run`(일반 사용자, `--autostart` 포함과 제외)으로 확인했다. 스크립트가 만드는
  보조 명령(`saver-*`)은 `bash -n` 과 shellcheck, sudoers 는 `visudo -cf`, 서비스 단위는 `systemd-analyze verify` 를 통과했다.
  **젯슨 실기에서는 아직 실행하지 않았다.**
  apt 패키지 이름, Chromium 플래그, gdm3 자동 로그인, Claude Code 실행 여부는 실기에서 확인한다.
