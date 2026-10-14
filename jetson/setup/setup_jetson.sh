#!/usr/bin/env bash
# ============================================================================
# SAVER 젯슨 나노 일괄 설정
#   대상: JetPack 4.6.x (L4T R32), Ubuntu 18.04, aarch64, Python 3.6
#
# 젯팩을 새로 깐 젯슨에서 터미널을 열고 한 번 실행하면 SAVER 실행에 필요한 설정을 끝낸다.
#    1. 시스템 확인            aarch64, L4T, Ubuntu 18.04, Python 3.6, 시각(틀리면 인터넷 기준으로 맞춤),
#                              인터넷, 저장 공간. curl 이 없는 새 젯팩이면 wget 으로 확인
#    2. apt 패키지             Chromium(mp3 코덱), 한글 글꼴, 시리얼, 오디오(mpg123 시험 재생), 카메라 점검, 빌드 도구
#    3. 저장소                 이 스크립트가 저장소 안에 있으면 그대로, 아니면 ~/SAVER 로 복제.
#                              비공개 저장소 토큰은 12시간 동안 메모리에만 기억(git credential cache)
#    4. Python 패키지          websockets==9.1(3.6 고정), pyserial (사용자 영역 --user)
#    5. 장치 권한              dialout 등 그룹, Teensy udev 규칙과 /dev/saver-teensy,
#                              ModemManager 끔, nvgetty 끔(40핀 UART /dev/ttyTHS1 을 레이더용으로 비움)
#    6. 브리지 서비스          saver-bridge.service (UI http://127.0.0.1:8080, WebSocket 8765, 로그 ~/saver_logs)
#    7. 키오스크               Chromium 전체 화면, 화면 꺼짐 방지, 커서 숨김, 음성 자동 재생 허용,
#                              시작할 때 소리 출력을 HDMI(모니터 3.5 mm 단자의 헤드셋)로
#    8. 편의 명령              saver-start, saver-stop, saver-off, saver-status, saver-logs, saver-link,
#                              saver-sound, saver-time, saver-kiosk, saver-test, saver-update, 바탕화면 아이콘
#    9. Claude Code
#   10. 자동 시험              jetson/tests/test_all.py (하드웨어 없이)
#
# 사용 (sudo 없이 실행. 필요할 때 비밀번호를 묻는다)
#   bash jetson/setup/setup_jetson.sh                # 설치만. 부팅 자동 시작은 켜지 않는다
#   bash jetson/setup/setup_jetson.sh --autostart    # 부팅하면 자동 로그인, 브리지와 키오스크 자동 시작
#   bash jetson/setup/setup_jetson.sh --help         # 옵션
#
# 안전 (AGENTS.md)
#   - 브리지는 장치에 연결되면 S1 초기화에서 PING, GET_STATUS, ARM, HOME 을 자동으로 보낸다.
#     종료 화면의 [대기로]도 S1 로 돌아가 같은 초기화를 다시 한다(상태머신 v2.5).
#     실물 펌웨어의 HOME 동작과 상단 리미트를 확인하기 전까지는 부팅 자동 시작을 기본으로 켜지 않는다.
#     처음 연결은 모터와 블로워 전원을 끈 상태에서 한다.
#   - 이 스크립트는 압박(CPR) 명령을 보내지 않는다. 압박은 화면에서 사람이 시작해야만 시작된다.
#   - config.sim.json(시뮬레이션 전용)은 실기에 쓰지 않는다. jetson/config.json(확정값)이 있을 때만 쓴다.
# ============================================================================
set -Eeuo pipefail

SCRIPT_VERSION="2026-10-15"
DEFAULT_REPO_URL="https://github.com/SangHyeon-Shin/SAVER.git"
# jetson/, ui/ 최신 작업 브랜치(10/14 기준 main 은 10/4 이후 변경이 반영되지 않았다)
DEFAULT_BRANCH="claude/saver-project-onboarding-xpmdhw"
PRETENDARD_URL="https://github.com/orioncactus/pretendard/releases/download/v1.3.9/Pretendard-1.3.9.zip"
HTTP_PORT=8080
WS_PORT=8765

AUTOSTART=0
INSTALL_CLAUDE=1
RUN_TEST=1
SKIP_APT=0
DRY_RUN=0
MAXN=0
BRANCH="${SAVER_BRANCH:-$DEFAULT_BRANCH}"
REPO_URL="${SAVER_REPO_URL:-$DEFAULT_REPO_URL}"
REPO_DIR="${SAVER_REPO_DIR:-}"

usage() {
  cat <<EOF
SAVER 젯슨 나노 일괄 설정 ($SCRIPT_VERSION)

  bash setup_jetson.sh [옵션]

  --autostart        부팅하면 자동 로그인, 브리지 서비스와 키오스크 자동 시작
                     (실물 HOME 동작과 상단 리미트를 확인한 뒤에 켠다)
  --no-claude        Claude Code 를 설치하지 않는다
  --no-test          마지막 자동 시험(jetson/tests/test_all.py)을 건너뛴다
  --skip-apt         apt 패키지 설치를 건너뛴다(이미 설치했을 때)
  --maxn             전원 모드를 MAXN(10 W)으로. 5 V 4 A 배럴잭 전원(J48 점퍼)일 때만
  --branch NAME      저장소를 새로 복제할 때 쓸 브랜치 (기본: $DEFAULT_BRANCH)
  --repo-dir DIR     저장소 위치를 직접 지정
  --dry-run          바꾸지 않고 할 일만 출력
  -h, --help         이 도움말

  환경변수 SAVER_REPO_URL, SAVER_BRANCH, SAVER_REPO_DIR 로도 지정할 수 있다.
EOF
}

while [ $# -gt 0 ]; do
  case "$1" in
    --autostart) AUTOSTART=1 ;;
    --no-claude) INSTALL_CLAUDE=0 ;;
    --no-test) RUN_TEST=0 ;;
    --skip-apt) SKIP_APT=1 ;;
    --maxn) MAXN=1 ;;
    --dry-run) DRY_RUN=1 ;;
    --branch) BRANCH="${2:?--branch 뒤에 이름}"; shift ;;
    --repo-dir) REPO_DIR="${2:?--repo-dir 뒤에 경로}"; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "알 수 없는 옵션: $1"; usage; exit 2 ;;
  esac
  shift
done

# ---------------------------------------------------------------- 출력과 기록
LOG_FILE="$HOME/saver_setup_$(date +%Y%m%d_%H%M%S).log"
if [ "$DRY_RUN" -eq 0 ]; then
  exec > >(tee -a "$LOG_FILE") 2>&1
fi

