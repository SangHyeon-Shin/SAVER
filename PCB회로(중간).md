flowchart TD

%% ================================
%% BATTERY
%% ================================
BAT["Li-Po Battery<br/>4S 12~16.8V"]

%% GND REFERENCE
GND_BUS["GND 버스바<br/>(스타 접지)"]
BAT --- GND_BUS

%% EXTERNAL POWER
DCDC_5V["외부 Buck Converter<br/>12~16.8V → 5V"]
BAT --> DCDC_5V

%% ================================
%% PCB : POWER + CONTROL
%% ================================
subgraph PCB["PCB (전원부 + 제어부 통합)"]

  %% Power
  REG33["Buck Converter<br/>5V → 3.3V"]
  REG33_IN_CAP["C_IN: 0.1µF + 22µF"]
  REG33_OUT_CAP["C_OUT: 0.1µF + 22~47µF"]

  %% Test Points
  TP_3V3_OUT["TP: 3.3V 레일 출력"]
  TP_GND1["TP: GND"]
  
  %% ESP 5V 입력 보호
  TVS_5V["TVS 다이오드<br/>(5V 입력 보호)"]
  ESP_5V_BULK["5V 벌크<br/>100µF × 2 병렬 (또는 220µF × 1)"]

  %% Control
  ESP["ESP32-S3 N16R8"]
  ESP_VDD_CAP["VDD 디커플링 (0.1µF x4 + 1µF x2)"]

  %% Test Points
  TP_ADC["TP: ADC 입력"]
  TP_I2C_SCL["TP: I²C SCL"]
  TP_I2C_SDA["TP: I²C SDA"]
  TP_UART_TX["TP: UART TX"]
  TP_UART_RX["TP: UART RX"]
  TP_1WIRE["TP: 1-Wire (DS18B20)"]
  TP_FAN_PWM["TP: 팬 PWM"]
  TP_MOTOR_PWM["TP: 모터/블로워 PWM"]

  %% ADC
  ADCIN["ADC 입력"]
  ADC_R["R 1.6kΩ"]
  ADC_C["C 1µF"]

  %% GND
  PCB_GND["PCB GND"]

  %% Connector
  CONN["확장 커넥터<br/>(3.3V + GND + 신호)"]
  MOTOR_CONN["모터/블로워 커넥터<br/>(PWM + GND)"]

  %% I2C Pull-up
  PULLUP["I²C Pull-up<br/>4.7kΩ (ESP ↔ 확장 커넥터, 원 와이어 풀업, 압력 제외)"]

  %% FAN 드라이버
  FAN_MOSFET["N-MOSFET 팬 드라이버"]

  %% PWM 직렬 저항
  MOTOR_R["PWM 직렬 저항 100Ω"]

  %% 플라이백 다이오드
  F.B["플라이백 다이오드"]

end

%% ================================
%% 5V 레일 (병렬)
%% ================================
DCDC_5V -->|"5V"| ESP
DCDC_5V -->|"5V"| REG33
DCDC_5V -->|"5V"| ESP_5V_BULK
DCDC_5V -->|"5V"| REG33_IN_CAP
DCDC_5V -->|"5V"| TVS_5V
DCDC_5V -->|"5V"| FAN

ESP_5V_BULK --- PCB_GND
REG33_IN_CAP --- PCB_GND
TVS_5V --- PCB_GND

%% ================================
%% 3.3V 레일 → 확장 커넥터 + TP
%% ================================
REG33 -->|"3.3V"| REG33_OUT_CAP
REG33 -->|"3.3V"| CONN
REG33 -->|"3.3V"| TP_3V3_OUT

REG33_OUT_CAP --- PCB_GND
REG33 --> ESP_VDD_CAP --- PCB_GND

%% ================================
%% GND → 확장 커넥터 + TP
%% ================================
PCB_GND --> CONN
PCB_GND --> TP_GND1

%% ================================
%% ADC RC → TP
%% ================================
ADC_SIGNAL["센서 압력 신호"] --> ADC_R
ADC_R -->|"ADC"| ADCIN
ADC_R --> ADC_C
ADC_C --- PCB_GND
ADCIN --> ESP
ADCIN --> TP_ADC

%% ================================
%% ESP IO (RC 회로 제거됨) + TP
%% ================================
ESP --> PULLUP --> CONN
ESP --> TP_I2C_SCL
ESP --> TP_I2C_SDA
ESP --> TP_UART_TX
ESP --> TP_UART_RX

%% ================================
%% COMPUTE + 열센서 + 팬
%% ================================
subgraph CMP["컴퓨팅"]
  JET["Jetson Nano B01"]
  DS18["DS18B20 열 센서"]
  FAN["쿨링 팬"]
end

DCDC_5V -->|"5V"| JET
JET --- GND_BUS
JET <-->|"UART / USB-UART"| ESP
DS18 --> CONN
DS18 --> TP_1WIRE

ESP --> FAN_MOSFET
ESP --> TP_FAN_PWM
FAN_MOSFET-->FAN
FAN_MOSFET--> F.B -->FAN
FAN_MOSFET --> PCB_GND

%% ================================
%% SENSORS
%% ================================
subgraph SNS["센서"]
  RADAR["레이더"]
  THERM["MLX90640"]
  PPG["PPG"]
  PRESS["압력"]
end

RADAR --> CONN
THERM --> CONN
PPG --> CONN
PRESS --> CONN
PRESS --> ADC_SIGNAL

%% ================================
%% MOTOR/BLOWER WITH FLYBACK
%% ================================
ESP --> MOTOR_R --> MOTOR_CONN
MOTOR_CONN --> GND_BUS
ESP --> TP_MOTOR_PWM

