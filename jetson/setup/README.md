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

재부팅 뒤 헤드셋을 모니터 3.5 mm 단자에 꽂고 `saver-sound` 로 소리를 확인한 다음,
모터와 블로워 전원을 끈 채 Teensy 를 USB 로 연결하고 `saver-start` 한다(아래 "첫 연결 점검").

## 하는 일

| 순서 | 내용 |
|---|---|
| 1 | 시스템 확인: aarch64, L4T, Ubuntu 18.04, Python 3.6, 시각, 인터넷, 저장 공간. 시각이 인터넷과 2분 넘게 다르면 HTTP 응답의 Date 로 맞춘다(젯슨은 RTC 전지가 없고, 시각이 틀리면 HTTPS 와 apt 가 실패한다). curl 이 없는 새 젯팩이면 wget 으로 확인 |
| 2 | apt: Chromium(mp3 코덱 포함), 한글 글꼴(Noto CJK, 나눔), 시리얼(picocom), 오디오(mpg123 시험 재생 포함), 카메라 점검(v4l-utils), 빌드 도구, SSH |
| 3 | 저장소: 스크립트가 들어 있는 저장소를 쓰고, 없으면 `~/SAVER` 로 복제. 비공개 저장소 토큰은 12시간 동안 메모리에만 기억(`git credential cache`, 디스크에 쓰지 않음, 재부팅하면 지워짐) |
| 4 | Python: `websockets==9.1`(3.6 고정, AGENTS.md), `pyserial==3.5`, pip 21.3.1 (사용자 영역 `--user`) |
| 5 | 장치 권한: dialout, video, audio, plugdev 그룹. Teensy udev 규칙(`/etc/udev/rules.d/49-saver-teensy.rules`, 고정 이름 `/dev/saver-teensy`). ModemManager 끔(ttyACM 에 AT 명령을 보내지 않게). nvgetty 끔(40핀 UART `/dev/ttyTHS1` 을 레이더용으로 비움) |
| 6 | 브리지 서비스 `saver-bridge.service`: UI `http://127.0.0.1:8080`, WebSocket 8765, 기록 `~/saver_logs`, UTF-8 로캘 |
| 7 | 키오스크 `saver-kiosk`: Chromium 전체 화면, 화면 꺼짐과 잠금 끔, 커서 숨김, 1024x600, 음성 자동 재생 허용. 시작할 때 소리 기본 출력을 마지막으로 고른 종류(기본 HDMI)로 맞춤 |
| 8 | 명령과 바탕화면 아이콘(아래 표) |
| 9 | Claude Code (공식 설치기, `~/.local/bin/claude`) |
| 10 | 자동 시험 `jetson/tests/test_all.py` (하드웨어 없이) |

## 명령

| 명령 | 내용 |
|---|---|
| `saver-start` | 브리지 서비스와 키오스크 시작 (바탕화면 "SAVER 시작") |
| `saver-stop` | 키오스크와 브리지 정지 (바탕화면 "SAVER 정지") |
| `saver-off` | 키오스크와 브리지를 멈추고 젯슨을 끈다 (바탕화면 "SAVER 끄기"). 모니터에 신호 없음이 뜨고 20초 뒤 배터리(XT90)를 뽑는다. 전원을 그냥 끊으면 SD 카드 파일 시스템이 손상될 수 있다 |
| `saver-status` | 서비스, `/dev/saver-teensy`, 시리얼 포트, UI 응답, 최근 기록의 마지막 단계와 사건 8개(S1 점검 통과 여부), 시각과 동기화, IP 주소, 소리 출력 |
| `saver-logs` | 브리지 실시간 기록 (`journalctl -u saver-bridge -f`) |
| `saver-link` | 최근 세션 기록의 링크 품질(처음과 마지막). 모터 끔 30분과 모터 켬 30분 비교용(`docs/noise_emi.md` 6절) |
| `saver-sound` | 소리 출력을 HDMI(모니터 3.5 mm 단자의 헤드셋)로 맞추고 음성 하나를 재생. `saver-sound usb`(USB 헤드셋), `saver-sound list` |
| `saver-time` | 인터넷(HTTP Date)으로 시각 맞춤. 인터넷이 없으면 `saver-time "2026-10-24 09:30"`(한국 시각) |
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

