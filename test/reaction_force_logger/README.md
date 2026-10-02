# 반력 기록 전용 시험 — DYMH-103 + NAU7802

연구용 벤치 기록기입니다. **모터를 제어하지 않습니다.** 다른 모터의 EN도 초기화하지 않으므로
이 스케치를 업로드하기 전에 **모든 모터 주전원을 분리**하세요. 사람에게 사용하는 코드가 아닙니다.

사용자가 확인한 장비: DYMH-103 0~100 kg, NAU7802 3.3 V I2C 모듈, Teensy 4.1.
로드셀 정격은 사용자 제공값이며 실제 모델별 배선·권장 여기전압·정확도·허용 하중을 검증하지 않았습니다.
100 kg은 시험 때 올려야 하는 무게도, 이 장치 전체의 안전 한계도 아닙니다.

## 범위와 상태

- NAU7802 **CH1 / gain 128 / nominal 320 SPS**, USB 요약 목표 **20 Hz**.
- NAU7802는 500 SPS를 지원하지 않습니다. 준비 비트 `CR`이 1인 경우에만 24-bit 값을 읽습니다.
  새 변환값이 전 값과 같은 것은 정상일 수 있으므로 값이 같다는 이유로 제거하지 않습니다.
- 실제 평균 수집 속도와 누락 없는 기록은 아직 실물 미검증입니다. 내부 RC 속도 오차, I2C/SD 지연이
  있으므로 정확히 320.000 Hz라고 보장하지 않습니다. 50 ms 요약은 보통 약 16개이며 고정 개수가 아닙니다.
- `test/reaction_force_logger/`에만 독립 코드를 추가합니다. 기존 모터·리니어 PID·운용 펌웨어,
  `docs/`, `protocol/`, 공용 핀맵을 변경하지 않습니다.
- USB 출력은 **벤치용 텍스트 콘솔**입니다. Jetson의 버전/seq/ACK JSON 프로토콜이 아니므로
  기존 브리지에 운용 펌웨어처럼 연결하지 마세요. 이후 통합 시 `cp_force_N` 의미와 추가 평균/최대
  필드·SD 상태를 신상현과 합의해야 합니다.

## 파일

| 파일 | 역할 |
|---|---|
| `reaction_force_logger.ino` | Arduino 진입점, I2C, 명령, 보정 수집, USB 요약 |
| `nau7802_reader.h` | 초기화/내부 ADC 보정/준비 확인/실패/시간제한 상태 처리 |
| `logger_logic.h` | 원시값 부호, N 보정, 요약, 버퍼, 시간, 명령 파싱 |
| `sd_recorder.h` | 내장 SDIO, 새 CSV 파일, 버퍼 배출, 기록 종료/오류 |
| `tests/check_logger.ps1`, `tests/test_logger.cpp` | 실제 로직의 컴파일타임 시험·모의 I2C |
| `tools/check_recording.ps1` | PC에서 CSV 구조/시각/순번/누락/종료 표시 검사 |

추가 NAU7802 라이브러리 설치는 필요 없습니다. Teensy 패키지의 `Wire`, `SdFat`를 사용합니다.
Arduino IDE에서 **이 폴더의 `reaction_force_logger.ino`**를 열고 모든 `.h`를 같은 폴더에 둡니다.
보드: **Teensy 4.1**, USB Type: **Serial**, 시리얼 모니터: 115200, New Line 또는 Both NL & CR.
부팅 시 센서 연결을 시도하지만 SD 기록은 명시적 `LOG_START` 전에는 시작하지 않습니다.

## 추후 배선 — 아직 실물 확인 필요

전원을 모두 끄고 연결합니다. 본 시험은 NAU7802만 연결해 먼저 검증하세요.

| 연결 | 대상 | 선택 이유 / 주의 |
|---|---|---|
| NAU 모듈의 **입력 전원** VCC/VIN | Teensy 3.3 V | 사용자 지정 3.3 V 모듈. 모듈 단자명이 AVDD/E+인 경우 전원 입력과 혼동 금지 |
| NAU GND | Teensy GND | 비절연 측정 기준 |
| NAU SDA | Teensy **18** | 기본 Wire I2C. A9로 연결하지 않음 |
| NAU SCL | Teensy **19** | 기본 Wire I2C |
| NAU DRDY | 미연결 | 현재 CR 준비 비트를 I2C로 폴링 |
| DYMH **EX+/E+** | 모듈 **E+ / excitation+** | 센서 여기 전원. 단자 기능을 실제 모듈 설명으로 확인 |
| DYMH **EX-/E-** | 모듈 **E- / excitation-** | 센서 여기 귀환 |
| DYMH **SG+/S+** | 모듈 CH1 **A+ / VIN1P** | 차동 신호 양극 |
| DYMH **SG-/S-** | 모듈 CH1 **A- / VIN1N** | 차동 신호 음극 |
| microSD | Teensy 4.1 내장 슬롯 | SDIO 사용, 별도 SPI 배선/CS 핀 불필요 |