STEP=0
WARNINGS=()
step() { STEP=$((STEP + 1)); printf '\n\033[1;34m[%d] %s\033[0m\n' "$STEP" "$*"; }
info() { printf '    %s\n' "$*"; }
ok()   { printf '    \033[32m완료\033[0m %s\n' "$*"; }
warn() { printf '    \033[33m주의\033[0m %s\n' "$*"; WARNINGS+=("$*"); }
die()  { printf '\n\033[31m중단: %s\033[0m\n' "$*"; exit 1; }
trap 'die "명령 실패 (줄 $LINENO): $BASH_COMMAND. 기록: $LOG_FILE"' ERR

# 실행 감싸개: --dry-run 이면 출력만
run() {
  if [ "$DRY_RUN" -eq 1 ]; then printf '    (dry-run) %s\n' "$*"; return 0; fi
  "$@"
}
# 루트 권한 파일 쓰기: write_root 경로 권한 < 내용
write_root() {
  local path="$1" mode="$2" tmp
  tmp="$(mktemp)"; cat > "$tmp"
  if [ "$DRY_RUN" -eq 1 ]; then
    printf '    (dry-run) %s (%s) 쓰기:\n' "$path" "$mode"; sed 's/^/        | /' "$tmp"; rm -f "$tmp"; return 0
  fi
  sudo install -D -m "$mode" "$tmp" "$path"; rm -f "$tmp"
}
# 사용자 파일 쓰기: write_user 경로 권한 < 내용
write_user() {
  local path="$1" mode="$2" tmp
  tmp="$(mktemp)"; cat > "$tmp"
  if [ "$DRY_RUN" -eq 1 ]; then
    printf '    (dry-run) %s (%s) 쓰기:\n' "$path" "$mode"; sed 's/^/        | /' "$tmp"; rm -f "$tmp"; return 0
  fi
  install -D -m "$mode" "$tmp" "$path"; rm -f "$tmp"
}
# 보조 명령 쓰기: write_cmd 경로 "설치 때 정한 값(변수 정의 줄)" < 본문(글자 그대로, 따옴표 heredoc)
write_cmd() {
  local path="$1" vars="$2"
  { printf '#!/usr/bin/env bash\n%s\n' "$vars"; cat; } | write_user "$path" 0755
}
# pipefail 에서 'grep -q' 가 앞 명령을 SIGPIPE 로 끊으면 일치해도 거짓이 되므로 출력을 받아 비교한다
have_unit() { [ -n "$(systemctl list-unit-files "$1" --no-legend 2>/dev/null)" ]; }
# 새 젯팩에는 curl 이 없을 수 있다(2단계에서 설치). 그 전에는 wget 으로 확인한다
url_ok() {
  if command -v curl >/dev/null 2>&1; then curl -fsS --max-time 8 -o /dev/null "$1" 2>/dev/null
  else wget -q -T 8 -O /dev/null "$1" 2>/dev/null; fi
}
# HTTP 응답 머리글의 Date 를 epoch 초로. 인증서 확인이 필요 없는 http 주소를 쓴다(시각이 틀리면 https 가 실패한다)
url_epoch() {
  local d
  if command -v curl >/dev/null 2>&1; then
    d="$(curl -sSI --max-time 8 "$1" 2>/dev/null | tr -d '\r' | awk -F': ' 'tolower($1) == "date" {print $2; exit}')" || true
  else
    d="$(wget -S -q -T 8 --spider "$1" 2>&1 | tr -d '\r' | awk -F': ' 'tolower($1) ~ /^ *date$/ {print $2; exit}')" || true
  fi
  [ -n "$d" ] && date -d "$d" +%s 2>/dev/null
}

printf '\033[1mSAVER 젯슨 나노 일괄 설정 %s\033[0m%s\n' "$SCRIPT_VERSION" "$([ "$DRY_RUN" -eq 1 ] && echo '  (dry-run: 아무것도 바꾸지 않음)')"
[ "$DRY_RUN" -eq 0 ] && info "기록: $LOG_FILE"

# ---------------------------------------------------------------- 1. 시스템 확인
step "시스템 확인"
[ "$(id -u)" -ne 0 ] || die "root 로 실행하지 마세요. 일반 사용자로 실행하면 필요할 때 sudo 비밀번호를 묻습니다."
USER_NAME="$(id -un)"
USER_GROUP="$(id -gn)"
ARCH="$(uname -m)"
if [ "$ARCH" != "aarch64" ]; then
  [ "$DRY_RUN" -eq 1 ] || die "aarch64(젯슨)가 아닙니다: $ARCH"
  warn "aarch64 가 아님($ARCH). dry-run 이라 계속"
fi
if [ -f /etc/nv_tegra_release ]; then
  info "L4T: $(head -n 1 /etc/nv_tegra_release | sed 's/^# //')"
else
  warn "/etc/nv_tegra_release 없음(젯팩이 아닐 수 있음)"
fi
UBU="$(lsb_release -rs 2>/dev/null || echo '?')"
info "Ubuntu $UBU, 사용자 $USER_NAME"
[ "$UBU" = "18.04" ] || warn "Ubuntu 18.04 가 아님($UBU). JetPack 4.6 기준으로 만든 스크립트"
PY_VER="$(python3 -c 'import sys; print("%d.%d" % sys.version_info[:2])' 2>/dev/null || echo '?')"
info "Python $PY_VER"
FREE_MB="$(df -Pm "$HOME" | awk 'NR==2 {print $4}')"
info "남은 저장 공간 ${FREE_MB} MB"
[ "${FREE_MB:-0}" -ge 2048 ] || warn "저장 공간 2 GB 미만. Chromium, 글꼴 설치가 실패할 수 있음"
if [ "$DRY_RUN" -eq 0 ]; then
  sudo -v || die "sudo 권한이 필요합니다"
  # 긴 설치 중 sudo 시간 만료 방지. 실패해도 본 스크립트를 중단시키지 않게 ERR 트랩을 끄고, 출력은 기록에 물리지 않는다
  ( trap - ERR; set +e; while kill -0 "$$" 2>/dev/null; do sudo -n true; sleep 50; done ) >/dev/null 2>&1 &
fi
# 시각: 젯슨에는 RTC 전지가 없어 인터넷 동기화 전에는 시각이 틀릴 수 있다(NTP 를 막은 학교망 포함).
# 틀린 시각이면 HTTPS 인증서 확인, apt 의 Release 유효 기간 확인이 실패하고 기록 파일 시각도 틀린다
WEB_TS=""
for u in http://www.google.com http://www.naver.com; do
  WEB_TS="$(url_epoch "$u" || true)"
  [ -z "$WEB_TS" ] || break
done
if [ -z "$WEB_TS" ]; then
  warn "인터넷 시각을 받지 못함. 지금 시각 $(date '+%Y-%m-%d %H:%M'). 틀리면 apt 와 HTTPS 가 실패하므로 sudo date -s \"YYYY-MM-DD HH:MM\" 으로 맞춘 뒤 다시 실행"