- 브리지는 장치에 연결되면 S1 초기화에서 `PING`, `GET_STATUS`, `ARM`, `HOME` 을 자동으로 보낸다.
  종료 화면의 [대기로]도 S1 로 돌아가 같은 초기화를 다시 한다(상태머신 v2.5, D-47).
  실물 펌웨어의 `HOME` 동작과 상단 리미트를 확인하기 전에는 `--autostart` 를 쓰지 않고, 처음 연결은 모터와
  블로워 전원을 끈 상태에서 한다.
- 이 스크립트와 서비스는 압박(CPR) 명령을 보내지 않는다. 압박은 화면에서 사람이 시작해야만 하고,
  목표 깊이가 정해지지 않으면 Teensy 가 `START_CPR` 을 거부한다(정상).
- `config.sim.json` 은 쓰지 않는다. 확정값 `jetson/config.json` 이 있을 때만 `--config` 로 넘긴다.
- 물리 비상정지는 소프트웨어와 무관하다. `saver-stop` 은 비상정지가 아니다.

## 첫 연결 점검

모터와 블로워 전원은 끈 상태에서 한다. 결과는 `saver-status` 의 "마지막 단계와 사건"으로 본다.

| 순서 | 할 일 | 확인할 것 |
|---|---|---|
| 1 | 헤드셋을 모니터 3.5 mm 단자에 꽂고 `saver-sound` | 헤드셋에서 음성 하나가 들림 |
| 2 | Teensy USB 연결 뒤 `saver-start` | `/dev/saver-teensy` 있음, 사건에 `LINK_UP` 과 `INIT_DONE:S1->X_MOVE` |
| 3 | 켜는 순서를 바꿔 본다(젯슨 먼저, Teensy 먼저) | 두 경우의 S1 점검 결과(`INIT_DONE` 또는 `INIT_ERROR`) |
| 4 | USB 를 3초 뽑았다가 다시 꽂는다 | `LINK_DOWN`, `LINK_UP` 뒤의 단계와 S1 점검 결과 |

3, 4번은 실물에서 아직 확인하지 않았다. Teensy 를 먼저 켜면 S1 점검이 실패할 수 있다는 코드 대조 결과(10/11)가 있어
나온 결과를 그대로 기록한다.

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
| `git clone` 이나 설치가 `Could not resolve host` 로 실패 | 비공개 저장소 문제가 아니라 네트워크 문제다(비공개면 사용자 이름과 토큰을 묻는다). `ping -c 3 8.8.8.8` 이 되면 DNS 만 문제: `nmcli -t -f NAME con show --active` 로 연결 이름을 보고 `sudo nmcli con mod "<이름>" ipv4.dns "8.8.8.8 1.1.1.1" ipv4.ignore-auto-dns yes` 뒤 `sudo nmcli con up "<이름>"`. ping 도 안 되면 인터넷 연결 자체가 없다(케이블, 학교망 등록이나 로그인, 휴대폰 핫스팟) |
| `git clone` 이 `server certificate verification failed` 로 실패 | 먼저 `date`. 젯슨은 RTC 전지가 없어 새로 설치한 직후 날짜가 과거일 수 있고, 그러면 인증서가 아직 유효하지 않은 것으로 판정된다. `sudo date -s "YYYY-MM-DD HH:MM"`(지금 시각) 뒤 다시 실행. 날짜가 맞는데도 같으면 네트워크의 HTTPS 검사(학교, 기관망)나 오래된 인증서 묶음: `openssl s_client -connect github.com:443 -servername github.com </dev/null 2>/dev/null \| grep -m1 issuer` 로 발급자를 보고, 공인 기관이 아니면 다른 네트워크(휴대폰 핫스팟), 아니면 `sudo apt-get update && sudo apt-get install -y ca-certificates`. 인증서 검증을 끄지 않는다(`http.sslVerify false` 금지, 토큰이 노출될 수 있다) |
| 비공개 저장소 인증 | `Username` 에 GitHub 아이디, `Password` 에 GitHub 개인 액세스 토큰(계정 비밀번호는 안 됨. SAVER 저장소 Contents 읽기 권한) |
| `/dev/saver-teensy` 없음 | `lsusb` 에 `16c0:0483`. 다른 USB 케이블(데이터선) |
| 터치 좌표가 어긋남 | `xinput list` 로 터치 장치 ID, `xinput map-to-output <ID> HDMI-0` |
| 소리 안 남 | `saver-sound` (HDMI 출력 프로필이 꺼져 있으면 켠다), `saver-sound list`. 모니터 음량과 헤드셋 단자 확인 |
| 기록 시각이 틀림 | 젯슨에 RTC 전지가 없어 인터넷 없이 켜면 시각이 어긋난다. `saver-time`(인터넷, 휴대폰 핫스팟이면 충분) 또는 `saver-time "YYYY-MM-DD HH:MM"` |
| 설치 중 apt 가 "Release file ... is not valid yet" 으로 실패 | 시각이 틀림. `sudo date -s "YYYY-MM-DD HH:MM"` 으로 맞춘 뒤 다시 실행 |
| `saver-update` 가 토큰을 다시 묻는다 | 재부팅으로 토큰 기억(12시간, 메모리)이 지워졌다. 사용자 이름과 토큰을 다시 넣는다 |
| 한글이 네모로 보임 | `fc-list : family \| grep -i -e pretendard -e "noto sans cjk"` |
| UI 가 옛 화면 | `saver-update`, 키오스크 다시 시작(`saver-stop` 뒤 `saver-start`) |
| SSH 에서 `saver-start` 를 했는데 화면에 키오스크가 안 뜸 | SSH 세션에는 화면 권한이 없다. 젯슨 화면의 바탕화면 아이콘 "SAVER 시작"으로 띄운다 |