**선 색만 보고 연결하지 마세요.** DYMH-103 실제 라벨/판매자 배선표로 EX/SG를 확인하세요.
Shield 처리도 실제 케이블/접지 설계 확인이 필요합니다.

- SDA/SCL 풀업은 3.3 V 기준이어야 합니다. 모듈 내장 풀업 유무·레일 확인 후 필요할 때만 추가합니다.
- MAX30102도 18/19를 사용하지만 이번 시험에서 그 센서를 초기화하거나 읽지는 않습니다.
- 설정은 **내부 LDO 3.0 V**, PGA 128배입니다. 입력 3.3 V에서 LDO 여유를 두려는 시험 설정이지
  사용자 모듈 회로를 확인했다는 뜻은 아닙니다. 실제 보드가 내부 LDO/REFP/AVDD/E+를 어떻게
  연결했는지, DYMH-103이 이 여기전압에서 사양을 만족하는지 반드시 확인하세요.
  외부 AVDD를 강제로 공급하는 보드라면 이 초기화를 그대로 쓰기 전에 회로에 맞게 수정해야 합니다.
- CH2에는 센서를 연결하지 않습니다. 코드가 CH1 고이득용 PGA 우회 커패시터 경로를 켭니다.
  실제 보드의 부품 구성이 해당 동작을 지원하는지도 확인 대상입니다.

## 명령과 사용 순서

명령은 **대문자, 한 줄씩** 보냅니다. 응답을 확인한 뒤 다음 명령을 보내세요.

| 명령 | 동작 |
|---|---|
| `STATUS` | 센서·SD·보정·누락·지연·USB 출력 유실 상태 |
| `HELP` | 명령 안내 재출력 |
| `TARE` | 현재 무부하 상태에서 새 샘플 320개 평균을 영점으로 수집, 이전 N 보정 무효화 |
| `CAL 2.5` | **실제로 2.5 kg의 알려진 하중이 걸려 있을 때만** 보정 수집. 숫자는 하중 명령이 아님 |
| `CAL_SHOW` | 영점과 counts/kg 확인. 보정은 RAM에만 있음 |
| `CAL_RESET` | 보정 삭제, 원시값만 표시 |
| `TIME YYYY-MM-DDTHH:MM:SSZ` | 현재 UTC 시각을 수동 지정. 기록 시작 전 필수 |
| `LOG_START` | 새 CSV 기록 시작. 무보정 원시 기록도 허용 |
| `LOG_STOP` 또는 `STOP` | 기록을 끝내고 버퍼 배출/파일 종료. **모터 정지 명령 아님** |

### 1. 처음 연결해 확인

모터 전원 분리, 로드셀을 하중이 정상 전달되도록 고정한 뒤 업로드합니다.
`STATUS`에서 `sensor_ok=1`과 원시값 변화를 확인합니다. `sensor_ok`는 ADC 응답/신선도이지
로드셀 연결·기계 설치·힘 정확도의 검증값이 아닙니다. 로드셀 단선은 중간 원시값으로 보일 수도 있습니다.

### 2. 무부하 영점 및 알려진 하중으로 보정

1. 센서에 시험 하중이 없는 안정된 상태에서 `TARE`를 보냅니다.
2. 약 1초간 움직이지 않고 `CAL_RESULT,ok=1,kind=TARE`를 기다립니다.
3. 고정구·로드셀 허용 범위에서 안전하게 다룰 수 있고 실제 질량을 아는 기준 하중을 적용합니다.
4. 예를 들어 **실제 기준 질량이 2.5 kg일 때만** `CAL 2.5`를 보냅니다.
5. `CAL_RESULT,ok=1,kind=KNOWN_KG`, `CAL,calibrated=1`을 확인합니다.
6. 하중을 제거해 영점 복귀를 보고, 다른 알려진 하중으로 N 환산을 별도로 확인합니다.

두 구간 raw 최소/최대가 겹치면 보정을 거부합니다. 320개 수집 중 포화 또는 큰 읽기 간격이 있으면
실패합니다. 이것은 최소 무결성 검사일 뿐 기계적 흔들림·비선형성·정확도를 인증하지 않습니다.
원시값 span과 영점 드리프트를 실물로 검토하세요. 캡처는 3초 제한이며 USB 단절/STOP/센서 오류로
취소될 수 있습니다. 취소되면 보정을 다시 해야 합니다.

