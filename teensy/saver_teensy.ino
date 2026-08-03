/* ============================================================================
 * saver_teensy.ino — SAVER Teensy 4.1 통신 골격  (담당: 한건민)
 *
 * 명세: protocol/teensy_usb_protocol_v1.md  v1.0
 *
 * 이 파일이 하는 일
 *   - Jetson 명령 수신 · 파싱 · ACK · 중복 차단
 *   - 20 Hz 텔레메트리 송신
 *   - 통신 워치독
 *   - Teensy 내부 상태 관리
 *
 * 이 파일이 하지 않는 일  →  [구현] 표시된 곳을 채워 주세요
 *   - 실제 모터 PID/PWM, 엔코더, 로드셀, 압력센서, MAX30102 읽기
 *
 * 이전 작업 참고:  teensy/reference/
 *   음압 모터 드라이버 신호(ERR/RUN/EN/VSP/FG), 압력센서 변환식,
 *   목표 음압 후보값, PCB 회로도가 거기 정리돼 있습니다.
 *   ※ 그 코드는 ESP32-S3 기준이라 핀 번호와 ADC 분해능이 다릅니다.
 *
 * 통신 계층과 제어 계층을 섞지 마세요. 제어 루프는 아래 controlTick() 안에서만
 * 돌리고, 통신은 이 파일이 알아서 합니다. 그래야 통신이 막혀도 제어가 안 멈춥니다.
 * ==========================================================================*/
#include "saver_proto.h"

/* ──────────────────────────── 1. 설정값 ────────────────────────────
 * ⚠ 아래 TODO 값은 명세 §11의 미확정 항목입니다.
 *    추측으로 채우지 말고, 확정 전에는 해당 기능을 켜지 마세요.            */
const uint32_t TEL_PERIOD_MS_DEFAULT = 50;      /* 20 Hz */
const uint32_t COMM_WATCHDOG_MS      = 2000;    /* 명세 §9 */
const uint32_t T_COMM_MS             = 30000;   /* TODO 확정: 한성윤 (의학 근거) */
const uint32_t CONTROL_PERIOD_US     = 2000;    /* 500 Hz 제어 루프 */

const float I_LIMIT_A       = 0.0f;   /* TODO 확정: 한건민 — 0 이면 과전류 보호 비활성 */
const float VAC_TARGET_KPA  = 0.0f;   /* TODO 확정: 서정준+한건민
                                         참고: 이전 ESP32 코드에 30000 Pa(=-30 kPa),
                                         허용오차 500 Pa 가 있었음.
                                         teensy/reference/Pressure_logic_esp32.ino */
const float VAC_TOL_KPA     = 0.5f;   /* 허용 오차. 위 참고값 기준 */
const float TEMP_FAN_ON_C   = 40.0f;  /* 팬 가동 온도. 이전 코드 기준 */
const float DEPTH_TARGET_MM = 0.0f;   /* TODO 확정: 한성윤 (가이드라인 근거) */
const float RATE_TARGET_CPM = 110.0f; /* 확정됨: BPM 110 */

/* ──────────────────────────── 2. 상태 ──────────────────────────── */
enum State {
  ST_IDLE, ST_ARMED, ST_HOMED, ST_VACUUM_HOLD,
  ST_CPR_READY, ST_CPR_RUNNING, ST_CPR_PAUSED, ST_FAULT, ST_ESTOP
};
const char* STATE_NAME[] = {
  "IDLE","ARMED","HOMED","VACUUM_HOLD",
  "CPR_READY","CPR_RUNNING","CPR_PAUSED","FAULT","ESTOP"
};

State      g_state       = ST_IDLE;
SeqDedupe  g_seq;
char       g_line[SAVER_MAX_LINE];
size_t     g_len         = 0;
bool       g_overflow    = false;
uint32_t   g_telPeriod   = TEL_PERIOD_MS_DEFAULT;
uint32_t   g_lastTel     = 0;
uint32_t   g_lastRx      = 0;      /* 마지막 수신 시각 */
uint32_t   g_commLostAt  = 0;      /* 통신 두절 시작 시각 (0=정상) */
uint32_t   g_lastCtrl    = 0;
char       g_fault[16]   = "";     /* latched 오류 코드 */

/* 목표값 */
float g_targetBpm = RATE_TARGET_CPM;
float g_targetDepthMm = DEPTH_TARGET_MM;
float g_forceMaxN = 0.0f;