else
  NOW_TS="$(date +%s)"
  DIFF=$(( WEB_TS > NOW_TS ? WEB_TS - NOW_TS : NOW_TS - WEB_TS ))
  if [ "$DIFF" -gt 120 ]; then
    run sudo date -s "@$WEB_TS" >/dev/null
    ok "시각을 인터넷 기준으로 맞춤 (${DIFF}초 차이 → $(date '+%Y-%m-%d %H:%M %Z'))"
  else
    ok "시각 $(date '+%Y-%m-%d %H:%M %Z') (인터넷과 ${DIFF}초 차이)"
  fi
fi
if url_ok https://pypi.org/simple/pip/; then
  ok "인터넷 연결"
else
  [ "$SKIP_APT" -eq 1 ] || [ "$DRY_RUN" -eq 1 ] || die "인터넷에 연결되지 않습니다(pypi.org). 와이파이나 유선을 연결한 뒤 다시 실행하세요"
  warn "인터넷 확인 실패"
fi

# ---------------------------------------------------------------- 2. apt 패키지
step "apt 패키지"
APT_PKGS=(
  git curl ca-certificates unzip usbutils picocom
  python3-pip python3-dev python3-setuptools python3-wheel build-essential
  chromium-browser chromium-codecs-ffmpeg-extra unclutter xdotool x11-xserver-utils
  fonts-noto-cjk fonts-nanum fontconfig
  alsa-utils pulseaudio-utils mpg123 v4l-utils
  openssh-server
)
if [ "$SKIP_APT" -eq 1 ]; then
  info "건너뜀(--skip-apt)"
else
  # 젯팩 첫 부팅 직후에는 자동 업데이트가 apt 잠금을 잡고 있을 수 있다
  if [ "$DRY_RUN" -eq 0 ]; then
    for _ in $(seq 1 60); do
      if sudo fuser /var/lib/dpkg/lock-frontend /var/lib/apt/lists/lock >/dev/null 2>&1; then
        info "다른 apt 작업이 끝나기를 기다리는 중..."; sleep 5
      else break; fi
    done
  fi
  run sudo apt-get update
  run sudo env DEBIAN_FRONTEND=noninteractive apt-get install -y "${APT_PKGS[@]}"
  ok "${#APT_PKGS[@]}개 패키지"
fi

# ---------------------------------------------------------------- 3. 저장소
step "저장소"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")" && pwd)"   # 파이프로 받아 실행해도(bash -s) 멈추지 않게
if [ -z "$REPO_DIR" ] && [ -f "$SCRIPT_DIR/../saver/bridge.py" ]; then
  REPO_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
  info "스크립트가 들어 있는 저장소를 씀"
fi
# 비공개 저장소: 복제 때 넣은 토큰을 12시간 동안 메모리에만 기억해 saver-update 가 다시 묻지 않게 한다(디스크에 쓰지 않음)
if command -v git >/dev/null 2>&1 && [ -z "$(git config --global credential.helper 2>/dev/null || true)" ]; then
  run git config --global credential.helper 'cache --timeout=43200'
  info "GitHub 토큰은 12시간 동안 메모리에만 기억(git credential cache, 재부팅하면 지워짐)"
fi
if [ -z "$REPO_DIR" ]; then
  REPO_DIR="$HOME/SAVER"
  if [ -d "$REPO_DIR/.git" ]; then
    info "기존 저장소 $REPO_DIR"
  else
    info "복제: $REPO_URL ($BRANCH) → $REPO_DIR"
    info "비공개 저장소면 GitHub 사용자 이름과 개인 액세스 토큰(PAT)을 비밀번호 자리에 넣는다"
    run git clone --branch "$BRANCH" "$REPO_URL" "$REPO_DIR"
  fi
fi
[ "$DRY_RUN" -eq 1 ] || [ -f "$REPO_DIR/jetson/saver/bridge.py" ] || die "저장소에 jetson/saver/bridge.py 가 없습니다: $REPO_DIR"
JETSON_DIR="$REPO_DIR/jetson"
UI_DIST="$JETSON_DIR/ui_dist"
if [ -d "$REPO_DIR/.git" ]; then
  info "위치 $REPO_DIR, 브랜치 $(git -C "$REPO_DIR" rev-parse --abbrev-ref HEAD 2>/dev/null || echo '?'), 커밋 $(git -C "$REPO_DIR" rev-parse --short HEAD 2>/dev/null || echo '?')"
fi
ok "저장소 준비"

# ---------------------------------------------------------------- 4. Python 패키지
step "Python 패키지 (사용자 영역)"
if [ "$PY_VER" = "3.6" ]; then
  WS_SPEC="websockets==9.1"          # AGENTS.md: 3.6 에서는 9.1 고정
  PIP_SPEC="pip<22"                  # pip 21.3.1 이 3.6 을 지원하는 마지막 판
else
  WS_SPEC="websockets"
  PIP_SPEC="pip"
  warn "Python $PY_VER: websockets 9.1 고정은 3.6 기준. 최신판 설치"
fi
run python3 -m pip install --user --upgrade "$PIP_SPEC"
run python3 -m pip install --user --upgrade "$WS_SPEC" "pyserial==3.5"
if [ "$DRY_RUN" -eq 0 ]; then
  PYV="$(python3 -c 'import websockets, serial; print("websockets %s, pyserial %s" % (websockets.__version__, serial.__version__))')"
  ok "$PYV"
fi

# ---------------------------------------------------------------- 5. 장치 권한
step "장치 권한 (Teensy USB, UART, 카메라, 오디오)"
for g in dialout video audio plugdev; do
  if getent group "$g" >/dev/null; then run sudo usermod -aG "$g" "$USER_NAME"; fi
done
ok "그룹 dialout, video, audio, plugdev (새 로그인부터 적용)"
write_root /etc/udev/rules.d/49-saver-teensy.rules 0644 <<'EOF'
# SAVER: Teensy 4.1 (PJRC, VID 16c0). jetson/setup/setup_jetson.sh 가 만든다.
# ModemManager 가 포트를 열어 AT 명령을 보내지 않게 하고, 시리얼 포트에 고정 이름 /dev/saver-teensy 를 붙인다.
ATTRS{idVendor}=="16c0", ATTRS{idProduct}=="04[789B]?", ENV{ID_MM_DEVICE_IGNORE}="1", ENV{ID_MM_PORT_IGNORE}="1"
KERNEL=="ttyACM*", SUBSYSTEMS=="usb", ATTRS{idVendor}=="16c0", ATTRS{idProduct}=="0483", GROUP="dialout", MODE="0660", SYMLINK+="saver-teensy"
# Teensy Loader(펌웨어 올리기)용 HID
KERNEL=="hidraw*", ATTRS{idVendor}=="16c0", ATTRS{idProduct}=="04[789ABCD]?", MODE="0666"
EOF
run sudo udevadm control --reload-rules
run sudo udevadm trigger --subsystem-match=tty || true
ok "udev 규칙 49-saver-teensy.rules (/dev/saver-teensy)"
if have_unit ModemManager.service; then
  run sudo systemctl disable --now ModemManager.service || true
  ok "ModemManager 끔 (/dev/ttyACM* 를 건드리지 않게)"
