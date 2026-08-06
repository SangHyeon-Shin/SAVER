# MR60BHA2 실시간 모니터

XIAO가 USB 시리얼(115200 baud)로 보내는 다음 형식을 실시간 표시합니다.

```text
heart_rate_bpm, breath_rate_brpm, breath_period_sec, heart_status, breath_status, distance_cm
88.00, 16.00, 3.75, NORMAL, NORMAL, 34.44
```

## 실행

1. `start-monitor.cmd`를 더블클릭합니다.
2. 열린 Edge/Chrome에서 **센서 연결**을 누릅니다.
3. 목록에서 XIAO가 연결된 `COM4` 장치를 선택하고 **연결**을 누릅니다.

실시간 심박수·호흡수 그래프, 현재 호흡 주기와 감지 거리, 선택 구간의 최소·평균·최대값을 표시합니다. **CSV 저장** 버튼으로 지금까지 받은 샘플을 내려받을 수 있습니다.

서버를 종료하려면 함께 열린 PowerShell 창에서 `Ctrl+C`를 누르세요.

> 개발·관찰 전용입니다. 의료 진단이나 치료 판단에 사용하지 마세요.
