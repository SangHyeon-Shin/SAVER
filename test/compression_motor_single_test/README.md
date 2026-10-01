# 압박 모터 한 개 단독 시험: Teensy 4.1 + MKS VESC 75100 V2

Arduino IDE에서 `compression_motor_single_test.ino`를 엽니다. 같은 폴더의
`vesc_bench.h`도 필요합니다. 보드는 **Teensy 4.1**, USB Type은 **Serial**입니다.
추가 VESC/JSON 라이브러리는 필요하지 않습니다. 별도의 `.c` 프로그램이 아니라
`.ino` 스케치이며, `.h`는 통신·상태 로직을 나누어 시험하기 위한 C++ 보조 파일입니다.

**연구용 고정 시험대의 모터 단독 회전 시험입니다. 사람·마네킹 압박에 사용하지 않습니다.**
벨트·볼스크류·압박 기구에서 모터를 분리하고 본체를 고정합니다. 축과 회전체에 손을
대지 않습니다. 기구가 붙어 있거나 하중을 지탱하는 상태에서는 사용하지 마세요.

## 현재 기본값과 사용자 확인 상태

- 사용자 확인: 모터 BL60120-2430, 드라이버 Makerbase VESC 75100 V2.
- 사용자 확인: 모터를 기구에서 분리해서 시험. VESC 설정을 완료했다고 보고함.
- 2026-09-16 사용자 보고: 기존 단독 구동 성공. **이번 역방향 코드는 아직 실물 미검증**입니다.
  이후 연결할 계획인 볼스크류·벨트는 이 단독 시험 전에 다시 분리해야 합니다.
  현재 비상정지 수단은 파워서플라이 스위치라고 정정됐으며, 이를 전용 E-stop 검증으로 보지 않습니다.
- 미확인: 실제 펌웨어 버전, UART baud, 전류·회생·속도 한계, 통신 timeout 설정값/실측.
- 현재 파일의 사용자 수정값을 보존: 기본 **+10%**, 듀티 크기 상한 **50%**, 기본/최대 시간
  **5000 ms = 5초**. 과거 5%라고 남아 있던 주석/설명만 실제 값에 맞췄습니다.
  음수 듀티를 지정하면 기존 양수 지령의 반대 방향을 요청합니다. 상한 50%는 안전 권장값이 아닙니다.
- 사용자의 명시적 잠금 해제 요청에 따라 **`BENCH_SETUP_CONFIRMED=true`**로 변경했습니다.
  사용자는 VESC 설정 완료를 보고했지만, 실제 설정값/전기적 조건을 Codex가 검증한 것은 아닙니다.
  부팅 자동 실행은 없고 정상 통신·오류 해제·수동 `ARM` 뒤에만 `RUN`을 수락합니다.
- 부팅 후 조회와 **0 A release 지령**은 송신합니다. 완전한 UART 무송신 모드는 아닙니다.
  양/음의 구동 듀티, 펌웨어 변경, 모터 검출, 설정 저장은 자동으로 하지 않습니다.

## 배선

배선·업로드 전 VESC 주전원과 USB를 모두 끕니다. 사용자 제공 사진의 **5번 COMM**
커넥터를 사용합니다. 사진의 6번 USART2나 USB·PPM·CAN 포트와 혼동하지 마세요.
커넥터를 뒤집으면 물리 순서가 달라지므로 핀 번호/선 색을 추측하지 말고 실물 라벨을 대조합니다.

| VESC 신호 | Teensy 4.1 | 이유 |
|---|---|---|
| COMM `TX/SCL` | `28 / RX7` | 기존 `teensy/pinmap.h`의 압박 UART 수신 핀 |
| COMM `RX/SDA` | `29 / TX7` | 기존 압박 UART 송신 핀; TX/RX 교차 |
| COMM `GND` | `GND` | 비절연 UART의 공통 신호 기준 |
| COMM `5V`, `3.3V` | **미연결·절연** | Teensy VIN/3.3V에 전원을 묶어 역급전하지 않음 |
| COMM의 나머지 신호 | 미연결 | 이 스케치는 UART만 사용 |

