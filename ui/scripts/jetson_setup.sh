#!/usr/bin/env bash
# ============================================================
# 젯슨 나노(Ubuntu 18.04) 키오스크 설치 스크립트
#   PC에서 npm run build → dist/ 를 젯슨으로 복사한 뒤 젯슨에서 실행합니다.
#
#   scp -r dist backend scripts <user>@<젯슨IP>:~/saver-ui/
#   ssh <user>@<젯슨IP>
#   cd ~/saver-ui && bash scripts/jetson_setup.sh
# ============================================================
set -e

APP_DIR="$(cd "$(dirname "$0")/.." && pwd)"
USER_NAME="$(whoami)"

echo "▶ 설치 경로: $APP_DIR"

# 1) 필수 패키지 -------------------------------------------------
echo "▶ 패키지 설치"
sudo apt-get update
sudo apt-get install -y chromium-browser python3-pip unclutter xdotool
# 젯슨 나노 기본 Python은 3.6 → websockets 9.1 고정
python3 -c "import sys; sys.exit(0 if sys.version_info>=(3,7) else 1)" \
  && pip3 install --user websockets pyserial \
  || pip3 install --user 'websockets==9.1' pyserial

# 2) 시리얼 권한 -------------------------------------------------
sudo usermod -aG dialout "$USER_NAME" || true
echo "  (시리얼 권한은 재로그인 후 적용됩니다)"

# 3) 브리지 서비스 ------------------------------------------------
echo "▶ saver-bridge.service 등록"
sudo tee /etc/systemd/system/saver-bridge.service >/dev/null <<EOF
[Unit]
Description=SAVER Jetson Bridge (WebSocket + Serial + static)
After=network.target

[Service]
Type=simple
User=$USER_NAME
WorkingDirectory=$APP_DIR
ExecStart=/usr/bin/python3 $APP_DIR/backend/saver_bridge.py --serve $APP_DIR/dist --http-port 8080 --serial /dev/ttyACM0
Restart=always
RestartSec=3

[Install]
WantedBy=multi-user.target
EOF

# 4) 키오스크 서비스 ----------------------------------------------
echo "▶ saver-kiosk.service 등록"
sudo tee /etc/systemd/system/saver-kiosk.service >/dev/null <<EOF
[Unit]
Description=SAVER Touch UI (Chromium kiosk)
After=graphical.target saver-bridge.service
Wants=saver-bridge.service

[Service]
Type=simple
User=$USER_NAME
Environment=DISPLAY=:0
Environment=XAUTHORITY=/home/$USER_NAME/.Xauthority
ExecStartPre=/bin/sh -c 'xset s off; xset -dpms; xset s noblank; unclutter -idle 0 &'
ExecStart=/usr/bin/chromium-browser \\
  --kiosk http://localhost:8080/ \\
  --window-size=1024,600 --window-position=0,0 \\
  --touch-events=enabled --enable-features=OverlayScrollbar \\
  --noerrdialogs --disable-infobars --disable-session-crashed-bubble \\
  --disable-pinch --overscroll-history-navigation=0 \\
  --check-for-update-interval=31536000 --disable-translate \\
  --autoplay-policy=no-user-gesture-required
Restart=always
RestartSec=5

[Install]
WantedBy=graphical.target
EOF

sudo systemctl daemon-reload
sudo systemctl enable saver-bridge.service saver-kiosk.service
sudo systemctl restart saver-bridge.service

cat <<'MSG'

✅ 설치 완료

  실행      sudo systemctl start saver-kiosk
  중지      sudo systemctl stop saver-kiosk
  로그      journalctl -u saver-bridge -f
  화면갱신  dist/ 를 새로 복사한 뒤  sudo systemctl restart saver-kiosk

  터치 좌표가 어긋나면:
    xinput list                       # 터치 장치 ID 확인
    xinput map-to-output <ID> HDMI-0  # 해당 디스플레이에 매핑
  화면 회전이 필요하면 /boot/extlinux/extlinux.conf 또는 xrandr 사용.

MSG