/* 센서 최신값 — [구현] 실제 측정값으로 채우세요 */
struct Sensors {
  float rate_cpm, depth_mm, force_N, motor_i_A, vac_kPa;
  float ppg_hr, spo2, ppg_sqi;
  float temp_C, fan_pct;
  bool  rate_ok, depth_ok, force_ok, motor_i_ok, vac_ok;
  bool  ppg_hr_ok, spo2_ok, ppg_sqi_ok, temp_ok, fan_ok;
  bool  estop, limit_top, limit_bottom, vac_leak;
} g_s;

/* ──────────────────────────── 3. 송신 ──────────────────────────── */
char g_out[SAVER_MAX_LINE];

void sendAck(long seq, bool ok, const char* err, const char* why) {
  buildAck(g_out, sizeof g_out, seq, ok, STATE_NAME[g_state], err, why);
  Serial.println(g_out);
}
void sendEvt(const char* evt) {
  buildEvt(g_out, sizeof g_out, millis(), evt);
  Serial.println(g_out);
}
void sendErr(const char* code, int sev, bool latched, const char* why) {
  buildErr(g_out, sizeof g_out, millis(), code, sev, latched, why);
  Serial.println(g_out);
}
void sendTel() {
  Telemetry t = {};
  t.state = STATE_NAME[g_state];  t.ms = millis();
  t.cp_rate_cpm  = g_s.rate_cpm;   t.cp_rate_ok    = g_s.rate_ok;
  t.cp_depth_mm  = g_s.depth_mm;   t.cp_depth_ok   = g_s.depth_ok;
  t.cp_force_N   = g_s.force_N;    t.cp_force_ok   = g_s.force_ok;
  t.cp_motor_i_A = g_s.motor_i_A;  t.cp_motor_i_ok = g_s.motor_i_ok;
  t.vac_kPa      = g_s.vac_kPa;    t.vac_ok        = g_s.vac_ok;
  t.ppg_hr_bpm   = g_s.ppg_hr;     t.ppg_hr_ok     = g_s.ppg_hr_ok;
  t.spo2_pct     = g_s.spo2;       t.spo2_ok       = g_s.spo2_ok;
  t.ppg_sqi      = g_s.ppg_sqi;    t.ppg_sqi_ok    = g_s.ppg_sqi_ok;
  t.temp_C       = g_s.temp_C;     t.temp_ok       = g_s.temp_ok;
  t.fan_pct      = g_s.fan_pct;    t.fan_ok        = g_s.fan_ok;
  t.estop = g_s.estop; t.limit_top = g_s.limit_top;
  t.limit_bottom = g_s.limit_bottom; t.vac_leak = g_s.vac_leak;
  char fbuf[24] = "";
  if (g_fault[0]) snprintf(fbuf, sizeof fbuf, "\"%s\"", g_fault);
  t.faults = fbuf;
  buildTel(g_out, sizeof g_out, t);
  Serial.println(g_out);
}

/* ──────────────────────── 4. 안전 · 고장 ──────────────────────── */
void motorsOff() {
  /* [구현] PWM 0, 드라이버 Enable LOW. 음압은 유지한다 (명세 §7)

     음압 모터 드라이버 신호 (teensy/reference/ 에서 확인):
       ERR  입력  LOW  → 드라이버 오류        → 2-VU-007
       RUN  입력  LOW  → 모터 동작 중
       EN   출력  LOW  → 긴급 정지, HIGH → 재가동
       VSP  출력  속도 지령 PWM (0~3.3V → 레벨 변환 0~5V)
       FG   입력  속도 피드백 펄스                                */
}

void raiseFault(const char* code, int sev, const char* why) {
  if (sev >= 3) { motorsOff(); g_state = ST_FAULT; }   /* 정지가 먼저, 보고가 나중 */
  snprintf(g_fault, sizeof g_fault, "%s", code);
  sendErr(code, sev, sev >= 3, why);
}

/* E-stop 은 하드웨어 회로가 이미 모터를 끊은 상태. 여기서는 보고만 한다. */
void checkEstop() {
  bool e = false;   /* [구현] digitalRead(PIN_ESTOP) 등 */
  g_s.estop = e;
  if (e && g_state != ST_ESTOP) {
    g_state = ST_ESTOP;
    sendErr("3-MU-002", 3, true, "ESTOP_ACTIVE");
  }
}

/* ──────────────────────── 5. 명령 처리 ──────────────────────── */
bool requireState(long seq, State need, const char* why) {
  if (g_state == need) return true;
  sendAck(seq, false, "2-CU-011", why);
  return false;
}