- Teensy는 시험 중 PC USB로 전원을 공급합니다. VESC는 별도의 검증된 24 V 주전원을 사용합니다.
- UART HIGH가 **3.3 V 이하인지 실측**한 뒤 Teensy RX에 연결합니다. Teensy는 5 V tolerant가 아닙니다.
- 모터의 3상 선은 VESC의 모터 출력으로, Hall 하네스는 VESC의 HALL 커넥터로 연결합니다.
  Hall 공급전압/선 순서는 모터 설명과 VESC 선택 스위치를 확인해야 합니다. Teensy 30/31/39로
  Hall 배선을 옮기지 않습니다. Hall 센서나 권선에 전압을 임의로 직접 인가하지 않습니다.
- 모터 주전류는 브레드보드·Teensy GND 배선을 통과시키지 않습니다. 정격 배선·단자·퓨즈와
  USB/소프트웨어와 무관한 물리 비상정지 수단이 필요합니다.
- 다른 모터의 전원/제어 연결은 분리합니다. 이 스케치는 음압·BTS7960·팬의 EN/PWM을
  초기화하지 않습니다. 다른 드라이버 입력이 떠도 멈춰 있을 것이라고 가정하면 안 됩니다.

제조사 PDF는 배포 위치까지 확인했지만 이 환경에서 내용 추출/렌더링에 실패했습니다.
위 COMM 라벨은 **사용자 제공 커넥터 사진**을 근거로 합니다. 제조사 도면 및 실물 전압은
추가 확인 대상입니다. 기존 `teensy/pinmap.h`와 `docs/`는 수정하지 않았습니다.

## VESC Tool에서 먼저 확인할 것

1. 장착된 보드/펌웨어가 75100 V2에 맞는지 확인합니다. 이 스케치는 펌웨어를 업데이트하지 않습니다.
2. 이 모터의 24 V 사양에 맞는 motor 설정, Hall 연결·검출 결과를 확인합니다.
   모터 검출 과정은 회전/통전할 수 있으므로 기구 분리·고정 상태에서 제조사 절차를 따릅니다.
3. `Motor Current Max`, 제동/회생 전류, `Battery Current Max`, 입력 전압 및 속도/듀티 한계를
   모터·전원·배선 조건에 맞춰 별도로 확인합니다. **컨트롤러의 100 A 표기는 이 모터의
   허용 설정값이 아닙니다.** 전원 전류와 VESC 모터 전류도 같은 값이 아닙니다.
4. 이 시험에는 UART 전용 제어를 사용합니다. PPM/ADC/리모컨/CAN/Lisp/다른 VESC Tool 구동
   명령 등 경쟁하는 입력을 제거합니다. 설정을 저장한 뒤 VESC Tool의 구동 제어는 종료합니다.
5. UART 속도를 확인해 `.ino`의 `VESC_UART_BAUD`와 일치시킵니다. 스케치는 115200, 8N1로
   작성했지만 **실제 VESC 설정값을 읽어 확인한 것은 아닙니다.** USB Serial의 115200과 별개입니다.
6. VESC의 자체 **Timeout을 유한한 양수로 설정**하고, 이 무부하 coast 시험에 맞게 Timeout
   Brake Current를 확인합니다. 0 A release를 계획한다면 timeout 쪽도 0 A 조건을 확인합니다.
   UART 단선/Teensy 정지 때의 실제 동작을 검증해야 합니다. Timeout=0 비활성화는 사용하지 않습니다.
7. 파워서플라이의 전류 제한만으로 모든 고장을 막을 수 없습니다. 회생 에너지가 입력으로
   돌아올 수 있으므로 공급원의 회생 흡수 가능 여부와 과전압 보호를 확인합니다.