fi
if have_unit nvgetty.service; then
  run sudo systemctl disable --now nvgetty.service || true
  ok "nvgetty 끔 (40핀 UART /dev/ttyTHS1 의 직렬 콘솔을 끄고 레이더용으로 비움)"
fi

# ---------------------------------------------------------------- 6. 브리지 서비스
step "브리지 서비스 saver-bridge"
LOG_DIR="$HOME/saver_logs"
run mkdir -p "$LOG_DIR"
CONFIG_ARG=""
if [ -f "$JETSON_DIR/config.json" ]; then
  CONFIG_ARG=" --config $JETSON_DIR/config.json"
  info "확정 설정 jetson/config.json 사용"
else
  info "jetson/config.json 없음: 미확정 값은 0 으로 두고 보내지 않는다(START_CPR 거부가 정상, config.sim.json 은 쓰지 않음)"
fi
if [ -f "$UI_DIST/index.html" ] && [ -d "$REPO_DIR/.git" ] && [ -f "$UI_DIST/BUILD_INFO.json" ]; then
  B_UI="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1])).get("ui_tree",""))' "$UI_DIST/BUILD_INFO.json")"
  B_PR="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1])).get("protocol_tree",""))' "$UI_DIST/BUILD_INFO.json")"
  C_UI="$(git -C "$REPO_DIR" rev-parse HEAD:ui 2>/dev/null || true)"
  C_PR="$(git -C "$REPO_DIR" rev-parse HEAD:protocol 2>/dev/null || true)"
  if [ -n "$B_UI" ] && [ "$B_UI" = "$C_UI" ] && [ "$B_PR" = "$C_PR" ]; then
    ok "UI 묶음 jetson/ui_dist 가 현재 ui/, protocol/ 과 같은 소스"
  else
    warn "UI 묶음 jetson/ui_dist 가 현재 ui/ 또는 protocol/ 소스와 다름. PC 에서 'cd ui && npm run bundle:jetson' 뒤 커밋하고 saver-update"
  fi
elif [ ! -f "$UI_DIST/index.html" ]; then
  [ "$DRY_RUN" -eq 1 ] || die "UI 묶음 $UI_DIST/index.html 이 없습니다. PC 에서 'cd ui && npm run bundle:jetson' 뒤 커밋하세요"
fi
PY_BIN="$(command -v python3)"
write_root /etc/systemd/system/saver-bridge.service 0644 <<EOF
# SAVER 젯슨 브리지. jetson/setup/setup_jetson.sh 가 만든다.
# 장치에 연결되면 S1 초기화(PING, GET_STATUS, ARM, HOME)를 자동으로 보낸다. 종료 화면의 [대기로]도 S1 로 돌아가
# 같은 초기화를 다시 한다(상태머신 v2.5). 압박은 화면에서 사람이 시작해야만 한다.
[Unit]
Description=SAVER Jetson bridge (state machine, Teensy USB, UI :$HTTP_PORT, WebSocket :$WS_PORT)
After=network.target
StartLimitIntervalSec=0

[Service]
Type=simple
User=$USER_NAME
Group=$USER_GROUP
WorkingDirectory=$JETSON_DIR
# Python 3.6 은 C 로캘에서 한글 출력과 기록 파일 쓰기가 실패한다
Environment=HOME=$HOME
Environment=LANG=C.UTF-8
Environment=LC_ALL=C.UTF-8
Environment=PYTHONIOENCODING=utf-8
Environment=PYTHONUNBUFFERED=1
ExecStart=$PY_BIN -m saver.bridge --serial /dev/saver-teensy --serve $UI_DIST --http-port $HTTP_PORT --port $WS_PORT --logdir $LOG_DIR$CONFIG_ARG
Restart=on-failure
RestartSec=3

[Install]
WantedBy=multi-user.target
EOF
SYSTEMCTL="$(command -v systemctl)"
write_root /etc/sudoers.d/saver-bridge 0440 <<EOF
# SAVER: 브리지 서비스 시작, 정지를 비밀번호 없이 (saver-start, saver-stop)
$USER_NAME ALL=(root) NOPASSWD: $SYSTEMCTL start saver-bridge, $SYSTEMCTL stop saver-bridge, $SYSTEMCTL restart saver-bridge
EOF
if [ "$DRY_RUN" -eq 0 ]; then
  sudo visudo -cf /etc/sudoers.d/saver-bridge >/dev/null || { sudo rm -f /etc/sudoers.d/saver-bridge; die "sudoers 문법 오류"; }
fi
run sudo systemctl daemon-reload
if [ "$AUTOSTART" -eq 1 ]; then
  run sudo systemctl enable saver-bridge.service
  ok "saver-bridge 부팅 자동 시작 켬 (--autostart)"
else
  run sudo systemctl disable saver-bridge.service >/dev/null 2>&1 || true
  ok "saver-bridge 설치 (부팅 자동 시작 꺼짐. saver-start 로 시작)"
fi

# ---------------------------------------------------------------- 7. 키오스크와 화면
step "키오스크 (Chromium 전체 화면)"
BIN="$HOME/.local/bin"
write_user "$BIN/saver-kiosk" 0755 <<EOF
#!/usr/bin/env bash
# SAVER 터치 UI 키오스크. 브리지(:$HTTP_PORT)가 뜰 때까지 기다렸다가 Chromium 전체 화면으로 연다.
URL="http://127.0.0.1:$HTTP_PORT/"
export DISPLAY="\${DISPLAY:-:0}"
# 화면 꺼짐, 화면 보호기, 잠금 끔 (GNOME 이 아니면 gsettings 는 조용히 실패)
xset s off -dpms s noblank 2>/dev/null || true
gsettings set org.gnome.desktop.session idle-delay 0 2>/dev/null || true
gsettings set org.gnome.desktop.screensaver lock-enabled false 2>/dev/null || true
gsettings set org.gnome.desktop.screensaver idle-activation-enabled false 2>/dev/null || true
gsettings set org.gnome.settings-daemon.plugins.power sleep-inactive-ac-type 'nothing' 2>/dev/null || true
gsettings set org.gnome.desktop.notifications show-banners false 2>/dev/null || true
# 7인치 화면: 연결된 출력에 1024x600 이 있으면 맞춘다
OUT="\$(xrandr --query 2>/dev/null | awk '/ connected/ {print \$1; exit}')"
if [ -n "\$OUT" ] && xrandr --query | awk -v o="\$OUT" '\$1==o {f=1; next} /^[^ ]/ {f=0} f' | grep -q '1024x600'; then
  xrandr --output "\$OUT" --mode 1024x600 2>/dev/null || true