void handleCommand(const char* line) {
  long v = 0;
  if (!jsonInt(line, "v", &v) || v != SAVER_PROTO_VERSION) {
    sendErr("3-CM-001", 3, true, "VERSION_MISMATCH");
    motorsOff(); g_state = ST_FAULT;
    return;
  }
  long seq = 0;
  if (!jsonInt(line, "seq", &seq)) return;          /* seq 없는 명령은 무시 */

  char cmd[24];
  if (!jsonStr(line, "cmd", cmd, sizeof cmd)) return;

  /* 중복 명령: 재실행하지 않고 ACK 만 다시 보낸다 (명세 §4.1) */
  if (g_seq.isDuplicate(seq)) { sendAck(seq, true, NULL, NULL); return; }
  g_seq.remember(seq);

  /* E-stop 중에는 상태 조회만 허용 */
  if (g_state == ST_ESTOP && strcmp(cmd, "GET_STATUS") && strcmp(cmd, "PING")) {
    sendAck(seq, false, "3-MU-002", "ESTOP_ACTIVE");
    return;
  }

  if (!strcmp(cmd, "PING")) {
    sendAck(seq, true, NULL, NULL);

  } else if (!strcmp(cmd, "GET_STATUS")) {
    sendAck(seq, true, NULL, NULL);
    sendTel();

  } else if (!strcmp(cmd, "ARM")) {
    if (!requireState(seq, ST_IDLE, "NOT_IDLE")) return;
    /* [구현] 인터록 전체 검사: 리미트, 전원전압, 드라이버 상태 */
    g_state = ST_ARMED;
    sendAck(seq, true, NULL, NULL);

  } else if (!strcmp(cmd, "DISARM")) {
    motorsOff(); g_state = ST_IDLE;
    sendAck(seq, true, NULL, NULL);

  } else if (!strcmp(cmd, "HOME")) {
    if (!requireState(seq, ST_ARMED, "NOT_ARMED")) return;
    sendAck(seq, true, NULL, NULL);
    /* [구현] 원점 복귀. 완료되면 아래 두 줄 실행 */
    g_state = ST_HOMED;
    sendEvt("HOMING_DONE");

  } else if (!strcmp(cmd, "SET_TARGET")) {
    float f;
    if (jsonNum(line, "bpm", &f))         g_targetBpm = f;
    if (jsonNum(line, "depth_mm", &f))    g_targetDepthMm = f;
    if (jsonNum(line, "force_max_N", &f)) g_forceMaxN = f;
    sendAck(seq, true, NULL, NULL);

  } else if (!strcmp(cmd, "VACUUM_ON")) {
    if (!requireState(seq, ST_HOMED, "NOT_HOMED")) return;
    sendAck(seq, true, NULL, NULL);
    /* [구현] 음압 펌프 기동, 목표 도달 확인 후: */
    g_state = ST_VACUUM_HOLD;
    sendEvt("VACUUM_OK");

  } else if (!strcmp(cmd, "VACUUM_OFF")) {
    /* [구현] 음압 해제 */
    sendAck(seq, true, NULL, NULL);
    sendEvt("VACUUM_RELEASED");

  } else if (!strcmp(cmd, "STIFFNESS_SCAN")) {
    if (!requireState(seq, ST_VACUUM_HOLD, "NOT_FIXED")) return;
    sendAck(seq, true, NULL, NULL);
    /* [구현] 저속 탐침 압박으로 위치별 강성 측정 → SCAN_RESULT 송신
       원시 배열은 Jetson 이 해석하고, 압박점 결정은 SET_CP_POINT 로 되돌아온다 */

  } else if (!strcmp(cmd, "SET_CP_POINT")) {
    if (!requireState(seq, ST_VACUUM_HOLD, "NOT_FIXED")) return;
    /* [구현] 압박점 이동 */
    g_state = ST_CPR_READY;
    sendAck(seq, true, NULL, NULL);

  } else if (!strcmp(cmd, "TRIAL_COMPRESSION")) {
    if (!requireState(seq, ST_CPR_READY, "NOT_READY")) return;
    sendAck(seq, true, NULL, NULL);
    /* [구현] 시험 압박 → TRIAL_RESULT 송신 */

  } else if (!strcmp(cmd, "START_CPR")) {
    if (!requireState(seq, ST_CPR_READY, "NOT_READY")) return;
    if (g_targetDepthMm <= 0.0f) {          /* 미확정 값으로 구동 금지 */
      sendAck(seq, false, "2-CU-012", "TARGET_NOT_SET");
      return;
    }
    g_state = ST_CPR_RUNNING;
    sendAck(seq, true, NULL, NULL);

  } else if (!strcmp(cmd, "PAUSE_CPR")) {
    if (!requireState(seq, ST_CPR_RUNNING, "NOT_RUNNING")) return;
    motorsOff(); g_state = ST_CPR_PAUSED;
    sendAck(seq, true, NULL, NULL);

  } else if (!strcmp(cmd, "RESUME_CPR")) {
    if (!requireState(seq, ST_CPR_PAUSED, "NOT_PAUSED")) return;
    g_state = ST_CPR_RUNNING;
    sendAck(seq, true, NULL, NULL);

  } else if (!strcmp(cmd, "STOP_CPR")) {
    motorsOff();
    if (g_state == ST_CPR_RUNNING || g_state == ST_CPR_PAUSED) g_state = ST_HOMED;
    sendAck(seq, true, NULL, NULL);

  } else if (!strcmp(cmd, "RESET_FAULT")) {
    /* [구현] 원인이 해소됐는지 먼저 확인. 해소 안 됐으면 거부해야 한다 */
    bool cleared = false;
    if (!cleared) { sendAck(seq, false, g_fault, "FAULT_PERSISTS"); return; }
    g_fault[0] = '\0'; g_state = ST_IDLE;
    sendAck(seq, true, NULL, NULL);

  } else if (!strcmp(cmd, "SET_TEL_RATE")) {
    long hz;
    if (jsonInt(line, "hz", &hz) && hz >= 1 && hz <= 50) {
      g_telPeriod = 1000UL / (uint32_t)hz;
      sendAck(seq, true, NULL, NULL);
    } else sendAck(seq, false, "1-CM-004", "BAD_RATE");

  } else {
    sendAck(seq, false, "1-CM-005", "UNKNOWN_COMMAND");
  }
}