이 코드의 통신 시간(50/100/300 ms)은 단독 벤치 통신 정책이며 의료 `T_comm`이 아닙니다.
의학적 압박 깊이·속도·힘 기준은 사용하거나 변경하지 않습니다. 기존의 6 A/12 A 기준도
VESC의 모터/배터리 전류 제한에 자동 복사하지 않습니다. 측정 위치와 의미 확인이 먼저입니다.

## 사용자가 수정할 곳

### 나중에 보내줄 VESC Tool 화면

모터를 움직이지 않는 설정/조회 화면만 보내주세요. 화면 확보를 위해 검출·구동을 다시 할 필요는 없습니다.

1. 연결된 **펌웨어 버전과 하드웨어 이름**이 보이는 화면.
2. **App Settings**: App to use, UART baud rate, Timeout, Timeout Brake Current.
3. **Motor Settings**: Motor Current Max/Min, Absolute Maximum Current, Battery Current Max/Min.
4. **입력 전압·ERPM·듀티 제한**, motor type/센서 모드와 Hall 검출 결과가 보이는 화면.

화면 이름/위치는 VESC Tool 버전에 따라 다를 수 있습니다. 필요하면 여러 장으로 보내주세요.
사용자 요청으로 소프트웨어 구동 잠금은 해제했지만 사진의 실제 값과 UART 설정·전제조건을
대조하는 작업은 아직 남아 있습니다. **사진만으로 실제 회전/정지·전압 안전성이 검증되는 것은
아닙니다.** 설정/전기적 조건을 모르는 상태에서 실물을 구동하는 것을 권장하지 않습니다.

`.ino`의 **사용자가 수정하는 부분**에서 다음을 설정합니다.

| 설정 | 현재 값 | 의미 |
|---|---:|---|
| `BENCH_SETUP_CONFIRMED` | `true` | 사용자 요청으로 소프트웨어 잠금 해제. 실제 검증 완료 표시는 아님 |
| `MAX_DUTY_PERCENT` | `50` | 기존 파일의 듀티 크기 상한: -50~+50% (RUN 0은 거부) |
| `MAX_RUN_MS` | `5000` | 사용자 지정 최대 지령시간 5초 |
| `DEFAULT_DUTY_PERCENT` | `10` | `RUN`에 듀티 생략 시 +10%; signed 상수이므로 음수 지정 가능 |
| `DEFAULT_RUN_MS` | `5000` | `RUN`에 시간 생략 시 5초 |
| `VESC_UART_BAUD` | `115200` | 실제 VESC UART baud와 일치 필요 |

허용 문법상 듀티는 부호 있는 정수 -100..100%, 시간은 100..30000 ms이지만, **이 전체 범위가 이 모터에
안전하다는 뜻이 아닙니다.** 실제 상한은 위 두 MAX 값으로 더 제한합니다.
RUN은 0%를 거부하며 SET 0은 정지입니다. 숫자 앞 + 또는 - 하나만 허용하고 소수·지수·NaN·범위 초과는
거부합니다. 단위시험 숫자는 소프트웨어 검사용이며 기존 사용자 설정 역시 하드웨어 안전성 검증값은 아닙니다.
설정을 바꾸면 다시 컴파일/업로드해야 하며, 값 변경만으로 부팅 자동 구동되지는 않습니다.

## 시리얼 모니터에서 사용

115200, 줄바꿈 `New Line` 또는 `Both NL & CR`. 다른 시리얼 모니터/Jetson 브리지는 닫습니다.
기동 후 `HELP`, `STATUS`로 **설정 잠금과 통신부터 확인**합니다.