```text
counts_per_kg = (loaded_raw_mean - unloaded_raw_mean) / known_mass_kg
force_N      = (raw - unloaded_raw_mean) / counts_per_kg * 9.80665
```

정적 질량으로 보정할 때 표준중력 9.80665 m/s²를 사용합니다. 동적 정확도·설치 반력과 순수 접촉력의
차이를 이 식만으로 검증할 수는 없습니다. 부호를 자동 절댓값으로 바꾸거나 음수를 0으로 자르지 않습니다.
보정 전/포화 때 `force_ok=0`이고 USB 힘 필드는 생략, CSV `force_N`은 빈칸입니다.

전원 재시작/센서 재초기화 시 보정은 사라집니다. 기록 중에는 보정과 TIME 변경을 거부합니다.
현재 EEPROM 저장/보정값 불러오기 명령은 없으며 매 부팅 시 재보정해야 합니다.

### 3. UTC 지정 후 기록

Windows PowerShell에서 다음 명령으로 **현재 UTC 명령**을 얻어 시리얼 모니터에 붙여 넣습니다.
PowerShell이 모터/시리얼 포트를 열지는 않습니다.

```powershell
'TIME ' + (Get-Date).ToUniversalTime().ToString("yyyy-MM-dd'T'HH:mm:ss'Z'")
```

수동 붙여넣기의 지연 및 MCU 발진기 오차가 있으므로 정밀한 외부 장치 시각 동기화가 아닙니다.
UTC는 파일 해석용이고 구간 계산은 계속 단조 `read_us`로 수행합니다.

`LOG_START` → `OK,LOG_STARTED,file=RF0000.CSV`를 확인합니다.
종료할 때 `LOG_STOP` 후 `EVENT,LOG_CLOSED,...,sd_fault=0`을 확인하고 **그 다음** 전원을 끄고
카드를 빼세요. 메시지를 놓쳤으면 `STATUS`에서 `sd_logging=0,sd_closing=0,sd_fault=0`을 확인합니다.
USB 단절도 기록 종료 요청을 만들고 재연결 후 자동 재시작하지 않습니다. USB가 유일 전원이면
케이블 제거 즉시 전원이 끊겨 종료를 수행할 수 없으므로 먼저 명령으로 닫아야 합니다.

## 데이터와 오류 해석

USB `DATA`는 목표 50 ms 간격의 새 샘플 구간 요약입니다.
`n`=수집수, `valid_n`=비포화 수, `rail_n`=포화 수, `window_us`=실제 구간 길이.
원시 평균/최소/최대, 보정된 경우 N 평균/최소/최대를 표시합니다. 샘플이 없으면 값 필드를 생략합니다.
USB가 느리면 해당 출력 줄을 버리고 `dropped_usb`를 올리며 SD 기록을 위해 기다리지 않습니다.

CSV는 필터로 평활화하지 않은 **ADC 출력 원시값**을 보존합니다. NAU7802 내부 디지털 필터는 존재하므로
완전한 아날로그 파형이나 320 Hz 대역폭을 의미하지 않습니다.

| CSV 열 | 의미 |
|---|---|
| `utc` | 수동 UTC 앵커로 환산한 ISO 8601 시각 |
| `read_us` | Teensy 부팅 후 확장된 단조 마이크로초, **I2C 읽기 완료 시각** |
| `seq` | 성공한 새 ADC 읽기마다 증가. 부팅 단위, uint32 순환 |
| `raw` | signed 24-bit ADC 값 |
| `force_ok`, `force_N` | 보정 유효 여부와 N. 미측정/무보정은 빈칸 |
| `adc_rail` | ADC 코드의 양/음 끝값 포화 |
| `gap_flag` | 직전 읽기와 간격이 6250 µs 초과. 진단용, 안전 임계값 아님 |
| `interval_us` | 직전 성공 읽기와 실제 시간 차이, 첫 읽기는 0 |

NAU7802에는 이 코드가 이용할 샘플 번호/FIFO가 없으므로 놓친 **ADC 변환 개수를 정확히 셀 수 없습니다**.
`seq`가 이어져도 ADC 내부 누락 0을 증명하지 않습니다. `gap_flag`, 관측 Hz, SD 최악 지연을 함께 봅니다.
`read_us`는 변환 완료 순간도, 센서 내부 필터 지연을 보정한 실제 하중 시각도 아닙니다.

- 64 KiB RAM 버퍼, 내장 FIFO SDIO, **한 번에 최대 512 byte** 쓰기.
- 세션마다 32 MiB를 사전 확보. 여유 공간 부족이면 시작 실패. 이름은 `RF0000.CSV`부터
  빈 번호를 찾아 **배타적 생성**합니다. 기존 파일은 덮어쓰거나 지우지 않습니다.