## 검증 범위

- Python 3.6.15 에서 자동 시험 전체 통과, 브리지를 서비스와 같은 명령(`--serve jetson/ui_dist`, 절대 경로 기록)으로
  띄워 UI(8080), 자산, 음성 파일 응답과 키오스크 화면의 브리지 연결(S1 → 환자에게 이동)을 확인했다(PC, 가짜 Teensy).
- 스크립트는 `bash -n`, shellcheck, `--dry-run`(일반 사용자, `--autostart` 포함과 제외)으로 확인했다. 스크립트가 만드는
  보조 명령(`saver-*`)은 `bash -n` 과 shellcheck, sudoers 는 `visudo -cf`, 서비스 단위는 `systemd-analyze verify` 를 통과했다.
- 10/15 판: `saver-status` 의 기록 요약을 Python 3.6.15(로캘 C 포함)에서 실제 `CsvLogger` 기록으로 확인했다.
  `saver-sound` 는 모의 `pactl`(HDMI 있음, HDMI 프로필 꺼짐, USB, 장치 없음), `saver-time` 과 시각 맞춤 함수는
  모의 HTTP 응답으로 확인했다. 실제 HTTP Date 응답은 작업 환경에서 80번 포트가 막혀 확인하지 못했다.
- 10/15 젯슨 실기 첫 실행(JetPack 4.6): 1~9단계 완료(시각, apt, 저장소, Python, 장치 권한, 서비스, 키오스크 명령,
  보조 명령, 글꼴과 시간대). 소리 기본 출력이 `alsa_output.platform-70030000.hda.hdmi-stereo` 로 자동 선택됨을 확인했다.
  10단계(Claude Code)에서 `ldd --version | head -n 1` 이 SIGPIPE(141)로 끊겨 pipefail 로 멈췄다. `getconf GNU_LIBC_VERSION`
  으로 바꾸고, 같은 꼴의 `| head` 3곳(nvpmodel, claude --version)을 끝까지 읽는 awk 로 바꿨다(가짜 ldd 로 재현과 수정 확인).
  고친 판으로 다시 실행해 10~12단계도 끝났다: Claude Code 2.1.299 설치와 `claude --version` 확인(18.04 에서도 실행됨,
  로그인은 미확인), 자동 시험 252/252 통과(젯슨 Python 3.6). gdm3 자동 로그인(`--autostart`)은 아직 쓰지 않았다.
- Teensy 를 연결하지 않으면 키오스크는 S1 "시스템 점검 중" 화면에 머문다. 브리지 초기화가 USB 링크를 기다리는
  단계(wait_link)에서 시간 제한 없이 기다리기 때문이고, 연결 전에는 화면이 스스로 넘어가지 않는 설계(D-44)대로다.
  브리지(WebSocket)가 끊겼을 때만 "장치와 연결하고 있습니다" 화면이 나온다.