- `VESC_FW,major=...,minor=...`: 버전 응답 수신. 일반 VESC 5/6 값 구조를 처리하며 다른 버전은 거부.
- `VESC,values_ok=1,...`: CRC/프레임/값 구조를 통과한 최근 응답. 센서 정확도나 안전 보증은 아님.
- `values_ok=0`: 미수신·오래됨·해석 불가. 측정 숫자는 생략하며 0/마지막 정상값으로 위장하지 않음.
- `input_V`, `motor_current_A`, `input_current_A`, `duty_pct`, `erpm`, `fet_temp_C`, `fault_code`는
  **VESC가 보고한 값**입니다. Teensy가 외부 전류/온도/회전 센서를 직접 측정한 값이 아닙니다.
- `erpm`은 전기적 RPM입니다. 극쌍수를 확인하지 않았으므로 축 RPM이나 압박 속도로 환산하지 않습니다.
- 시작 문구: `SAVER_COMPRESSION_MOTOR_SINGLE_TEST_READY,build=vesc_uart_bidirectional_v2`.
- `IDLE_CHECK,reported_zero_stable=1`은 fresh VESC 응답의 ERPM=0·듀티=0이 정지 지령 상태에서
  250ms 이상의 관측 구간에 걸쳐 보고됐다는 뜻입니다. 한 표본을 받아 250ms 기다리기만 해서는
  통과하지 않습니다. **독립적인 실제 축 정지 확인이 아니며** `standstill_verified=0`입니다.

상한과 VESC 설정 검토 후에만 다음 순서로 사용합니다. 아래 `<...>`에는 정한 숫자를 넣습니다.

```text
STATUS
ARM
RUN <듀티 정수 %> <작동시간 ms>
STOP
```

현재 `RUN`과 `RUN 10 5000`이 같은 지령입니다. 구동 잠금은 해제했지만
정상 통신·오류 해제·VESC 0 보고 안정·수동 `ARM`을 통과해야 합니다.
`ARM` 수락 뒤 10초 안에 `RUN`을 보냅니다. RUN 시에도 0 보고 조건을 다시 검사합니다.
5초가 지나면 0 A 지령을 요청하는 것으로, 축이 정확히 5초에 멎는다는 의미는 아닙니다.

### 반대 방향 요청

**기구에서 분리·고정한 무부하 모터이며, VESC/전원/보호 조건을 확인한 뒤에만 사용합니다.**
기존 파일 기본값과 같은 크기/시간으로 반대 방향을 요청하는 문법 예시는 다음과 같습니다.
실기에서 10%·5초가 적절한지는 별도 확인 사항입니다.

1. `STOP`을 보내고 축이 완전히 멈췄는지 직접 확인합니다. 손으로 축을 잡아 멈추지 않습니다.
2. `STATUS`에서 최근 값, `fault_code=0`, `reported_zero_stable=1`을 확인합니다.
   오류가 잠겼다면 원인 해소 후 `CLEAR_ERR`가 필요합니다.
3. `ARM` 수락 후 아래 **한 줄**을 보냅니다.

```text
RUN -10 5000
```

`duty_pct=-10,direction=REV`로 요청이 표시됩니다. 이는 실제 역회전 성공 로그가 아닙니다.
양수 `RUN 10 5000`이 기존 방향, 음수가 그 반대이며 VESC의 방향 반전 설정에 영향을 받으므로
시계/반시계 또는 압박/복귀 방향을 뜻한다고 단정하지 않습니다. 상선/Hall 배선을 바꿀 필요 없이
signed UART 듀티 지령을 사용합니다. VESC의 음의 ERPM 한계 등 설정이 역회전을 막을 수 있으나
이 코드는 그 설정을 자동 변경하지 않습니다.

회전 중 `SET`은 **같은 부호의 크기 변경만** 허용합니다. 양수 RUN 중 `SET -10` 또는 그 반대는
정지 요청·오류 잠금 처리하며 반대 듀티를 전송하지 않습니다. `SET 0`은 계속 정지 명령입니다.
STOP/시간 종료 이후에는 이전 ARM이 사라지고, 새 0 보고 관측 → 새 ARM → 새 RUN이 필요합니다.
즉시 역회전이나 자동 왕복 모드는 없습니다. 음수 기본값으로 매번 시험하려면
`DEFAULT_DUTY_PERCENT`를 검토한 음수로 바꾼 후 다시 컴파일/업로드할 수 있습니다.