fi
# 커서 숨김
pkill -x unclutter 2>/dev/null || true
(unclutter -idle 0.5 -root >/dev/null 2>&1 &)
# 소리 출력: saver-sound 로 마지막에 고른 종류(기본 HDMI, 모니터 3.5 mm 단자의 헤드셋)가 있으면 기본 출력으로
"$BIN/saver-sound" --auto >/dev/null 2>&1 || true
# 브리지 대기(최대 120초. 부팅 직후에는 브리지가 늦게 뜰 수 있다)
for _ in \$(seq 1 120); do curl -fsS -o /dev/null --max-time 2 "\$URL" && break; sleep 1; done
exec chromium-browser --kiosk "\$URL" \\
  --user-data-dir="\$HOME/.config/saver-kiosk" --incognito --password-store=basic \\
  --no-first-run --noerrdialogs --disable-infobars --disable-session-crashed-bubble --disable-translate \\
  --check-for-update-interval=31536000 --overscroll-history-navigation=0 --disable-pinch \\
  --touch-events=enabled --autoplay-policy=no-user-gesture-required --window-position=0,0
EOF
ok "saver-kiosk"

# ---------------------------------------------------------------- 8. 편의 명령과 아이콘
step "편의 명령 (~/.local/bin)"
write_user "$BIN/saver-start" 0755 <<EOF
#!/usr/bin/env bash
# 브리지 서비스와 키오스크를 시작한다. 처음 연결은 모터와 블로워 전원을 끈 상태에서.
sudo -n $SYSTEMCTL start saver-bridge || sudo $SYSTEMCTL start saver-bridge
# 키오스크(브리지를 기다리는 스크립트 또는 Chromium)가 이미 있으면 다시 띄우지 않는다
if ! pgrep -f 'saver-kiosk' >/dev/null; then
  (setsid "$BIN/saver-kiosk" >/dev/null 2>&1 &)
fi
echo "시작: UI http://127.0.0.1:$HTTP_PORT  (상태 확인: saver-status)"
EOF
write_user "$BIN/saver-stop" 0755 <<EOF
#!/usr/bin/env bash
# 키오스크와 브리지 서비스를 멈춘다. Teensy 는 Jetson 이 멈춰도 스스로 안전 상태를 유지한다.
pkill -f 'saver-kiosk' 2>/dev/null || true
sudo -n $SYSTEMCTL stop saver-bridge || sudo $SYSTEMCTL stop saver-bridge
echo "정지"
EOF
write_user "$BIN/saver-off" 0755 <<EOF
#!/usr/bin/env bash
# 안전하게 끄기: 키오스크와 브리지를 멈추고 젯슨을 끈다. 젯슨 전원을 그냥 끊으면 SD 카드 파일 시스템이 손상될 수 있다.
# 모니터에 '신호 없음'이 뜨고 20초 뒤에 배터리(XT90)를 뽑는다. Teensy 는 브리지가 멈추면 스스로 안전 상태로 간다.
"$BIN/saver-stop" || true
sync
systemctl poweroff 2>/dev/null || sudo $SYSTEMCTL poweroff
EOF
write_cmd "$BIN/saver-status" "HTTP_PORT=$HTTP_PORT
LOG_DIR=\"$LOG_DIR\"" <<'EOF'
# 한눈에 보기: 서비스, Teensy, UI, 마지막 단계와 사건(S1 점검 통과 여부), 시각, 주소, 소리 출력
echo "== 브리지 서비스"; systemctl --no-pager --lines=5 status saver-bridge 2>/dev/null | sed -n '1,8p'
echo "== Teensy";       ls -l /dev/saver-teensy 2>/dev/null || echo "  /dev/saver-teensy 없음 (USB 연결 확인)"
echo "== 시리얼 포트";  ls /dev/ttyACM* /dev/ttyUSB* /dev/ttyTHS* 2>/dev/null | tr '\n' ' '; echo
echo "== UI"
if curl -fsS -o /dev/null --max-time 2 "http://127.0.0.1:$HTTP_PORT/"; then echo "  http://127.0.0.1:$HTTP_PORT 응답"; else echo "  응답 없음"; fi
f="$(ls -t "$LOG_DIR"/saver_*.csv 2>/dev/null | head -n 1)"
echo "== 마지막 단계와 사건 ${f:+($(basename "$f"))}"
if [ -n "$f" ]; then
  # Python 3.6 은 로캘이 C 이면(LANG 없는 SSH 등) 한글 출력에서 멈춘다
  PYTHONIOENCODING=utf-8 python3 - "$f" <<'PY'
import collections, csv, datetime, sys
last, mcu, ev = None, "", collections.deque(maxlen=8)
with open(sys.argv[1], encoding="utf-8") as fp:
    for r in csv.DictReader(fp):
        last = r
        if r.get("source") == "mcu" and r.get("substate"):
            mcu = r["substate"]
        d = r.get("decision") or ""
        if d and not d.startswith("LINK "):
            ev.append(r)
def hms(r):
    try:
        return datetime.datetime.fromtimestamp(int(r["ts_ms"]) / 1000.0).strftime("%H:%M:%S")
    except (KeyError, TypeError, ValueError):
        return "?"
if last is None:
    print("  기록이 비어 있음")
else:
    print("  단계 %s, Teensy %s (마지막 기록 %s)" % (last.get("state") or "?", mcu or "?", hms(last)))
    for r in ev:
        print("  %s %-4s %s" % (hms(r), r.get("source", ""), r.get("decision", "")))
PY
else
  echo "  기록 없음 (브리지를 한 번도 시작하지 않음)"
