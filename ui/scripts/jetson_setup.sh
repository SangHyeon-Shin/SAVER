#!/usr/bin/env bash
# 옛 젯슨 설치 스크립트 자리(backend/saver_bridge.py, dist/ 를 쓰던 판).
# 2026-10-10 부터 jetson/setup/setup_jetson.sh 가 설치를 맡는다(브리지 모듈, UI 묶음 jetson/ui_dist,
# Teensy udev 규칙, 키오스크, Claude Code, 자동 시험). 이 파일은 옛 안내를 따라 실행해도 새 스크립트로 넘긴다.
exec bash "$(cd "$(dirname "$0")/../.." && pwd)/jetson/setup/setup_jetson.sh" "$@"