| 명령 | 동작 |
|---|---|
| `ARM` | 설정·최근 통신·오류 해제·VESC ERPM/듀티 0 보고 250ms 확인 후 허가. 10초 이내 RUN 필요 |
| `RUN [signed_pct] [ms]` | 양수=기존 방향, 음수=반대 지령. 매번 새 ARM 및 0 보고 조건 필요 |
| `SET signed_pct` | 실행 중 같은 방향의 듀티 크기만 변경. 최초 종료시각은 늘리지 않음 |
| `SET 0`, `STOP`, `DISARM` | 전류 0 지령 요청 + ARM 해제 |
| `CLEAR_ERR` | VESC fault=0 응답이 250 ms 이상 유지되면 **Teensy 로컬 잠금만** 수동 해제 |
| `STATUS`, `HELP` | 진단/도움말. 구동을 시작하지 않음 |

RUN 중 RUN/ARM/CLEAR_ERR를 다시 보내거나, 잘못된 명령을 보내면 정지 지령·잠금으로 처리합니다.
RUN 시간이 끝나거나 USB/DTR가 끊겨도 ARM이 해제됩니다. 새 시험은 새 ARM/RUN이 필요합니다.
오류가 잠겼다면 원인을 제거하고 `CLEAR_ERR`부터 합니다. CLEAR_ERR는 VESC fault를 강제로
무시하거나 재부팅시키지 않습니다. 응답이 정상으로 돌아와도 자동 재시작하지 않습니다.

`VESC_NOT_REPORTING_ZERO_FOR_250MS`는 아직 정지 구간의 ERPM=0·듀티=0 응답이 충분하지 않다는
거부입니다. 멈춘 상태인데도 지속되면 STATUS 원문을 확인하며, 임의 RPM 허용 오차를 넣어 우회하지
않습니다. `MOTION_REPORTED_WHILE_ARMED`는 ARM 이후 움직임/듀티가 보고돼 허가를 취소한 경우입니다.
VESC 센서 추정 오차나 통신 갱신 간격 때문에 물리 정지와 보고값은 다를 수 있습니다.

## 정지와 보호의 한계

- UART `SET_DUTY`가 속도/전압 비율 지령이고 STOP은 `SET_CURRENT(0)`입니다. VESC EN 핀을
  LOW로 만드는 방식이 아닙니다. **브레이크·축 고정·실제 회전 정지를 보장하지 않습니다.**
- `RUN_REQUESTED`, `commanded_running`, `zero_current_queued`는 요청/Teensy 송신 상태입니다.
  VESC의 실행 ACK나 실제 회전/정지 확인값으로 읽으면 안 됩니다.
- VESC 값이 300 ms 이상 끊기거나, 구동 중 CRC/프레임 오류 또는 VESC fault가 생기면
  양/음의 구동 듀티 송신을 중단하고 0 A 지령을 먼저 시도한 뒤 보고합니다.
- 송신 공간이 없으면 기다리지 않고 정지 지령을 우선 대기시킵니다. `stop_pending=1`이면
  정지 요청조차 아직 UART에 넣지 못한 상태입니다. 즉시 물리 정지 수단을 사용해야 합니다.
- UART 단선·보드 리셋·프로그램 정지에서는 소프트웨어 STOP 전달을 보장할 수 없습니다.
  VESC 자체 timeout과 외부 비상정지는 별도로 필요합니다.
- 압박 리미트·E-stop 보조접점·전류/위치 센서·부하 피드백·Teensy 하드웨어 watchdog은
  이 단독 시험 범위에 없습니다. 연결된 압박 기구 제어/환자 사용으로 확장하면 안 됩니다.