fi
echo "== 시각";          date '+  %Y-%m-%d %H:%M:%S %Z'; timedatectl status 2>/dev/null | grep -i 'synchronized' | sed 's/^ */  /'
echo "== 주소 (SSH)";    echo "  $(hostname -I 2>/dev/null)"
echo "== 소리 출력";     echo "  기본 $(pactl info 2>/dev/null | awk -F': ' '/^Default Sink/ {print $2}')"
pactl list short sinks 2>/dev/null | awk '{print "  " $2}'
EOF
write_cmd "$BIN/saver-sound" "VOICE_DIR=\"$UI_DIST/voice\"" <<'EOF'
# 음성 출력 장치 고르기와 시험 재생. 구조자는 헤드셋으로 듣는다(D-40).
# 젯슨 나노에는 아날로그 소리 단자가 없어 HDMI 로 모니터에 보내고, 헤드셋은 모니터 3.5 mm 단자에 꽂는다(docs/noise_emi.md).
#   saver-sound          HDMI 로 맞추고 음성 하나를 재생
#   saver-sound usb      USB 헤드셋이나 USB 사운드카드로 맞추고 재생
#   saver-sound list     출력 장치 목록
#   saver-sound --auto   마지막으로 고른 종류(없으면 HDMI)를 기본 출력으로만 바꾼다(키오스크 시작 때, 소리 없음)
CONF="$HOME/.config/saver/sound_out"
mode="${1:-hdmi}"
find_sink() { pactl list short sinks 2>/dev/null | awk -v k="$1" 'index(tolower($2), k) {print $2; exit}'; }
hdmi_profile() {   # HDMI 출력이 꺼진 프로필이면 켠다
  local card p
  for card in $(pactl list short cards 2>/dev/null | awk '{print $2}' | grep -i -e hda -e hdmi); do
    for p in output:hdmi-stereo output:hdmi-stereo-extra1; do
      if pactl set-card-profile "$card" "$p" 2>/dev/null; then return 0; fi
    done
  done
  return 1
}
case "$mode" in
  list)
    pactl list short sinks
    echo "기본: $(pactl info 2>/dev/null | awk -F': ' '/^Default Sink/ {print $2}')"
    exit 0 ;;
  --auto)
    kind=hdmi
    if [ -f "$CONF" ]; then kind="$(cat "$CONF")"; fi ;;
  hdmi|usb) kind="$mode" ;;
  *) echo "사용: saver-sound [hdmi|usb|list]"; exit 2 ;;
esac
sink="$(find_sink "$kind")"
if [ -z "$sink" ] && [ "$kind" = hdmi ] && hdmi_profile; then sink="$(find_sink hdmi)"; fi
if [ -z "$sink" ]; then
  echo "$kind 출력 장치가 없습니다. 모니터 전원과 HDMI 연결(USB 헤드셋이면 USB 연결)을 확인하세요. 목록: saver-sound list"
  exit 1
fi
pactl set-default-sink "$sink"
for i in $(pactl list short sink-inputs 2>/dev/null | awk '{print $1}'); do
  pactl move-sink-input "$i" "$sink" 2>/dev/null || true      # 이미 소리를 내는 Chromium 도 옮긴다
done
pactl set-sink-mute "$sink" 0 2>/dev/null || true
if [ "$mode" = --auto ]; then exit 0; fi
mkdir -p "$(dirname "$CONF")" && echo "$kind" > "$CONF"
pactl set-sink-volume "$sink" 100% 2>/dev/null || true
echo "출력: $sink (음량 100 %. 크기는 모니터나 헤드셋에서 조절)"
f="$(ls "$VOICE_DIR"/*.mp3 2>/dev/null | head -n 1)"
if [ -n "$f" ] && command -v mpg123 >/dev/null 2>&1; then
  echo "시험 재생: $(basename "$f")"
  mpg123 -q "$f"
else
  paplay /usr/share/sounds/alsa/Front_Center.wav 2>/dev/null || echo "시험 재생 파일이나 재생기(mpg123)가 없습니다"
fi
EOF
write_cmd "$BIN/saver-time" "" <<'EOF'
# 젯슨 시각 맞추기. 젯슨에는 RTC 전지가 없어 인터넷 없이 켜면 시각이 틀리고, 기록 파일의 시각도 틀린다.
#   saver-time                       인터넷(HTTP 응답의 Date)으로 맞춤. 휴대폰 핫스팟이면 충분
#   saver-time "2026-10-24 09:30"    직접 입력(한국 시각)
if [ $# -ge 1 ]; then
  sudo date -s "$1" >/dev/null || exit 1
else
  d=""
  for u in http://www.google.com http://www.naver.com; do
    d="$(curl -sSI --max-time 8 "$u" 2>/dev/null | tr -d '\r' | awk -F': ' 'tolower($1) == "date" {print $2; exit}')"
    if [ -n "$d" ]; then break; fi
  done
  if [ -z "$d" ]; then
    echo "인터넷으로 시각을 받지 못했습니다. 직접 입력: saver-time \"YYYY-MM-DD HH:MM\""
    exit 1
  fi
  sudo date -s "$d" >/dev/null || exit 1
fi
echo "현재 $(date '+%Y-%m-%d %H:%M:%S %Z') (UTC $(date -u '+%H:%M:%S'))"
EOF
write_user "$BIN/saver-logs" 0755 <<'EOF'
#!/usr/bin/env bash
# 브리지 서비스 실시간 기록 (Ctrl+C 로 끝)
exec journalctl -u saver-bridge -f -n 50
EOF
write_user "$BIN/saver-test" 0755 <<EOF
#!/usr/bin/env bash
# 자동 시험 (하드웨어 없이, 가짜 Teensy 로)
cd "$JETSON_DIR" && LANG=C.UTF-8 LC_ALL=C.UTF-8 PYTHONIOENCODING=utf-8 python3 tests/test_all.py
EOF
write_user "$BIN/saver-link" 0755 <<EOF
#!/usr/bin/env bash
# 최근 세션 기록의 링크 품질(브리지가 10초마다 누적 횟수를 남긴다).
# 10/11 모터 끔 30분과 10/12 모터 켬 30분을 같은 항목으로 비교한다(docs/noise_emi.md 6절).
f="\$(ls -t "$LOG_DIR"/saver_*.csv 2>/dev/null | head -n 1)"
[ -n "\$f" ] || { echo "기록 없음: $LOG_DIR"; exit 1; }
echo "기록 \$f"
echo "처음:"; grep 'LINK ' "\$f" | head -n 1 | cut -c1-400
echo "마지막:"; grep 'LINK ' "\$f" | tail -n 1 | cut -c1-400
EOF
write_user "$BIN/saver-update" 0755 <<EOF
#!/usr/bin/env bash
# 저장소를 받아 오고(fast-forward 만) 브리지를 다시 시작한다
set -e
git -C "$REPO_DIR" pull --ff-only
if systemctl is-active --quiet saver-bridge; then sudo -n $SYSTEMCTL restart saver-bridge || sudo $SYSTEMCTL restart saver-bridge; fi
echo "갱신 완료: \$(git -C "$REPO_DIR" log -1 --format='%h %s')"
EOF
DESK="$(xdg-user-dir DESKTOP 2>/dev/null || true)"
[ -n "$DESK" ] || DESK="$HOME/Desktop"
for name in start stop off; do
  case "$name" in
    start) label="SAVER 시작"; icon=media-playback-start ;;
    stop)  label="SAVER 정지"; icon=media-playback-stop ;;
    off)   label="SAVER 끄기 (젯슨 전원)"; icon=system-shutdown ;;
  esac
  write_user "$DESK/saver-$name.desktop" 0755 <<EOF