/* ──────────────────────── 6. 수신 루프 ──────────────────────── */
void pollSerial() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      g_line[g_len] = '\0';
      if (g_overflow) {
        sendErr("1-CM-003", 1, false, "LINE_TOO_LONG");   /* 줄만 버리고 계속 */
      } else if (g_len > 0) {
        g_lastRx = millis();
        g_commLostAt = 0;
        handleCommand(g_line);
      }
      g_len = 0; g_overflow = false;
    } else if (g_len < SAVER_MAX_LINE - 1) {
      g_line[g_len++] = c;
    } else {
      g_overflow = true;
    }
  }
}

/* ──────────────────── 7. 통신 워치독 (명세 §9) ──────────────────── */
void commWatchdog() {
  uint32_t now = millis();
  if (now - g_lastRx < COMM_WATCHDOG_MS) return;

  if (g_commLostAt == 0) {
    g_commLostAt = now;
    if (g_state != ST_CPR_RUNNING) {          /* 압박 중이 아니면 즉시 정지 */
      motorsOff();
      raiseFault("3-CM-010", 3, "COMM_LOST");
    }
    /* 압박 중이면 마지막 목표값으로 계속 — T_COMM_MS 까지만 */
  } else if (g_state == ST_CPR_RUNNING && now - g_commLostAt > T_COMM_MS) {
    motorsOff();
    raiseFault("3-CM-010", 3, "COMM_LOST_TIMEOUT");
    /* [구현] 부저 경보 */
  }
}

/* ──────────────────── 8. 실시간 제어 (한건민 영역) ──────────────────── */
void controlTick() {
  /* [구현] 500 Hz 제어 루프
     - 엔코더/변위/로드셀/전류/압력 읽기 → g_s 갱신
     - g_state == ST_CPR_RUNNING 일 때만 PID → PWM
     - 과전류·리미트 이탈은 여기서 즉시 차단하고 raiseFault() 호출

     주의: 이 함수 안에서 Serial 을 길게 쓰지 마세요. 제어 주기가 흔들립니다. */

  if (I_LIMIT_A > 0.0f && g_s.motor_i_ok && g_s.motor_i_A > I_LIMIT_A) {
    raiseFault("3-CU-021", 3, "OVER_CURRENT");
  }
}

/* ──────────────────────────── 9. 진입점 ──────────────────────────── */
void setup() {
  Serial.begin(115200);          /* Teensy 4.1 네이티브 USB 는 이 값을 무시함 */
  /* [구현] 핀 모드, 드라이버 Disable, 센서 초기화 */
  g_lastRx = millis();
  sendEvt("BOOT");
}

void loop() {
  pollSerial();
  checkEstop();

  uint32_t nowUs = micros();
  if (nowUs - g_lastCtrl >= CONTROL_PERIOD_US) { g_lastCtrl = nowUs; controlTick(); }

  uint32_t now = millis();
  if (now - g_lastTel >= g_telPeriod) { g_lastTel = now; sendTel(); }

  commWatchdog();
}