- 파일 공간 끝에서는 기록 종료. 버퍼 가득 참/센서 오류/SD 오류 시 종료 또는 실패 표시.
  `unwritten_bytes`는 앱 버퍼에서 버린 바이트이며 SD 내부 캐시/전원 단절 손실의 정확한 개수가 아닙니다.
- 초기화 실패 시 센서를 2초 간격으로 다시 시도하지만 N 보정 및 SD 기록은 자동 복구하지 않습니다.
- 파일에는 보정계수·ADC 설정·빌드 식별자·시각 출처가 포함됩니다. 정상 종료는 `# END` 행을 남깁니다.
- 갑작스런 전원 단절/카드 제거 시 데이터·파일시스템이 손상될 수 있습니다. 손상 파일이나 사전 할당된
  잔여 영역은 자동 삭제하지 않습니다. 검사 도구는 읽을 수 있는 앞부분만 보고할 수 있습니다.
- 파일 시작/최종 `truncate/sync/close`, I2C, SD 라이브러리 호출은 동기식입니다.
  Teensy Wire에는 자체 timeout이 있지만 수십 ms 지연될 수 있으며, SD busy 검사만으로 모든 정지를
  막을 수 없습니다. **이 기록기의 loop를 모터 실시간 제어에 그대로 합치면 안 됩니다.**

## PC에서 기록 검사

CSV 자체는 일반 텍스트/스프레드시트 도구로 볼 수 있습니다. 원본은 보존하세요.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File test/reaction_force_logger/tools/check_recording.ps1 -Path E:\RF0000.CSV
```

실제 카드 경로로 바꿉니다. 관측 읽기 Hz, 간격 min/max, 포화·간격 이상·순번 누락, 종료 행,
행 개수·시각/빈칸 규칙을 검사합니다. 무결성 오류가 없다는 것이 힘 정확도나 누락 없는 ADC 변환을
입증하지는 않습니다. CRC/서명 검사는 없고 모든 비트 손상을 검출하지는 못합니다.

## 나중에 해야 할 실물 시험

1. **사진/회로 확인**: NAU 모듈 단자, LDO/REFP/AVDD 구조, DYMH 선 배치·여기전압, 장착 방향 확인.
2. 모터 전원 분리 후 전원·I2C 풀업 전압 확인. 원시값/포화/센서 미연결 표시 확인.
3. 무부하/기준 하중 보정 및 다른 하중으로 검증. 영점 복귀·드리프트·크리프·잡음 확인.
4. 짧은 원시 SD 기록을 먼저 하고 `LOG_STOP` 후 CSV를 PC에서 검사.
5. 예정 실험 시간만큼 장시간 기록. 관측 샘플 속도·gap·버퍼 high-water·max_write_us·SD 오류 확인.
6. 모터 없는 상태에서 센서 분리, SD 없음/용량 부족, USB 단절 시험. 파손/전원 단절 시험은 백업된
   시험용 카드로만 진행하고 파일 손상 가능성을 전제로 함.
7. 이후 리니어 위치와 공통 시간축으로 기록하는 기능 및 모터 제어 통합을 별도 검토.
   센서/SD 장애가 보호 루프를 지연시키지 않는 구조와 실제 물리 안전장치를 먼저 검증.
8. Jetson 20 Hz 요약 연동은 프로토콜 합의 후 별도 작업. 현재는 PC 시리얼 모니터 시험용.

## 소프트웨어 검증 명령

저장소 루트에서:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File test/reaction_force_logger/tests/check_logger.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File test/reaction_force_logger/tools/check_recording.ps1 -SelfTest
arduino-cli compile --fqbn teensy:avr:teensy41 --warnings all --build-property 'recipe.hooks.postbuild.1.pattern=' test/reaction_force_logger
```

시험 결과와 남은 제약은 `VALIDATION.md`에 기록합니다. 소프트웨어 검증은 실물 동작 검증과 다릅니다.

## 근거

- [Nuvoton NAU7802 Rev2.6](https://www.nuvoton.com/export/resource-files/en-us--DS_NAU7802_DataSheet_EN_Rev2.6.pdf):
  §4.1 준비 비트, §4.2 gain/LDO, §4.3 320 SPS 및 내부 offset 보정, §4.9 24-bit burst read.
- [SparkFun NAU7802 구현](https://github.com/sparkfun/SparkFun_Qwiic_Scale_NAU7802_Arduino_Library):
  초기화 순서 교차 확인. 이번 코드는 별도의 최소 상태기계이며 이 라이브러리를 설치/복사하지 않음.
- [SdFat TeensySdioLogger](https://github.com/greiman/SdFat/blob/master/examples/TeensySdioLogger/TeensySdioLogger.ino):
  SDIO busy 확인, 섹터 단위 쓰기, 사전 할당 접근 참고.