[Desktop Entry]
Type=Application
Name=$label
Exec=$BIN/saver-$name
Icon=$icon
Terminal=false
EOF
  if [ "$DRY_RUN" -eq 0 ]; then gio set "$DESK/saver-$name.desktop" metadata::trusted true 2>/dev/null || true; fi
done
ok "saver-start, saver-stop, saver-off, saver-status, saver-logs, saver-link, saver-sound, saver-time, saver-test, saver-update, 바탕화면 아이콘 3개"
# 화면 세션에서 실행 중이면 소리 기본 출력을 바로 HDMI 로(SSH 로 실행했거나 모니터가 꺼져 있으면 재부팅 뒤 saver-sound)
if [ "$DRY_RUN" -eq 1 ]; then
  info "(dry-run) saver-sound --auto"
elif "$BIN/saver-sound" --auto >/dev/null 2>&1; then
  ok "소리 기본 출력: $(pactl info 2>/dev/null | awk -F': ' '/^Default Sink/ {print $2}')"
else
  info "소리 출력은 재부팅 뒤 헤드셋을 꽂고 saver-sound 로 맞춘다"
fi
# ~/.local/bin(saver-* 명령, Claude Code)을 새 터미널의 PATH 에 넣는다. Claude 설치기는 PATH 를 바꾸지 않는다
if ! grep -q 'SAVER: ~/.local/bin' "$HOME/.bashrc" 2>/dev/null; then
  if [ "$DRY_RUN" -eq 1 ]; then
    info "(dry-run) ~/.bashrc 에 PATH 줄 추가"
  else
    # shellcheck disable=SC2016  # .bashrc 에는 $HOME, $PATH 를 글자 그대로 넣는다
    printf '\n# SAVER: ~/.local/bin (saver-* 명령, Claude Code)\nexport PATH="$HOME/.local/bin:$PATH"\n' >> "$HOME/.bashrc"
    ok "$HOME/.bashrc 에 $HOME/.local/bin PATH 추가 (새 터미널부터)"
  fi
fi

# ---------------------------------------------------------------- 자동 시작(선택)
if [ "$AUTOSTART" -eq 1 ]; then
  step "부팅 자동 시작 (--autostart)"
  write_user "$HOME/.config/autostart/saver-kiosk.desktop" 0644 <<EOF
[Desktop Entry]
Type=Application
Name=SAVER kiosk
Exec=$BIN/saver-kiosk
X-GNOME-Autostart-enabled=true
EOF
  # 키오스크 화면 위로 업데이트 알림이 뜨지 않게
  if [ -f /etc/xdg/autostart/update-notifier.desktop ]; then
    write_user "$HOME/.config/autostart/update-notifier.desktop" 0644 <<'EOF'
[Desktop Entry]
Type=Application
Name=update-notifier
Hidden=true
EOF
  fi
  run sudo systemctl disable --now apt-daily.timer apt-daily-upgrade.timer || true
  info "자동 업데이트 타이머 끔 (시연 중 apt 잠금, 알림 방지. 되돌리기: sudo systemctl enable --now apt-daily.timer apt-daily-upgrade.timer)"
  if [ -f /etc/gdm3/custom.conf ]; then
    [ "$DRY_RUN" -eq 1 ] || sudo cp -n /etc/gdm3/custom.conf /etc/gdm3/custom.conf.saver.bak
    run sudo sed -i -E \
      -e '/^\s*#?\s*AutomaticLoginEnable\s*=.*/d' -e '/^\s*#?\s*AutomaticLogin\s*=.*/d' \
      -e "s/^\[daemon\]\s*$/[daemon]\nAutomaticLoginEnable=true\nAutomaticLogin=$USER_NAME/" /etc/gdm3/custom.conf
    if [ "$DRY_RUN" -eq 1 ] || grep -q '^AutomaticLoginEnable=true' /etc/gdm3/custom.conf; then
      ok "gdm3 자동 로그인 ($USER_NAME, 원본 custom.conf.saver.bak)"
    else
      warn "gdm3 custom.conf 에 [daemon] 절이 없어 자동 로그인을 넣지 못함. 설정 > 사용자 > 자동 로그인으로 켜기"
    fi
  elif [ -d /etc/lightdm ]; then
    write_root /etc/lightdm/lightdm.conf.d/50-saver-autologin.conf 0644 <<EOF
[Seat:*]
autologin-user=$USER_NAME
autologin-user-timeout=0
EOF
    ok "lightdm 자동 로그인 ($USER_NAME)"
  else
    warn "로그인 관리자(gdm3, lightdm)를 찾지 못해 자동 로그인은 직접 설정"
  fi
else
  if [ -f "$HOME/.config/autostart/saver-kiosk.desktop" ]; then
    run rm -f "$HOME/.config/autostart/saver-kiosk.desktop"
    info "이전 키오스크 자동 시작 항목 제거"
  fi
fi

# ---------------------------------------------------------------- 글꼴, 시간대, 전원 모드
step "한글 글꼴, 시간대, 전원 모드"
if fc-list : family 2>/dev/null | grep -i 'pretendard' >/dev/null; then
  ok "Pretendard 이미 설치"
elif [ "$DRY_RUN" -eq 1 ]; then
  info "(dry-run) Pretendard 내려받아 /usr/share/fonts/opentype/pretendard 에 설치"
else
  FT="$(mktemp -d)"
  if curl -fsSL --max-time 120 --retry 2 -o "$FT/p.zip" "$PRETENDARD_URL" && unzip -q "$FT/p.zip" -d "$FT/x"; then
    mapfile -t OTFS < <(find "$FT/x" -path '*static*' -name 'Pretendard-*.otf' 2>/dev/null)
    if [ "${#OTFS[@]}" -eq 0 ]; then mapfile -t OTFS < <(find "$FT/x" -name 'Pretendard-*.otf'); fi
    if [ "${#OTFS[@]}" -gt 0 ]; then
      sudo install -d /usr/share/fonts/opentype/pretendard
      sudo install -m 0644 "${OTFS[@]}" /usr/share/fonts/opentype/pretendard/
      sudo fc-cache -f >/dev/null
      ok "Pretendard ${#OTFS[@]}개 (UI 기본 글꼴, 인터넷 없이도 같은 화면)"
    else
      warn "Pretendard 압축 안에서 OTF 를 찾지 못함. Noto Sans CJK 로 표시"
    fi
  else
    warn "Pretendard 내려받기 실패. Noto Sans CJK 로 표시(UI 는 정상)"
  fi
  rm -rf "$FT"