- USB 로그가 막히면 버리고 `dropped_logs`로 셉니다. 로그가 안 보인다고 정지했다고 판단하지 마세요.

## 소프트웨어 검사와 출처

`tests/test_vesc_bench.cpp`의 constexpr 시험은 실제 포함 헤더를 평가합니다. CRC 독립 벡터,
패킷 길이·손상·timeout, 값 해석, 숫자 overflow, 설정 잠금 동작, ARM/RUN/SET/STOP, 중복 실행,
통신 단절, 오류 해제, millis rollover를 다룹니다.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File test/compression_motor_single_test/tests/check_vesc_bench.ps1
```

이는 실행 보드 없는 **컴파일 타임 시험**이며 실기 시험이 아닙니다. PC C++ 컴파일러가 있으면
같은 소스를 `-std=c++14 -Wall -Wextra -Werror`로 빌드할 수 있습니다.

기본 검사 스크립트는 보존한 현재 파일 프로필(true, 상한 50%, 기본 +10%, 5000 ms)을 확인합니다.
다른 프로필을 별도 검토하여 적용한 경우에만 `-AllowConfigured`를 명시합니다.
이는 프로필 일치 비교만 생략하며, 논리 시험·핀맵·통신/보고 검사는 그대로 수행합니다.
Arduino 컴파일은 실제 설정 범위도 `static_assert`로 확인합니다.

### 역방향 추가 검증 (2026-09-16)

- 수정 파일: 이 폴더의 `.ino`, `vesc_bench.h`, `README.md`, `tests/test_vesc_bench.cpp`,
  `tests/check_vesc_bench.ps1` 다섯 개. 통합 PID·리니어 센서·핀맵·운용 펌웨어·공동 명세는 미변경.
- `check_vesc_bench.ps1`: **컴파일타임 검증 82개 통과**, 핀맵·구동 보호·signed 출력 소스 검사 통과.
  독립 CRC/바이트 벡터(+/-5%, +/-100%), 부호 파싱/범위·overflow, 같은 방향 SET의 종료시각 유지,
  반대 방향 SET 거부, 정지 후 새 ARM, 단일 0 표본 노화만으로 허가 금지, 통신 공백·오류·타이머 wrap 포함.
- Arduino CLI / Teensy 4.1 `--warnings all`: **오류 0·경고 0**.
  Loader 후처리 hook을 비활성화하고 `compile`만 실행했습니다.
- 실행 명령: 위 `check_vesc_bench.ps1`, 아래 컴파일, C++ 파일의 `clang-format -i` 및
  `--dry-run --Werror`, `git diff --check`와 해당 폴더 후행 공백 검사.

```powershell
arduino-cli compile --fqbn teensy:avr:teensy41 --warnings all --build-property 'recipe.hooks.postbuild.1.pattern=' --build-path 'C:\Users\Administrator\AppData\Local\Temp\saver-compression-reverse-20260916' test/compression_motor_single_test
```

설치된 Arduino IDE 내 CLI/clang-format 절대 경로로 실행했으며 Arduino15 보드 패키지 접근을
허용받아 컴파일했습니다. 기본 C++ 검사는 ARM 컴파일러의 `-fsyntax-only`/`static_assert` 평가로,
PC 실행형 시험이나 전체 `.ino` 루프/USB/UART 스케줄링 시험이 아닙니다. 다른 폴더 시험은 이번에
재실행하지 않았습니다. **업로드·시리얼 접속·실제 역회전·정지·회생/전류 실측·커밋·푸시는 하지 않았습니다.**
기존 양방향 구동 가능 여부와 물리 방향·전원 보호 조건은 검증되지 않았습니다.

### 이전 소프트웨어 검증 이력 (2026-09-10; 아래 수치는 당시 설정)

- `check_vesc_bench.ps1`: 컴파일 타임 검증 **51개** 및 핀맵/소스 검사 통과.
  5%/5000 ms 수락 조건, 4999 ms 유지/5000 ms 종료, 6% 또는 5001 ms 거부를 포함합니다.
  확인 플래그 true와 기본/상한 5%/5000 ms인 사용자 요청 프로필로 재컴파일했습니다.
- 기존 통합 시험 71개, 음압 stage4 시험 35개, MAX30102 접촉 판정 시험 21개,
  리니어 센서 시험 24개 통과.
  이들도 컴파일 타임 시험이며, 실물 입출력 시험은 아닙니다.
- Arduino CLI 1.5.1 / Teensy 플랫폼 1.62.0 / Teensy 4.1: **오류 0개, 경고 0개**.
  Teensy Loader 실행 hook을 비활성화하여 컴파일만 했고 업로드하지 않았습니다.
  최초 제한 환경의 Arduino15 캐시 접근 실패는 권한 승인 후 재실행하여 해결했습니다.
- clang-format 포맷 검사, UTF-8/끝 공백 검사, `git diff --check` 통과.
- `teensy/test_saver_proto.cpp`는 ARM 컴파일러의 문법 검사만 통과했습니다.
  PC용 C++ 컴파일러가 없어 해당 실행형 단위시험은 실행하지 못했습니다.
- 헤더 검증은 실제 USB/UART 스케줄링·송신 큐와 VESC의 반응을 검증하지 않습니다.
  업로드, UART 실통신, 모터 회전·정지, 단선/전원 차단 시험은 하지 않았습니다.

저장소 루트에서 실행한 주요 명령입니다. `arduino-cli`, `clang-format`,
`arm-none-eabi-g++`는 설치된 실행 파일의 절대 경로로 호출했습니다.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File test/compression_motor_single_test/tests/check_vesc_bench.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File test/tests/check_combined.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File test/vacuum_motor_stage4_fg/tests/check_stage4.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File test/max30102_sensor_test/tests/check_contact.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File test/linear_sensor_test/tests/check_linear.ps1
arm-none-eabi-g++ -std=c++11 -Wall -Wextra -fsyntax-only teensy/test_saver_proto.cpp
arduino-cli compile --fqbn teensy:avr:teensy41 --warnings all --build-property 'recipe.hooks.postbuild.1.pattern=' --build-path 'C:\Users\Administrator\AppData\Local\Temp\saver-compression-e89b5f5526894acb9004ad8e28e74cc3' test/compression_motor_single_test
git diff --check
git status --short
```