fi
run sudo timedatectl set-timezone Asia/Seoul || true
info "시간대 Asia/Seoul (기록 파일의 시각은 코드가 UTC 로 쓴다). 젯슨에는 RTC 전지가 없어 인터넷 없이 켜면 시각이 틀릴 수 있다"
if [ "$MAXN" -eq 1 ]; then
  run sudo nvpmodel -m 0
  ok "전원 모드 MAXN (5 V 4 A 배럴잭 전원 전제)"
else
  info "전원 모드는 그대로 ($(sudo -n nvpmodel -q 2>/dev/null | head -n 1 || echo '확인 안 됨'))"
fi

# ---------------------------------------------------------------- 9. Claude Code
step "Claude Code"
# 공식 설치기(code.claude.com/docs/en/setup): ~/.local/bin/claude 에 설치하고 PATH 는 바꾸지 않는다.
# 공식 지원은 Ubuntu 20.04 이상, ARM64, 메모리 4 GB 이상이다. 젯팩 4.6(Ubuntu 18.04, glibc 2.27)은
# 지원 목록에 없어 실행이 안 될 수 있으므로, 실패해도 나머지 설정은 그대로 두고 경고만 남긴다.
CLAUDE_BIN="$HOME/.local/bin/claude"
CLAUDE_STATUS="건너뜀(--no-claude)"
install_claude() {
  local mem_mb glibc tmp
  mem_mb="$(awk '/MemTotal/ {print int($2 / 1024)}' /proc/meminfo)"
  glibc="$(ldd --version 2>&1 | head -n 1 | awk '{print $NF}')"
  info "메모리 ${mem_mb} MB, glibc ${glibc}"
  if [ "$UBU" = "18.04" ]; then
    warn "Claude Code 공식 지원은 Ubuntu 20.04 이상. 18.04(glibc 2.27)는 설치는 되어도 실행이 안 될 수 있음"
  fi
  if [ "${mem_mb:-0}" -lt 3500 ]; then
    warn "메모리 ${mem_mb} MB: Claude Code 권장 4 GB 미만"
  fi
  if [ -x "$CLAUDE_BIN" ] && "$CLAUDE_BIN" --version >/dev/null 2>&1; then
    CLAUDE_STATUS="$("$CLAUDE_BIN" --version 2>/dev/null | head -n 1) (이미 설치)"
    ok "$CLAUDE_STATUS"
  elif [ "$DRY_RUN" -eq 1 ]; then
    info "(dry-run) curl -fsSL https://claude.ai/install.sh | bash"
    CLAUDE_STATUS="(dry-run)"
  else
    tmp="$(mktemp)"
    if curl -fsSL --max-time 120 --retry 2 -o "$tmp" https://claude.ai/install.sh && bash "$tmp"; then
      info "설치기 실행 끝"
    else
      warn "Claude Code 설치기 실패(네트워크 또는 지원 범위)"
    fi
    rm -f "$tmp"
    if [ -x "$CLAUDE_BIN" ] && "$CLAUDE_BIN" --version >/dev/null 2>&1; then
      CLAUDE_STATUS="$("$CLAUDE_BIN" --version 2>/dev/null | head -n 1)"
      ok "Claude Code $CLAUDE_STATUS. 재부팅 뒤 터미널에서 claude 를 실행해 로그인(점검: claude doctor)"
    else
      CLAUDE_STATUS="실행 안 됨 (Ubuntu 18.04 비지원 가능성. 노트북에서 SSH 로 젯슨 작업 권장)"
      warn "Claude Code 실행 확인 실패: $CLAUDE_BIN --version"
    fi
  fi
}
if [ "$INSTALL_CLAUDE" -eq 1 ]; then
  install_claude
fi

# ---------------------------------------------------------------- 10. 자동 시험
step "자동 시험 (하드웨어 없이)"
TEST_STATUS="건너뜀(--no-test)"
if [ "$RUN_TEST" -eq 1 ] && [ "$DRY_RUN" -eq 0 ]; then
  if (cd "$JETSON_DIR" && LANG=C.UTF-8 LC_ALL=C.UTF-8 PYTHONIOENCODING=utf-8 python3 tests/test_all.py > "$HOME/saver_setup_test.log" 2>&1); then
    TEST_STATUS="$(tail -n 2 "$HOME/saver_setup_test.log" | tr -d '\n')"
    ok "$TEST_STATUS"
  else
    TEST_STATUS="실패 ($HOME/saver_setup_test.log)"
    warn "자동 시험 실패: $HOME/saver_setup_test.log"
    grep '  FAIL' "$HOME/saver_setup_test.log" | head -n 10 || true
  fi
elif [ "$DRY_RUN" -eq 1 ]; then
  info "(dry-run) cd $JETSON_DIR && python3 tests/test_all.py"
fi

# ---------------------------------------------------------------- 요약
step "요약"
cat <<EOF
    저장소        $REPO_DIR
    UI            $UI_DIST → http://127.0.0.1:$HTTP_PORT (WebSocket $WS_PORT)
    브리지        saver-bridge.service, Teensy /dev/saver-teensy, 기록 $LOG_DIR
    자동 시작     $([ "$AUTOSTART" -eq 1 ] && echo "켬 (자동 로그인, 브리지, 키오스크)" || echo "끔 (saver-start 또는 바탕화면 'SAVER 시작')")
    주소 (SSH)    $(hostname -I 2>/dev/null | awk '{print $1}')  (노트북에서 ssh $USER_NAME@주소)
    시각          $(date '+%Y-%m-%d %H:%M %Z')
    Claude Code   $CLAUDE_STATUS
    자동 시험     $TEST_STATUS
EOF
if [ "${#WARNINGS[@]}" -gt 0 ]; then
  printf '\n    주의 %d건\n' "${#WARNINGS[@]}"
  for w in "${WARNINGS[@]}"; do printf '      - %s\n' "$w"; done
fi
cat <<'EOF'

    다음 순서
      1. 재부팅: sudo reboot   (그룹 권한, 자동 로그인, PATH 적용)
      2. 헤드셋을 모니터 3.5 mm 단자에 꽂고 saver-sound (HDMI 로 맞추고 음성 하나 재생. USB 헤드셋이면 saver-sound usb)
      3. 모터와 블로워 전원을 끈 채 Teensy 를 USB 로 연결하고 saver-start
      4. saver-status 로 /dev/saver-teensy, UI 응답, 마지막 단계(S1 점검 통과면 INIT_DONE:S1->X_MOVE) 확인. 기록은 saver-logs
      5. 시각이 틀리면 saver-time (인터넷) 또는 saver-time "YYYY-MM-DD HH:MM"
      6. 끌 때는 saver-off (바탕화면 'SAVER 끄기'). 모니터에 신호 없음이 뜨고 20초 뒤 배터리(XT90) 분리
      7. Claude Code: 새 터미널에서 claude 를 실행해 로그인 (실행이 안 되면 노트북에서 SSH 로 작업)
EOF