새 `.ino`, `.h`, `.cpp`에 clang-format을 적용하고 같은 스타일로
`--dry-run --Werror`를 실행했습니다. 기존 파일 수정, 커밋, 푸시는 하지 않았습니다.

- [VESC UART 프레이밍](https://github.com/vedderb/bldc/blob/master/comm/packet.c)
- [명령 ID](https://github.com/vedderb/bldc/blob/master/datatypes.h),
  [값 응답·듀티/전류 명령](https://github.com/vedderb/bldc/blob/master/comm/commands.c)
- [CRC](https://github.com/vedderb/bldc/blob/master/util/crc.c),
  [VESC timeout](https://github.com/vedderb/bldc/blob/master/timeout.c),
  [FOC 전류 0/release 동작](https://github.com/vedderb/bldc/blob/master/motor/mcpwm_foc.c)
- [Makerbase 제조사 자료 위치](https://github.com/makerbase-mks/VESC-MKS/tree/main/02_Makerbase%20VESC%20related%20documents)

실제 장착 펌웨어와 UART 포트의 호환성·VESC 설정·전압·회전 방향·정지시간·회생 보호·
전류 정확도는 아직 확인하지 않았습니다. 파일 작성/컴파일은 업로드나 실기 구동 성공을 뜻하지 않습니다.
