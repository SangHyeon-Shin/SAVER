/* ============================================================================
 * saver_teensy.ino — SAVER Teensy 4.1 통신 골격  (담당: 한건민)
 *
 * 명세: protocol/teensy_usb_protocol_v1.md  v1.1
 *       (v1.1 = docs/protocol_v1.1_draft.md 8차 합의본, 2026-10-04)
 *
 * 이 파일이 하는 일
 *   - Jetson 명령 수신 · 파싱 · ACK · 동작 기록(bid·aid)과 중복 판단
 *   - 20 Hz 텔레메트리(tel)와 동작 상태(mot) 짝 송신
 *   - 통신 워치독
 *   - Teensy 내부 상태 관리
 *
 * 이 파일이 하지 않는 일  →  [구현] 표시된 곳을 채워 주세요
 *   - 실제 모터 PID/PWM, 엔코더 환산, 로드셀·전류 센서 보정, 사이클 깊이 산출
 *   - 리니어 보정은 test/linear_sensor_test에서 수행하고 EEPROM 저장값만 읽는다.
 *   ※ 설치 전 ZERO/CAL 보정, 설치 후 영점·실측 거리·하강 양수 방향 재확인 필요.
 *
 * v1.1 통신 계층 (2026-10-23, 신상현(Claude Code) 작성, 한건민 검토 필요)
 *   - 바꾼 곳에는 "v1.1" 을 적었다. 판단 순서는 saver_v11.h, 생성 형식은 saver_proto.h.
 *   - 실제 구동(HOME, 헤드 탐색, 주행, 음압, 압박)은 여전히 [구현] 자리다. 3b 의 구동 훅이 없는
 *     기능은 SAVER_HW_*_MOTION = 0 으로 두어 GET_INFO caps 에서 빼고 명령을 거부한다(성공을 꾸며 보내지 않는다).
 *   - §7.1 설정값은 모두 0(미설정)이다. 미설정이면 그 값을 쓰는 명령을 거부한다.
 *     특히 음압 해제 판정값이 없고 압력 변환식이 없으므로 vac_released 가 false 로 남아 ARM 을 거부한다
 *     (§4.6, 7차 합의: "해제 판정값 3개가 정해지기 전까지 ARM 을 받지 않는다").
 *   - PC 컴파일·시험: teensy/tests/host/ (README 의 v1.1 절).
 *
 * 이전 작업 참고:  teensy/reference/
 *   음압 모터 드라이버 신호(ERR/RUN/EN/VSP), 압력센서 변환식,
 *   목표 음압 후보값, PCB 회로도가 거기 정리돼 있습니다.
 *   ※ 그 코드는 ESP32-S3 기준이라 핀 번호와 ADC 분해능이 다릅니다.
 *   ※ 2026-10-02: 음압 FG는 계속 미사용. 32/37은 음압 2 RUN/EN으로 재배정.
 *      WT901C는 Serial8 34/35 예약, NAU7802는 독립 Wire1 17/16 배정.
 *      두 센서의 메인 취득 기능은 아직 미구현이며 Hall은 VESC에만 연결한다.
 *
 * 통신 계층과 제어 계층을 섞지 마세요. 제어 루프는 아래 controlTick() 안에서만
 * 돌리고, 통신은 이 파일이 알아서 합니다. 그래야 통신이 막혀도 제어가 안 멈춥니다.
 * ==========================================================================*/
#include <Wire.h>
#include <EEPROM.h>
#include <math.h>
#include <MAX30105.h>       /* SparkFun MAX3010x library 1.1.2 */
#include <spo2_algorithm.h>

#include "saver_proto.h"
#include "saver_v11.h"
#include "pinmap.h"
#include "src/saver_linear/linear_sensor_reader.h"

static_assert(PIN_LINEAR_SENSOR == saver_linear::storage::PIN,
              "Linear EEPROM calibration requires A16 / physical pin 40");
static_assert(E2END + 1 >= saver_linear::storage::ADDRESS + saver_linear::storage::SIZE,
              "Linear EEPROM calibration slot does not fit");

/* ──────────────────────────── 1. 설정값 ────────────────────────────
 * ⚠ 아래 TODO 값은 명세 §11의 미확정 항목입니다.
 *    추측으로 채우지 말고, 확정 전에는 해당 기능을 켜지 마세요.            */
const uint32_t TEL_PERIOD_MS_DEFAULT = 50;      /* 20 Hz */
const uint32_t COMM_WATCHDOG_MS      = 2000;    /* 명세 §9 */
const uint32_t T_COMM_MS             = 30000;   /* TODO 확정: 한성윤 (의학 근거) */
const uint32_t CONTROL_PERIOD_US     = 2000;    /* 500 Hz 제어 루프 */

const float I_LIMIT_A          = 12.0f;  /* 확정: 정상 구동 과전류 임계 */
const float I_LIMIT_FIRSTRUN_A = 6.0f;   /* 확정: 최초 구동 과전류 임계 */
const float VAC_TARGET_KPA     = -22.5f; /* 확정: 목표 음압 */
const float VAC_TOL_KPA        = 2.5f;   /* 확정: 목표 음압 허용 오차 */
const float VAC_MIN_START_KPA  = -15.0f; /* 확정: 음압 시작 최소값 */
const float VAC_ABORT_KPA      = -10.0f; /* 확정: 음압 중단 임계 */
const float TEMP_FAN_ON_C      = 40.0f;  /* 확정: 팬 가동 온도 */
const float TEMP_STOP_C        = 70.0f;  /* 확정: 온도 정지 임계 */
const float DEPTH_TARGET_MM = 0.0f;   /* TODO 확정: 한성윤 (가이드라인 근거) */
const float RATE_TARGET_CPM = 110.0f; /* 확정됨: BPM 110 */

/* 센서 데이터시트/reference에 근거한 취득 설정. 의료 판단 임계값이 아니다. */
const uint16_t ADC_MAX_12BIT          = 4095;
const float ADC_REFERENCE_V          = 3.3f;
const float PRESSURE_R_TOP_OHM       = 4700.0f;  /* 센서 OUT -> A8 */
const float PRESSURE_R_BOTTOM_OHM    = 10000.0f; /* A8 -> GND */
const float PRESSURE_DIVIDER_RATIO   =
    PRESSURE_R_BOTTOM_OHM / (PRESSURE_R_TOP_OHM + PRESSURE_R_BOTTOM_OHM);
const uint32_t MAX30102_POLL_MS      = 10;
const uint32_t MAX30102_STALE_MS     = 1000;

/* v1.1 ─ 펌웨어 판(GET_INFO fw, 16자 이하) */
const char FW_VERSION[] = "1.1.0";

/* v1.1 ─ 구동 구현 여부. 1 로 바꾸기 전에 3b 의 해당 훅을 실제로 구현해야 한다.
 * 0 이면 그 기능을 GET_INFO caps 에 넣지 않고 해당 명령을 거부한다. */
#ifndef SAVER_HW_HEAD_MOTION
#define SAVER_HW_HEAD_MOTION 0     /* HOME, HEAD_HOLD, SET_CP_POINT(접촉 영점): 헤드 상하 이동 */
#endif
#ifndef SAVER_HW_DRIVE_MOTION
#define SAVER_HW_DRIVE_MOTION 0    /* DRIVE_STEP: 후륜 주행 */
#endif

/* v1.1 §7.1 설정값. 0 = 미설정 → 그 값을 쓰는 명령을 거부한다(§7).
 * 벤치 실측값을 근거·책임자와 함께 기록한 뒤에만 넣는다(초안 §7.1 표). 임의 숫자를 넣지 않는다. */
struct V11Settings {
  uint8_t  limit_top_ok;                  /* 상단 리미트 설치·검증(1). 0 이면 HOME_UNAVAILABLE */
  float    enc_counts_per_mm_l;           /* 엔코더 환산(좌), 개/mm */
  float    enc_counts_per_mm_r;           /* 엔코더 환산(우), 개/mm */
  float    drv_speed_mm_s;                /* 주행 기본 속도(DRIVE_STEP speed_mm_s 생략 시) */
  float    drv_speed_max_mm_s;
  float    drv_accel_mm_s2;
  float    drv_step_max_mm;               /* 1회 이동 상한 */
  uint32_t drv_timeout_ms;
  uint32_t drv_settle_ms;
  uint32_t drv_settle_max_ms;
  uint32_t drv_avg_ms;                    /* 반력 평균 구간 */
  uint16_t drv_avg_min_n;                 /* 반력 평균 최소 신규 변환 수 */
  uint32_t force_stale_ms;                /* 로드셀 정체 판정 */
  float    drv_pos_tol_mm;
  uint32_t drv_enc_timeout_ms;
  float    drv_enc_diff_max_mm;           /* 모드 2: 좌우 이동량 차 허용 */
  float    drv_yaw_max_deg;
  float    drv_tilt_max_deg;
  uint32_t drv_imu_timeout_ms;
  uint8_t  drv_enc_mode;                  /* 1 = 단일 엔코더 + 자이로, 2 = 좌우. 0(미설정) → 2 */
  char     drv_enc_side;                  /* 모드 1: 'L' 또는 'R'(장치 전방 기준) */
  float    drv_track_mm;                  /* 모드 1 거리 보정용 바퀴 간격 */
  float    drv_settle_delta_yaw_deg;      /* 모드 1 정지 확인 시간창 안 방위 변화 상한 */
  float    scan_force_max_N;              /* 탐색·헤드 유지·주행 중 반력 상한 */
  float    contact_force_N;
  uint32_t contact_confirm_ms;
  float    head_search_speed_mm_s;
  float    head_search_max_mm;
  uint32_t head_timeout_ms;
  float    head_press_max_mm;
  float    vac_release_kPa;               /* §4.6 해제 판정 |vac_kPa| 기준 */
  uint32_t vac_release_ms;                /* §4.6 해제 판정 연속 시간 */
  uint32_t vac_release_timeout_ms;        /* §4.6 해제 대기 상한(0 이면 시간 초과 판정 안 함) */
};
#ifdef SAVER_HOST_HOOKS
V11Settings g_cfg = {};        /* PC 시험(teensy/tests/host)이 시험 전용 값을 넣는다 */
#else
const V11Settings g_cfg = {};  /* 전부 미설정 */
#endif

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
char       g_line[SAVER_MAX_LINE];
size_t     g_len         = 0;
bool       g_overflow    = false;
uint32_t   g_telPeriod   = TEL_PERIOD_MS_DEFAULT;
uint32_t   g_lastTel     = 0;
uint32_t   g_lastRx      = 0;      /* 마지막 수신 시각 */
uint32_t   g_commLostAt  = 0;      /* 통신 두절 시작 시각 (0=정상) */
uint32_t   g_lastCtrl    = 0;

/* 목표값 */
float g_targetBpm = RATE_TARGET_CPM;
float g_targetDepthMm = DEPTH_TARGET_MM;
float g_forceMaxN = 0.0f;
float g_vacTargetKpa = VAC_TARGET_KPA;   /* v1.1: VACUUM_ON target_kPa. [구현] 음압 제어가 쓴다 */

/* 센서 최신값 — [구현] 실제 측정값으로 채우세요 */
struct Sensors {
  float rate_cpm, depth_mm, force_N, motor_i_A, vac_kPa;
  float ppg_hr, spo2, ppg_sqi;
  float temp_C, fan_pct;
  bool  rate_ok, depth_ok, force_ok, motor_i_ok, vac_ok;
  bool  ppg_hr_ok, spo2_ok, ppg_sqi_ok, temp_ok, fan_ok;
  bool  estop, limit_top, limit_bottom, vac_leak;
  /* 보정 ZERO 기준의 현재 센서 위치. 직전 사이클 최대 깊이와 다른 내부 측정값이다. */
  float linear_position_mm;
  bool linear_position_ok, linear_within_cal_span;
} g_s;

/* v1.1 ─ 기록과 오류(§2.5, §3.1) */
saver_v11::Ledger     g_ledger;          /* act_cur, act_rej, cmd_last, aid_max. 부팅마다 비운다 */
saver_v11::FaultTable g_faults;          /* 활성 오류. tel faults 와 GET_FAULTS 가 쓴다 */
uint16_t g_bootId    = 0;                /* §2.4 boot_id. setup() 에서 EEPROM 부팅 횟수로 정한다 */
uint32_t g_tn        = 0;                /* §3.1 텔레메트리 주기 번호 */
uint32_t g_telDrops  = 0;                /* 송신 버퍼가 모자라 버린 tel·mot 짝 수 */
uint32_t g_txFail    = 0;                /* 511 byte 를 넘거나 생성에 실패해 보내지 않은 줄 수(구현 결함) */

/* v1.1 ─ 음압(§4.6) */
bool     g_vacOutput   = false;          /* 음압 출력(블로워) 켬 요청. 실제 출력은 hwVacuumOutput() [구현] */
bool     g_vacReleased = false;          /* 해제 확인. 부팅 직후 false */
bool     g_relTiming   = false;
uint32_t g_relSince    = 0;
bool     g_rearmLock   = false;          /* 음압 유지 중 DISARM 뒤 재가동 금지(§2.2) */
struct PendingRelease { bool active; uint16_t seq; uint32_t aid; uint32_t at; bool reported; };
PendingRelease g_pend = {};              /* 대기 중인 VACUUM_OFF 요청 */

/* v1.1 ─ 기준점(§5) */
bool  g_homeValid     = false;           /* 기구 HOME 원점 */
bool  g_homeLinearOk  = false;
float g_homeLinearMm  = 0.0f;            /* HOME 원점의 리니어 센서 값(head_pos_mm 기준) */
bool  g_headHold      = false;           /* head_hold_active */
bool  g_cpZero        = false;           /* 압박 접촉 영점 */
bool  g_contactValid  = false;           /* 마지막 접촉점 */
float g_contactMm     = 0.0f;
bool  g_drvOrigin     = false;           /* 주행 원점(HEAD_HOLD drv_zero:true 성공 때만) */
bool  g_drvSettled    = true;            /* 주행 정지 확인(§4.3). 엔코더가 유효할 때만 보고한다 */

/* v1.1 ─ 실행 중인 동작. 한 번에 하나(§2.5 busy) */
enum JobKind { JOB_NONE, JOB_HOME, JOB_HEAD_HOLD, JOB_CP_ZERO, JOB_DRIVE };
struct Job {
  JobKind   kind;
  uint16_t  seq;
  uint32_t  aid;
  float     press_mm;
  bool      drv_zero;
  float     dist_mm;
  float     speed_mm_s;
  bool      finished;          /* 구동 훅이 끝났다고 알림. 기록·송신은 serviceJob() */
  MotionEvt rep;               /* 구동 훅이 채운 결과와 잰 값 */
};
Job g_job = {};

saver_linear::PersistedLinearSensor& linearSensor() {
  static saver_linear::PersistedLinearSensor sensor;
  return sensor;
}

/* SparkFun Example8의 MAX30102 호환 설정과 Maxim 알고리즘을 비차단 수집으로 옮겼다.
 * 유효 플래그가 나온 HR/SpO2만 공개하며, 저장소에 정의가 없는 SQI는 공개하지 않는다. */
class Max30102Reader {
 public:
  Max30102Reader()
      : ready_(false), sampleCount_(0), samplesSinceCalc_(0),
        lastPollMs_(0), lastSampleMs_(0) {}

  void begin() {
    Wire.setSDA(PIN_MAX30102_SDA);
    Wire.setSCL(PIN_MAX30102_SCL);
    ready_ = sensor_.begin(Wire, I2C_SPEED_FAST);
    if (!ready_) return;

    const uint8_t ledBrightness = 60;
    const uint8_t sampleAverage = 4;
    const uint8_t ledMode = 2;      /* Red + IR */
    const int sampleRate = 100;
    const int pulseWidth = 411;
    const int adcRange = 4096;
    sensor_.setup(ledBrightness, sampleAverage, ledMode,
                  sampleRate, pulseWidth, adcRange);
    lastPollMs_ = millis();
    lastSampleMs_ = lastPollMs_;
  }

  void tick(Sensors& sensors, uint32_t nowMs) {
    sensors.ppg_sqi_ok = false;  /* SQI 산출 근거가 없어 값을 만들지 않는다. */
    if (!ready_) {
      invalidate(sensors);
      return;
    }

    if (nowMs - lastSampleMs_ > MAX30102_STALE_MS) {
      invalidate(sensors);
      sampleCount_ = 0;
      samplesSinceCalc_ = 0;
    }
    if (nowMs - lastPollMs_ < MAX30102_POLL_MS) return;
    lastPollMs_ = nowMs;

    sensor_.check();
    while (sensor_.available()) {
      pushSample(sensor_.getFIFOIR(), sensor_.getFIFORed());
      sensor_.nextSample();
      lastSampleMs_ = nowMs;

      if (sampleCount_ == SAMPLE_COUNT &&
          (samplesSinceCalc_ == 0 || samplesSinceCalc_ >= RECALC_SAMPLES)) {
        calculate(sensors);
        samplesSinceCalc_ = 0;
      }
    }
  }

 private:
  static const uint16_t SAMPLE_COUNT = 100;
  static const uint8_t RECALC_SAMPLES = 25;

  void invalidate(Sensors& sensors) {
    sensors.ppg_hr_ok = false;
    sensors.spo2_ok = false;
    sensors.ppg_sqi_ok = false;
  }

  void pushSample(uint32_t ir, uint32_t red) {
    if (sampleCount_ < SAMPLE_COUNT) {
      ir_[sampleCount_] = ir;
      red_[sampleCount_] = red;
      ++sampleCount_;
      if (sampleCount_ == SAMPLE_COUNT) samplesSinceCalc_ = 0;
      return;
    }

    for (uint16_t i = 1; i < SAMPLE_COUNT; ++i) {
      ir_[i - 1] = ir_[i];
      red_[i - 1] = red_[i];
    }
    ir_[SAMPLE_COUNT - 1] = ir;
    red_[SAMPLE_COUNT - 1] = red;
    ++samplesSinceCalc_;
  }

  void calculate(Sensors& sensors) {
    int32_t spo2 = 0;
    int32_t heartRate = 0;
    int8_t spo2Valid = 0;
    int8_t heartRateValid = 0;
    maxim_heart_rate_and_oxygen_saturation(
        ir_, SAMPLE_COUNT, red_, &spo2, &spo2Valid, &heartRate, &heartRateValid);

    sensors.ppg_hr_ok =
        heartRateValid && heartRate >= 0 && heartRate <= 250;
    if (sensors.ppg_hr_ok) sensors.ppg_hr = (float)heartRate;

    sensors.spo2_ok = spo2Valid && spo2 >= 0 && spo2 <= 100;
    if (sensors.spo2_ok) sensors.spo2 = (float)spo2;
  }

  MAX30105 sensor_;
  bool ready_;
  uint32_t ir_[SAMPLE_COUNT];
  uint32_t red_[SAMPLE_COUNT];
  uint16_t sampleCount_;
  uint8_t samplesSinceCalc_;
  uint32_t lastPollMs_;
  uint32_t lastSampleMs_;
};

Max30102Reader& ppgReader() {
  static Max30102Reader reader;
  return reader;
}

/* ──────────────────────────── 3. 송신 ──────────────────────────── */
char g_out[SAVER_MAX_LINE];

const char* stateName() { return STATE_NAME[g_state]; }

/* v1.1 §3.4: 한 줄 = 본문(≤511 byte) + LF 하나. CR 은 보내지 않는다(Serial.println 금지).
 * 생성에 실패했거나(-1) 길면 보내지 않는다(잘린 JSON 금지). ACK·err·evt 는 버퍼가 빌 때까지 기다린다. */
bool sendBody(const char* body, int len) {
  static char tx[SAVER_MAX_LINE + 1];
  if (!body || len <= 0 || len > SAVER_MAX_BODY) { g_txFail++; return false; }
  memcpy(tx, body, (size_t)len);
  tx[len] = '\n';
  Serial.write((const uint8_t*)tx, (size_t)len + 1);
  return true;
}

int sendAck(long seq, bool ok, const char* err, const char* why) {
  int n = buildAck(g_out, sizeof g_out, seq, ok, stateName(), err, why);
  sendBody(g_out, n);
  return n;
}
void sendEvt(const char* evt) {
  int n = buildEvt(g_out, sizeof g_out, millis(), evt);
  sendBody(g_out, n);
}
void sendErr(const char* code, int sev, bool latched, const char* why) {
  int n = buildErr(g_out, sizeof g_out, millis(), code, sev, latched, why);
  sendBody(g_out, n);
}

/* ──────────────── 3b. 구동 훅 (v1.1, [구현] 한건민) ────────────────
 * 통신 계층은 구동을 이 함수들로만 요청한다. 실제 모터 출력은 이 안에서만 한다.
 * SAVER_HW_*_MOTION 이 0 이면 통신 계층이 해당 명령을 거부하므로 동작 훅은 불리지 않는다.
 *   ...Start() : 명령 수락 때 한 번(통신 계층에서).
 *   ...Tick()  : 제어 주기(controlTick) 안에서 불린다. Serial 을 쓰지 않는다.
 *                끝났으면 true 를 돌려주고 rep.result(OK NO_CONTACT ABORTED SHORT SETTLE_FAIL),
 *                rep.code, 잰 값(*_ok = true 인 것만 싣는다)을 채운다.
 *                심각도 3 조건(반력 상한, 엔코더·로드셀·리니어·자이로 상실)은 Tick 안에서 바로
 *                raiseFault(code, 3, why) 를 부른다(출력 차단이 먼저). 동작은 FAULT 로 끝난다.
 * 아래 기본 구현은 자리표시다. 결과를 꾸며 성공으로 보고하지 않고 ABORTED(2-TU-008)로 끝낸다. */
#ifdef SAVER_HOST_HOOKS
/* PC 시험(teensy/tests/host/host_test.cpp)이 정의한다 */
void hwVacuumOutput(bool on);
bool hwVacuumReached();
void hwHomeStart();
bool hwHomeTick(MotionEvt& rep);
void hwHeadSearchStart(bool hold, float press_mm);
bool hwHeadSearchTick(MotionEvt& rep);
bool hwHeadContactClear();
void hwHeadCancel();
void hwDriveStart(float dist_mm, float speed_mm_s);
bool hwDriveTick(MotionEvt& rep);
void hwDriveZero();
bool hwDriveEncoderValid();
bool hwDriveSettled();
void hwDrivePose(MotStatus& m);
void hwMotionStopReport(MotionEvt& rep);
void hostSensorOverride(Sensors& s);
#else
/* 음압 블로워 #1·#2 출력(VSP·EN). [구현] 켜고 끄는 순서와 PWM */
void hwVacuumOutput(bool on) { (void)on; }
/* 흡착 완료 판정. [구현] 확정값 VAC_TARGET_KPA·VAC_TOL_KPA·VAC_MIN_START_KPA 로. 측정이 없으면 false */
bool hwVacuumReached() { return false; }
/* HOME: 헤드를 상단 리미트까지 올린다. [구현] 상단 리미트 검출이 완료 조건(§2.2) */
void hwHomeStart() {}
bool hwHomeTick(MotionEvt& rep) { rep.result = "ABORTED"; rep.code = "2-TU-008"; return true; }
/* 접촉 탐색(§2.3). hold=true 면 HEAD_HOLD(접촉 뒤 press_mm 더 내려 리니어 기준 위치 유지),
 * false 면 SET_CP_POINT(접촉점을 압박 영점으로). [구현] head_contact_mm, head_pos_mm, cp_force_N 을 rep 에 */
void hwHeadSearchStart(bool hold, float press_mm) { (void)hold; (void)press_mm; }
bool hwHeadSearchTick(MotionEvt& rep) { rep.result = "ABORTED"; rep.code = "2-TU-008"; return true; }
/* 시작 조건(§2.3-0): 로드셀 신규 변환으로 본 반력이 contact_force_N 미만이면 true. 모르면 false */
bool hwHeadContactClear() { return false; }
/* 헤드 하강·상승 목표 취소. 그 자리 임시 정지는 §6 2-TU-002 유지 조건을 모두 만족할 때만, 아니면 출력 차단.
 * 이미 성립한 헤드 유지(head_hold_active)는 이 함수로 풀지 않는다. [구현] */
void hwHeadCancel() {}
/* DRIVE_STEP(§4.3): 엔코더 기준 주행 → 출력 차단 → 정지 확인 → 반력 평균. [구현]
 * rep 에 drive_mm, drv_pos_mm(주행 원점 기준), drv_yaw_deg, cp_force_N, force_n */
void hwDriveStart(float dist_mm, float speed_mm_s) { (void)dist_mm; (void)speed_mm_s; }
bool hwDriveTick(MotionEvt& rep) { rep.result = "ABORTED"; rep.code = "2-TU-008"; return true; }
/* 주행 원점: 누적 위치와 방위를 0 으로(HEAD_HOLD drv_zero:true 성공 때만, §5). [구현] */
void hwDriveZero() {}
/* 주행 엔코더 유효(§4.3-2, 모드 1 은 §4.7-5). 무효면 drv_settled 를 생략한다. [구현] */
bool hwDriveEncoderValid() { return false; }
/* 정지 확인(§4.3-3): 출력 차단 뒤 drv_settle_ms 동안 변화 없음. SETTLE_FAIL 뒤에도 계속 본다. [구현] */
bool hwDriveSettled() { return false; }
/* mot 의 drv_pos_mm(원점 기준), drv_yaw_deg, drv_pitch_deg, drv_roll_deg. 모르면 *_ok=false 로 둔다. [구현] */
void hwDrivePose(MotStatus& m) { (void)m; }
/* 중단·고장으로 끝날 때 그때까지 잰 값(이동 거리 등). 모르면 비워 둔다. [구현] */
void hwMotionStopReport(MotionEvt& rep) { (void)rep; }
#endif

/* ──────────────────────── 4. 안전 · 고장 ──────────────────────── */
/* v1.1: 주행 정지 출력(BTS7960 두 개 PWM 0, EN LOW). 멱등이라 몇 번 불러도 된다. 제동은 [구현] */
void driveOutputsOff() {
  analogWrite(PIN_DRIVE1_RPWM, 0);
  analogWrite(PIN_DRIVE1_LPWM, 0);
  analogWrite(PIN_DRIVE2_RPWM, 0);
  analogWrite(PIN_DRIVE2_LPWM, 0);
  digitalWrite(PIN_DRIVE1_EN, LOW);
  digitalWrite(PIN_DRIVE2_EN, LOW);
}

/* v1.1: 압박 축 정지 출력 */
void compressionOutputOff() {
  /* [구현] 압박 드라이버 UART 정지 명령: baud와 명령 형식 확정 전에는 송신 금지. */
}

void motorsOff() {
  /* 구동계 프로토콜은 아직 없지만, 위험 정지 시 두 BTS7960에 정지 논리값을 쓴다. */
  driveOutputsOff();
  compressionOutputOff();
  hwHeadCancel();          /* v1.1: 헤드 이동 목표도 취소 */

  /* 명세 §7: 음압 유지. PIN_VAC1/2_VSP 및 PIN_VAC1/2_EN은 변경하지 않는다. */
}

void finishJob(const char* result, const char* code, const MotionEvt* rep);   /* 4c */

/* v1.1: 오류는 활성 목록(g_faults)에 넣고 err 로 보고한다. latched = 심각도 3.
 * 심각도 3: 출력 차단 → 상태 FAULT(비상정지가 우선) → 보고 → 진행 중 동작을 FAULT 로 끝냄(§4.1).
 * rep: 진행 중 동작의 잰 값(없으면 NULL) */
void raiseFaultWith(const char* code, int sev, const char* why, const MotionEvt* rep) {
  const bool latched = sev >= 3;
  if (sev >= 3) {
    motorsOff();                                   /* 정지가 먼저, 보고가 나중 */
    g_headHold = false;                            /* 압박 축 출력 차단 → 헤드 유지 끝(§2.3) */
    if (g_state != ST_ESTOP) g_state = ST_FAULT;
  }
  g_faults.raise(code, sev, latched, why);
  sendErr(code, sev, latched, why);
  if (sev >= 3) finishJob("FAULT", code, rep);
}

void raiseFault(const char* code, int sev, const char* why) { raiseFaultWith(code, sev, why, NULL); }

/* 원인이 사라진 오류를 목록에서 뺀다(래치는 RESET_FAULT 까지 남는다). [구현] 감지 코드가 부른다 */
void clearFaultCause(const char* code) { g_faults.causeCleared(code); }

/* E-stop 은 하드웨어 회로가 이미 모터를 끊은 상태. 여기서는 보고만 한다. */
void checkEstop() {
  /* [실물확인 8/5] E-stop 상태 입력은 HIGH=활성으로 배선한다고 가정한다. */
  bool e = (digitalRead(PIN_ESTOP) == HIGH);
  g_s.estop = e;
  if (e && g_state != ST_ESTOP) {
    motorsOff();                 /* v1.1: 소프트웨어 출력도 정지 쪽으로(멱등) */
    g_headHold = false;
    g_state = ST_ESTOP;
    g_faults.raise("3-MU-002", 3, true, "ESTOP_ACTIVE");
    sendErr("3-MU-002", 3, true, "ESTOP_ACTIVE");
    finishJob("FAULT", "3-MU-002", NULL);   /* §4.3 결과 우선순위 1 */
  }
  if (!e && g_faults.causePresent("3-MU-002")) g_faults.causeCleared("3-MU-002");
}

/* ──────────────────────── 4c. 동작 (v1.1 §2.5, §4) ──────────────────────── */
const char* busyName(JobKind k) {
  switch (k) {
  case JOB_HOME:      return "HOMING";
  case JOB_HEAD_HOLD: return "HEAD_SEARCH";
  case JOB_CP_ZERO:   return "CP_ZERO";
  case JOB_DRIVE:     return "DRIVE";
  default:            return "NONE";
  }
}
const char* doneEvtName(JobKind k) {
  switch (k) {
  case JOB_HOME:      return "HOMING_DONE";
  case JOB_HEAD_HOLD: return "HEAD_HELD";
  case JOB_CP_ZERO:   return "CP_ZERO_SET";
  default:            return "DRIVE_DONE";
  }
}
JobKind jobKindOf(const char* cmd) {
  if (!strcmp(cmd, "HOME"))         return JOB_HOME;
  if (!strcmp(cmd, "HEAD_HOLD"))    return JOB_HEAD_HOLD;
  if (!strcmp(cmd, "SET_CP_POINT")) return JOB_CP_ZERO;
  if (!strcmp(cmd, "DRIVE_STEP"))   return JOB_DRIVE;
  return JOB_NONE;
}
uint8_t drvEncMode() { return g_cfg.drv_enc_mode == 1 ? 1 : 2; }   /* 미설정이면 2(8차) */

/* 동작 시작. 수락 ACK 를 만들기 전에 부른다(busy 는 수락한 순간부터, §2.5) */
void startJob(JobKind kind, uint16_t seq, uint32_t aid, const char* line) {
  g_job = Job();
  g_job.kind = kind;
  g_job.seq = seq;
  g_job.aid = aid;
  switch (kind) {
  case JOB_HOME:
    if (g_state == ST_VACUUM_HOLD) g_cpZero = false;   /* 복귀 시작 순간 압박 접촉 영점 무효(§2.2) */
    g_headHold = false;                                /* 헤드 유지 중이면 유지를 끝내고 복귀(§2.2) */
    hwHomeStart();
    break;
  case JOB_HEAD_HOLD:
    jsonNum(line, "press_mm", &g_job.press_mm);
    jsonBool(line, "drv_zero", &g_job.drv_zero);
    hwHeadSearchStart(true, g_job.press_mm);
    break;
  case JOB_CP_ZERO:
    g_cpZero = false;
    hwHeadSearchStart(false, 0.0f);
    break;
  case JOB_DRIVE:
    jsonNum(line, "dist_mm", &g_job.dist_mm);
    g_job.speed_mm_s = g_cfg.drv_speed_mm_s;
    jsonNum(line, "speed_mm_s", &g_job.speed_mm_s);
    g_drvSettled = false;
    hwDriveStart(g_job.dist_mm, g_job.speed_mm_s);
    break;
  default:
    break;
  }
}

/* 동작을 끝낸다. 결과를 act_cur 에 먼저 저장하고 busy 를 푼 뒤 완료 이벤트를 보낸다(§2.5, §4.3-5).
 * rep: 구동 훅이 잰 값(NULL 이면 hwMotionStopReport 가 아는 값만). 측정할 수 없는 값은 생략한다. */
void finishJob(const char* result, const char* code, const MotionEvt* rep) {
  if (g_job.kind == JOB_NONE) return;
  MotionEvt e = {};
  if (rep) e = *rep;
  else hwMotionStopReport(e);
  e.ms = millis();
  e.evt = doneEvtName(g_job.kind);
  e.seq = g_job.seq;
  e.aid = g_job.aid;
  e.result = result;
  e.code = code;
  e.drv_enc_mode = 0;
  const bool ok = !strcmp(result, "OK");

  switch (g_job.kind) {
  case JOB_HOME:
    if (ok) {
      g_homeValid = true;
      g_homeLinearOk = g_s.linear_position_ok;
      g_homeLinearMm = g_s.linear_position_mm;
      if (g_state == ST_ARMED) g_state = ST_HOMED;
    } else {
      g_homeValid = false;                                /* 중단된 HOME: 원점 무효(§4.5) */
      if (g_state == ST_HOMED) g_state = ST_ARMED;        /* ARMED·HOMED → ARMED. VACUUM_HOLD 는 그대로 */
    }
    break;
  case JOB_HEAD_HOLD:
    if (e.head_contact_ok) { g_contactValid = true; g_contactMm = e.head_contact_mm; }   /* 접촉점은 기록 */
    g_headHold = ok;
    if (ok && g_job.drv_zero) { g_drvOrigin = true; hwDriveZero(); }   /* 실패면 주행 원점을 만들지 않는다 */
    break;
  case JOB_CP_ZERO:
    if (e.head_contact_ok) { g_contactValid = true; g_contactMm = e.head_contact_mm; }
    g_cpZero = ok;
    if (ok && g_state == ST_VACUUM_HOLD) g_state = ST_CPR_READY;
    break;
  case JOB_DRIVE:
    g_drvSettled = ok;              /* OK 는 정지 확인까지 끝난 결과. 그 밖은 확인 전 DRIVE_STEP 거부(NOT_SETTLED) */
    if (!g_drvOrigin) e.drv_pos_ok = e.drv_yaw_ok = false;
    if (!ok) e.force_ok = false;    /* cp_force_N 은 OK 이고 평균이 유효할 때만(§4.2) */
    if (drvEncMode() == 1) e.drv_enc_mode = 1;
    break;
  default:
    break;
  }

  static char buf[SAVER_MAX_LINE];
  int n = buildMotionEvt(buf, sizeof buf, e);
  if (n <= 0) {                     /* 형식에 맞지 않는 잰 값이 있으면 잰 값 없이 다시 만든다 */
    MotionEvt bare = {};
    bare.ms = e.ms; bare.evt = e.evt; bare.seq = e.seq; bare.aid = e.aid;
    bare.result = e.result; bare.code = e.code; bare.drv_enc_mode = e.drv_enc_mode;
    n = buildMotionEvt(buf, sizeof buf, bare);
  }
  g_ledger.storeDone(g_job.aid, n > 0 ? buf : "", result);   /* 저장이 먼저 */
  g_job = Job();                                              /* busy = NONE */
  sendBody(buf, n);                                           /* 송신은 그 뒤 */
}

/* 원인이 남는 동안 명령을 막는 비래치 TU 오류(§6 표)인가 */
bool causeTracked(const char* code) {
  return !strcmp(code, "2-TU-001") || !strcmp(code, "2-TU-002") || !strcmp(code, "2-TU-007");
}
const char* motionWhy(const char* code) {
  if (!strcmp(code, "2-TU-001")) return "YAW_LIMIT";
  if (!strcmp(code, "2-TU-002")) return "TILT_LIMIT";
  if (!strcmp(code, "3-TU-003")) return "SCAN_FORCE_MAX";
  if (!strcmp(code, "2-TU-004")) return "DRIVE_SHORT";
  if (!strcmp(code, "3-TU-005")) return "ENCODER_INVALID";
  if (!strcmp(code, "2-TU-006")) return "NO_CONTACT";
  if (!strcmp(code, "2-TU-007")) return "GYRO_LOST";
  if (!strcmp(code, "2-TU-008")) return "LIMIT_UNSET";
  if (!strcmp(code, "3-TU-009")) return "LOADCELL_STALE";
  if (!strcmp(code, "3-TU-010")) return "LINEAR_LOST";
  if (!strcmp(code, "3-TU-011")) return "GYRO_LOST";
  return "MOTION_ERROR";
}

/* 제어 주기에서: 구동 훅만 부르고 Serial 은 쓰지 않는다 */
void jobTick() {
  if (g_job.kind != JOB_NONE && !g_job.finished) {
    bool done = false;
    switch (g_job.kind) {
    case JOB_HOME:      done = hwHomeTick(g_job.rep); break;
    case JOB_HEAD_HOLD:
    case JOB_CP_ZERO:   done = hwHeadSearchTick(g_job.rep); break;
    case JOB_DRIVE:     done = hwDriveTick(g_job.rep); break;
    default:            break;
    }
    if (done) g_job.finished = true;
  }
  /* SETTLE_FAIL·중단 뒤에도 정지 확인을 계속한다(§4.3) */
  if (!g_drvSettled && g_job.kind != JOB_DRIVE && hwDriveSettled()) g_drvSettled = true;
}

/* loop 에서: 끝난 동작을 기록하고 완료 이벤트를 보낸다(통신 계층) */
void serviceJob() {
  if (g_job.kind == JOB_NONE || !g_job.finished) return;
  MotionEvt rep = g_job.rep;
  const char* result = rep.result ? rep.result : "ABORTED";
  const char* code = rep.code;
  if (!strcmp(result, "FAULT")) {                 /* 훅이 raiseFault 를 부르지 않고 FAULT 를 돌려준 경우 */
    raiseFaultWith(code ? code : "3-TU-010", 3, motionWhy(code ? code : ""), &rep);
    return;
  }
  if (code && strcmp(result, "OK")) {             /* ABORTED, SHORT, NO_CONTACT: err 를 따로 보낸다(§4.3) */
    const int sev = (code[0] >= '1' && code[0] <= '3') ? code[0] - '0' : 2;
    if (causeTracked(code)) g_faults.raise(code, sev, false, motionWhy(code));
    sendErr(code, sev, false, motionWhy(code));
  }
  finishJob(result, code, &rep);
}

/* ──────────────────────── 5. 명령 처리 (v1.1) ──────────────────────── */
/* 헤드가 HOME 위치 = 기구 원점이 있고 상단 리미트가 검출됨(§2.1). 리미트 극성은 실물 확인 전이다 */
bool headAtHome() { return g_homeValid && g_s.limit_top; }

bool headSettingsSet() {
  return g_cfg.scan_force_max_N > 0.0f && g_cfg.contact_force_N > 0.0f && g_cfg.contact_confirm_ms > 0 &&
         g_cfg.head_search_speed_mm_s > 0.0f && g_cfg.head_search_max_mm > 0.0f &&
         g_cfg.head_timeout_ms > 0 && g_cfg.force_stale_ms > 0;
}

bool driveSettingsSet() {
  const bool common =
      g_cfg.drv_speed_mm_s > 0.0f && g_cfg.drv_speed_max_mm_s > 0.0f && g_cfg.drv_accel_mm_s2 > 0.0f &&
      g_cfg.drv_step_max_mm > 0.0f && g_cfg.drv_timeout_ms > 0 && g_cfg.drv_settle_ms > 0 &&
      g_cfg.drv_settle_max_ms > 0 && g_cfg.drv_avg_ms > 0 && g_cfg.drv_avg_min_n > 0 &&
      g_cfg.force_stale_ms > 0 && g_cfg.drv_pos_tol_mm > 0.0f && g_cfg.drv_enc_timeout_ms > 0 &&
      g_cfg.drv_yaw_max_deg > 0.0f && g_cfg.drv_tilt_max_deg > 0.0f && g_cfg.drv_imu_timeout_ms > 0 &&
      g_cfg.scan_force_max_N > 0.0f;
  if (!common) return false;
  if (drvEncMode() == 1) {          /* §4.7-8 */
    const bool left = g_cfg.drv_enc_side == 'L', right = g_cfg.drv_enc_side == 'R';
    return (left || right) && (left ? g_cfg.enc_counts_per_mm_l : g_cfg.enc_counts_per_mm_r) > 0.0f &&
           g_cfg.drv_track_mm > 0.0f && g_cfg.drv_settle_delta_yaw_deg > 0.0f;
  }
  return g_cfg.enc_counts_per_mm_l > 0.0f && g_cfg.enc_counts_per_mm_r > 0.0f &&
         g_cfg.drv_enc_diff_max_mm > 0.0f;
}

/* 원인이 남은 오류가 이 동작 명령을 막는가(§6 "원인이 남은 동안" 열). 막으면 그 오류 코드 */
const char* blockingFault(const char* cmd) {
  if (g_state == ST_FAULT) {
    const char* c = g_faults.topCode();
    return c ? c : "2-CU-011";
  }
  if (g_faults.causePresent("2-TU-002") && strcmp(cmd, "HOME")) return "2-TU-002";
  if (!strcmp(cmd, "DRIVE_STEP")) {
    if (g_faults.causePresent("2-TU-001")) return "2-TU-001";
    if (g_faults.causePresent("2-TU-007")) return "2-TU-007";
  }
  return NULL;
}

#define SAVER_REJECT(e, w) do { *err = (e); *why = (w); return false; } while (0)

/* 동작 명령 수락 조건(§2.1, §2.2). 거부하면 err·why 를 채우고 false */
bool checkAction(const char* cmd, const char* line, const char** err, const char** why) {
  if (g_job.kind != JOB_NONE) SAVER_REJECT("2-CU-011", "BUSY");
  if (g_rearmLock && saver_v11::rearmBlocked(cmd)) SAVER_REJECT("2-CU-011", "REARM_LOCKED");
  const char* bf = blockingFault(cmd);
  if (bf) SAVER_REJECT(bf, "FAULT_ACTIVE");

  if (!strcmp(cmd, "HOME")) {
    if (g_state != ST_ARMED && g_state != ST_HOMED && g_state != ST_VACUUM_HOLD)
      SAVER_REJECT("2-CU-011", "NOT_ARMED");
    if (!g_cfg.limit_top_ok || !SAVER_HW_HEAD_MOTION) SAVER_REJECT("2-CU-011", "HOME_UNAVAILABLE");

  } else if (!strcmp(cmd, "HEAD_HOLD")) {
    if (g_state != ST_HOMED) SAVER_REJECT("2-CU-011", "NOT_HOMED");
    if (!SAVER_HW_HEAD_MOTION || !headSettingsSet() || !(g_cfg.head_press_max_mm > 0.0f))
      SAVER_REJECT("2-TU-008", "LIMIT_UNSET");
    if (g_headHold) SAVER_REJECT("2-CU-011", "ALREADY_HOLDING");
    if (!headAtHome()) SAVER_REJECT("2-CU-011", "HEAD_NOT_READY");
    float p = 0.0f;
    if (!jsonNum(line, "press_mm", &p) || !(p > 0.0f) || p > g_cfg.head_press_max_mm)
      SAVER_REJECT("2-CU-011", "PRESS_RANGE");
    if (!hwHeadContactClear()) SAVER_REJECT("2-CU-011", "IN_CONTACT");

  } else if (!strcmp(cmd, "DRIVE_STEP")) {
    if (g_state != ST_HOMED) SAVER_REJECT("2-CU-011", "NOT_HOMED");
    if (!SAVER_HW_DRIVE_MOTION || !driveSettingsSet()) SAVER_REJECT("2-TU-008", "LIMIT_UNSET");
    if (!(headAtHome() || g_headHold)) SAVER_REJECT("2-CU-011", "HEAD_NOT_READY");
    if (!g_drvSettled) SAVER_REJECT("2-CU-011", "NOT_SETTLED");
    float d = 0.0f;
    if (!jsonNum(line, "dist_mm", &d) || d == 0.0f || fabsf(d) > g_cfg.drv_step_max_mm)
      SAVER_REJECT("2-CU-011", "DIST_RANGE");
    float s = g_cfg.drv_speed_mm_s;
    if (jsonHas(line, "speed_mm_s") && !jsonNum(line, "speed_mm_s", &s)) SAVER_REJECT("2-CU-011", "SPEED_RANGE");
    if (!(s > 0.0f) || s > g_cfg.drv_speed_max_mm_s) SAVER_REJECT("2-CU-011", "SPEED_RANGE");

  } else if (!strcmp(cmd, "SET_CP_POINT")) {
    if (g_state != ST_VACUUM_HOLD) SAVER_REJECT("2-CU-011", "NOT_FIXED");
    if (!SAVER_HW_HEAD_MOTION || !headSettingsSet()) SAVER_REJECT("2-TU-008", "LIMIT_UNSET");
    /* v1.1 탐색 후 고정은 offset_mm = 0(현재 위치가 압박점)만 쓴다(§2.2). idx 와 0 아닌 offset 은 정의 없음 */
    float off = 0.0f;
    if (jsonHas(line, "idx") ||
        (jsonHas(line, "offset_mm") && (!jsonNum(line, "offset_mm", &off) || off != 0.0f)))
      SAVER_REJECT("2-CU-011", "DIST_RANGE");
    if (!headAtHome()) SAVER_REJECT("2-CU-011", "HEAD_NOT_READY");
    if (!hwHeadContactClear()) SAVER_REJECT("2-CU-011", "IN_CONTACT");

  } else if (!strcmp(cmd, "TRIAL_COMPRESSION")) {
    if (g_state != ST_CPR_READY) SAVER_REJECT("2-CU-011", "NOT_READY");
    if (!g_cpZero) SAVER_REJECT("2-CU-011", "CP_ZERO_UNSET");
    /* v1.1 에서 busy 이름과 TRIAL_RESULT 필드가 정해지지 않았다(결선 C안 미사용). 정해지면 구현 */
    SAVER_REJECT("2-TU-008", "LIMIT_UNSET");

  } else {   /* STIFFNESS_SCAN: 결선 C안에서 쓰지 않는다(§2.2). busy 이름과 SCAN_RESULT 형식 미정 */
    if (g_state != ST_VACUUM_HOLD) SAVER_REJECT("2-CU-011", "NOT_FIXED");
    SAVER_REJECT("2-TU-008", "LIMIT_UNSET");
  }
  return true;
}

void evalAction(const saver_v11::Request& rq, const char* line) {
  const char* err = NULL;
  const char* why = NULL;
  const bool ok = checkAction(rq.cmd, line, &err, &why);
  saver_v11::Record& rec =
      g_ledger.recordNew(rq, ok ? saver_v11::SLOT_ACT_CUR : saver_v11::SLOT_ACT_REJ);
  if (ok) startJob(jobKindOf(rq.cmd), rq.seq, rq.aid, line);
  int n = buildAck(rec.ack, sizeof rec.ack, rq.seq, ok, stateName(), err, why);
  sendBody(rec.ack, n);
}

/* IDLE 로 돌아갈 때: 오류 전·해제 전의 탐색 좌표와 기준점을 버린다(§2.2 RESET_FAULT, §4.5) */
void enterIdle() {
  g_state = ST_IDLE;
  g_homeValid = false;
  g_headHold = false;
  g_cpZero = false;
  g_contactValid = false;
  g_drvOrigin = false;
}

/* 즉시 변경 명령(§2.5 표). 거부하면 err·why 를 채우고 false */
bool execImmediate(const saver_v11::Request& rq, const char* line, const char** err, const char** why) {
  const char* cmd = rq.cmd;
  /* 동작 실행 중에 받는 것은 정지 방향 4개, 조회 4개, SET_TEL_RATE 뿐(§2.5) */
  if (g_job.kind != JOB_NONE && strcmp(cmd, "SET_TEL_RATE")) SAVER_REJECT("2-CU-011", "BUSY");
  if (g_rearmLock && saver_v11::rearmBlocked(cmd)) SAVER_REJECT("2-CU-011", "REARM_LOCKED");

  if (!strcmp(cmd, "ARM")) {
    if (g_state != ST_IDLE) SAVER_REJECT("2-CU-011", "NOT_IDLE");
    if (!g_vacReleased) SAVER_REJECT("2-CU-011", "VACUUM_NOT_RELEASED");   /* §2.2, 7차 */
    /* [구현] 인터록 전체 검사: 리미트, 전원전압, 드라이버 상태 */
    g_state = ST_ARMED;

  } else if (!strcmp(cmd, "SET_TARGET")) {
    if (g_state == ST_IDLE || g_state == ST_FAULT) SAVER_REJECT("2-CU-011", "NOT_ARMED");   /* ARMED 이상 */
    float f;
    if (jsonNum(line, "bpm", &f))         g_targetBpm = f;
    if (jsonNum(line, "depth_mm", &f))    g_targetDepthMm = f;
    if (jsonNum(line, "force_max_N", &f)) g_forceMaxN = f;

  } else if (!strcmp(cmd, "VACUUM_ON")) {
    if (g_state != ST_HOMED) SAVER_REJECT("2-CU-011", "NOT_HOMED");
    float k;
    if (jsonNum(line, "target_kPa", &k)) g_vacTargetKpa = k;
    g_vacOutput = true;                 /* 흡착 완료(VACUUM_HOLD, VACUUM_OK)는 측정으로 확인한 뒤(vacuumTick) */
    hwVacuumOutput(true);

  } else if (!strcmp(cmd, "VACUUM_OFF")) {
    g_vacOutput = false;                /* ACK 는 수락만. 상태와 rearm_lock 은 해제 확인까지 그대로(§4.6) */
    hwVacuumOutput(false);
    g_pend.active = true;               /* 대기 중인 해제 요청. 새 요청이면 바꾸고 시간 초과를 다시 잰다 */
    g_pend.seq = rq.seq;
    g_pend.aid = rq.aid;
    g_pend.at = millis();
    g_pend.reported = false;

  } else if (!strcmp(cmd, "START_CPR")) {
    if (g_state != ST_CPR_READY) SAVER_REJECT("2-CU-011", "NOT_READY");
    if (!g_cpZero) SAVER_REJECT("2-CU-011", "CP_ZERO_UNSET");
    if (g_targetDepthMm <= 0.0f) SAVER_REJECT("2-CU-012", "TARGET_NOT_SET");   /* 미확정 값으로 구동 금지 */
    g_state = ST_CPR_RUNNING;

  } else if (!strcmp(cmd, "RESUME_CPR")) {
    if (g_state != ST_CPR_PAUSED) SAVER_REJECT("2-CU-011", "NOT_PAUSED");
    g_state = ST_CPR_RUNNING;

  } else if (!strcmp(cmd, "RESET_FAULT")) {
    /* [구현] 원인이 해소됐는지 먼저 확인. 해소 안 됐으면 거부해야 한다 */
    bool cleared = false;
    if (!cleared) {
      const char* c = g_faults.topCode();
      SAVER_REJECT(c ? c : "2-CU-011", "FAULT_PERSISTS");
    }
    g_faults.resetAll();
    enterIdle();                         /* RESET_FAULT 뒤 IDLE → ARM → HOME → 새 탐색(§2.2) */

  } else {   /* SET_TEL_RATE */
    long hz;
    if (!(jsonInt(line, "hz", &hz) && hz >= 1 && hz <= 50)) SAVER_REJECT("1-CM-004", "BAD_RATE");
    g_telPeriod = 1000UL / (uint32_t)hz;
  }
  return true;
}

void evalImmediate(const saver_v11::Request& rq, const char* line) {
  saver_v11::Record& rec = g_ledger.recordNew(rq, saver_v11::SLOT_CMD_LAST);
  const char* err = NULL;
  const char* why = NULL;
  const bool ok = execImmediate(rq, line, &err, &why);
  int n = buildAck(rec.ack, sizeof rec.ack, rq.seq, ok, stateName(), err, why);
  sendBody(rec.ack, n);
}

/* DISARM(§2.2, §4.5). 음압 유지 판정 = 음압 출력 켜짐 또는 해제 미확인(압력센서 무효 포함) */
void disarm() {
  const bool hold = g_vacOutput || !g_vacReleased;
  g_homeValid = false;
  g_contactValid = false;
  g_cpZero = false;
  g_drvOrigin = false;
  g_headHold = false;
  if (hold) {
    g_state = ST_VACUUM_HOLD;           /* 음압 유지. VACUUM_OK·목표 도달을 뜻하지 않는다 */
    g_rearmLock = true;
  } else {
    g_state = ST_IDLE;                  /* 압박·주행 출력은 이미 차단, 음압 출력도 꺼져 있다 */
  }
}

/* §2.5-0: 구조 검사를 통과한 정지 명령은 v·상태·bid·aid 검사보다 먼저 정지 출력을 낸다.
 * 같은 명령의 재전송에도 다시 낸다(멱등). 음압 출력은 바꾸지 않는다(VACUUM_OFF 는 이 예외가 아니다). */
void applyStopOutput(const char* cmd) {
  if (!strcmp(cmd, "DRIVE_STOP")) {
    driveOutputsOff();                  /* 주행 PWM 0, EN LOW(제동은 [구현]) */
    hwHeadCancel();                     /* 헤드 하강·상승 목표 취소. 이미 성립한 헤드 유지는 풀지 않는다 */
  } else {                              /* STOP_CPR·PAUSE_CPR: v1.0 정지 출력(motorsOff). DISARM: 압박·주행 차단 */
    motorsOff();
    g_headHold = false;                 /* 압박 축 출력 차단 → 헤드 유지 끝 */
  }
  if (g_job.kind != JOB_NONE) finishJob("STOPPED", NULL, NULL);   /* 원래 seq·aid 로 STOPPED, act_cur 에 저장 */
}

/* 정지 방향 명령의 상태 전이와 ACK(정지 출력은 이미 냈다). 상태가 맞지 않으면 전이 없이 상태 오류.
 * rec 가 있으면(새 aid) 그 기록에 ACK 를 남기고, 없으면(기록 사유로 거부하지 않은 정지) 기록하지 않는다. */
void execStop(uint16_t seq, const char* cmd, saver_v11::Record* rec) {
  bool ok = true;
  const char* err = NULL;
  const char* why = NULL;
  if (!strcmp(cmd, "DRIVE_STOP")) {
    /* 모든 상태에서 ACK. 상태·헤드 유지·음압은 바꾸지 않고 FAULT·ESTOP 을 풀지 않는다(§2.1) */
  } else if (g_state == ST_ESTOP) {
    ok = false; err = "3-MU-002"; why = "ESTOP_ACTIVE";
  } else if (!strcmp(cmd, "STOP_CPR")) {
    if (g_state == ST_CPR_RUNNING || g_state == ST_CPR_PAUSED) g_state = ST_HOMED;   /* v1.0 §7 */
    else { ok = false; err = "2-CU-011"; why = "NOT_RUNNING"; }
  } else if (!strcmp(cmd, "PAUSE_CPR")) {
    if (g_state == ST_CPR_RUNNING) g_state = ST_CPR_PAUSED;
    else { ok = false; err = "2-CU-011"; why = "NOT_RUNNING"; }
  } else {   /* DISARM */
    if (g_state == ST_FAULT) {           /* 상태 불변. RESET_FAULT 를 우회하지 않는다 */
      ok = false; err = g_faults.topCode(); why = "FAULT_ACTIVE";
      if (!err) err = "2-CU-011";
    } else if (g_state == ST_IDLE) {     /* v1.0: ARMED 이후 */
      ok = false; err = "2-CU-011"; why = "NOT_ARMED";
    } else {
      disarm();
    }
  }
  char* buf = rec ? rec->ack : g_out;
  size_t cap = rec ? sizeof rec->ack : sizeof g_out;
  int n = buildAck(buf, cap, seq, ok, stateName(), err, why);
  sendBody(buf, n);
}

/* 재실행 없이 원래 ACK. 끝난 동작이면 저장된 완료 이벤트도(§2.5-3·4) */
void replay(saver_v11::Slot slot) {
  const saver_v11::Record& r = g_ledger.record(slot);
  sendBody(r.ack, (int)strlen(r.ack));
  if (slot == saver_v11::SLOT_ACT_CUR && r.done) sendBody(r.evt, (int)strlen(r.evt));
}

void sendInfo(uint16_t seq) {
  const char* caps[6];
  int n = 0;
  if (SAVER_HW_DRIVE_MOTION) caps[n++] = "DRIVE";
  if (SAVER_HW_HEAD_MOTION) { caps[n++] = "HEAD_HOLD"; caps[n++] = "CP_ZERO"; }
  caps[n++] = "MOT";
  caps[n++] = "AID";
  caps[n++] = "FAULTS";
  InfoMsg m = {};
  m.ms = millis();
  m.seq = seq;
  m.fw = FW_VERSION;
  m.caps = caps;
  m.caps_n = n;
  m.boot_id = g_bootId;
  m.has_last_seq = g_ledger.hasLastSeq();
  m.last_seq = g_ledger.lastSeq();
  m.has_last_aid = g_ledger.aidMax() > 0;
  m.last_aid = g_ledger.aidMax();
  m.drv_enc_mode = drvEncMode();
  m.drv_enc_side = g_cfg.drv_enc_side;
  int len = buildInfo(g_out, sizeof g_out, m);
  sendBody(g_out, len);
}

/* §3.5: ACK → 활성 오류마다 err 한 줄(심각도 3 먼저) → FAULTS_END n */
void sendFaults(uint16_t seq) {
  sendAck(seq, true, NULL, NULL);
  const saver_v11::FaultEntry* fe[saver_v11::FaultTable::N];
  const int nf = g_faults.sorted(fe, saver_v11::FaultTable::N);
  unsigned sent = 0;
  for (int i = 0; i < nf; i++) {
    int n = buildErr(g_out, sizeof g_out, millis(), fe[i]->code, fe[i]->sev, fe[i]->latched, fe[i]->why);
    if (sendBody(g_out, n)) sent++;
  }
  int n = buildFaultsEnd(g_out, sizeof g_out, millis(), seq, sent);
  sendBody(g_out, n);
}

void sendTelPair(bool wait);   /* 아래 */

/* 조회 명령(§2.5): bid·aid 없이 매번 새로 실행하고 기록하지 않는다 */
void handleQuery(uint16_t seq, const char* cmd) {
  if (!strcmp(cmd, "PING")) {
    sendAck(seq, true, NULL, NULL);
  } else if (!strcmp(cmd, "GET_STATUS")) {
    sendAck(seq, true, NULL, NULL);
    sendTelPair(true);                   /* 같은 tn 으로 tel 과 mot 을 한 번씩(§3.2) */
  } else if (!strcmp(cmd, "GET_INFO")) {
    sendAck(seq, true, NULL, NULL);
    sendInfo(seq);
  } else {                               /* GET_FAULTS */
    sendFaults(seq);
  }
}

void handleCommand(const char* line) {
  /* (0) 구조 검사: 평면 JSON 이 아니면 그 줄만 버린다(명세 §3, v1.1 §2.5-0) */
  if (!saverJsonValid(line)) {
    sendErr("1-CM-003", 1, false, "PARSE_FAIL");
    return;
  }
  char t[8];
  if (!jsonStr(line, "t", t, sizeof t) || strcmp(t, "cmd")) return;   /* 명령이 아닌 줄은 무시 */
  char cmd[32];
  if (!jsonStr(line, "cmd", cmd, sizeof cmd) || strlen(cmd) >= sizeof cmd - 1) cmd[0] = '\0';
  const saver_v11::CmdClass cls = saver_v11::classify(cmd);
  const bool stop = (cls == saver_v11::CMD_STOP);

  /* (1) 정지 방향 명령: 버전·상태·bid·aid 검사보다 먼저 정지 출력 */
  if (stop) applyStopOutput(cmd);

  /* (2) 버전 불일치: 안전 상태(출력 차단이 먼저, 보고가 나중) */
  uint32_t v = 0;
  if (!jsonU32(line, "v", &v) || v != SAVER_PROTO_VERSION) {
    raiseFault("3-CM-001", 3, "VERSION_MISMATCH");
    return;
  }

  /* (3) seq 없는 명령은 무시(v1.0) */
  uint32_t seq32 = 0;
  if (!jsonU32(line, "seq", &seq32) || seq32 < 1 || seq32 > 65535) return;
  const uint16_t seq = (uint16_t)seq32;

  /* (4) 모르는 명령: 기록하지 않는다 */
  if (cls == saver_v11::CMD_UNKNOWN) {
    sendAck(seq, false, "1-CM-005", "UNKNOWN_COMMAND");
    return;
  }

  /* (5) E-stop 중에는 조회와 DRIVE_STOP 만(v1.1 §2.2). 정지 출력은 이미 냈다 */
  if (g_state == ST_ESTOP && cls != saver_v11::CMD_QUERY && strcmp(cmd, "DRIVE_STOP")) {
    sendAck(seq, false, "3-MU-002", "ESTOP_ACTIVE");
    return;
  }

  /* (6) 조회 */
  if (cls == saver_v11::CMD_QUERY) {
    handleQuery(seq, cmd);
    return;
  }

  /* (7) 기록 판단(§2.5 판단 순서 1~7) */
  char params[saver_v11::REC_PARAMS];
  saver_v11::normalizeParams(cmd, line, g_cfg.drv_speed_mm_s, params, sizeof params);
  saver_v11::Request rq;
  rq.seq = seq;
  rq.cmd = cmd;
  rq.params = params;
  rq.has_bid = jsonU32(line, "bid", &rq.bid);
  rq.has_aid = jsonU32(line, "aid", &rq.aid);
  saver_v11::Slot slot = saver_v11::SLOT_NONE;
  const saver_v11::Verdict vd = g_ledger.evaluate(rq, &slot);

  if (vd == saver_v11::V_REPLAY) { replay(slot); return; }
  if (vd == saver_v11::V_CONFLICT) { sendAck(seq, false, "2-CU-011", "SEQ_CONFLICT"); return; }
  if (saver_v11::isRecordReason(vd)) {
    /* 정지 방향 명령은 기록 사유로 거부하지 않고 실행한다. aid_max·cmd_last·act_cur 는 그대로 */
    if (stop) execStop(seq, cmd, NULL);
    else sendAck(seq, false, "2-CU-011", saver_v11::verdictWhy(vd));
    return;
  }

  /* (8) 새 명령 */
  if (cls == saver_v11::CMD_ACTION) {
    evalAction(rq, line);
  } else if (stop) {
    saver_v11::Record& rec = g_ledger.recordNew(rq, saver_v11::SLOT_CMD_LAST);
    execStop(seq, cmd, &rec);
  } else {
    evalImmediate(rq, line);
  }
}

/* ──────────────────── 5b. 텔레메트리 짝 (v1.1 §3.1, §3.2) ──────────────────── */
void fillMot(MotStatus& m, uint32_t ms) {
  m.ms = ms;
  m.tn = g_tn;
  m.boot_id = g_bootId;
  m.busy = busyName(g_job.kind);
  if (g_job.kind != JOB_NONE) {
    m.busy_ids = true;
    m.busy_seq = g_job.seq;
    m.busy_aid = g_job.aid;
  }
  m.head_hold_active = g_headHold;
  m.drv_settled_ok = hwDriveEncoderValid();
  m.drv_settled = g_drvSettled;
  hwDrivePose(m);
  if (!g_drvOrigin) m.drv_pos_ok = m.drv_yaw_ok = false;   /* 주행 원점이 없으면 위치·방위 생략(§3.2, §4.7-1) */
  m.head_pos_ok = g_homeValid && g_homeLinearOk && g_s.linear_position_ok;
  m.head_pos_mm = g_s.linear_position_mm - g_homeLinearMm;   /* 하강 양수: 리니어 설치 방향 확인 필요 */
  m.head_contact_ok = g_contactValid;
  m.head_contact_mm = g_contactMm;
  const saver_v11::Record& cur = g_ledger.record(saver_v11::SLOT_ACT_CUR);
  if (cur.used && cur.done) {
    m.last_ok = true;
    m.last_seq = cur.seq;
    m.last_aid = cur.aid;
    m.last_cmd = cur.cmd;
    m.last_result = cur.result;
  }
  m.rearm_lock = g_rearmLock;
  m.vac_released = g_vacReleased;
}

/* tel 과 mot 은 같은 ms·tn 의 짝으로만 보낸다. 주기 송신(wait=false)은 USB 송신 버퍼가 둘 다 담을 수
 * 없으면 기다리지 않고 둘 다 버린다(제어가 막히지 않게). GET_STATUS 응답(wait=true)은 기다려서 보낸다. */
void sendTelPair(bool wait) {
  static char telBuf[SAVER_MAX_LINE];
  static char motBuf[SAVER_MAX_LINE];
  const uint32_t ms = millis();
  g_tn++;

  const saver_v11::FaultEntry* fe[saver_v11::FaultTable::N];
  const int nf = g_faults.sorted(fe, saver_v11::FaultTable::N);
  const char* codes[SAVER_TEL_FAULTS_MAX];
  const int k = nf < SAVER_TEL_FAULTS_MAX ? nf : SAVER_TEL_FAULTS_MAX;
  for (int i = 0; i < k; i++) codes[i] = fe[i]->code;

  Telemetry t = {};
  t.state = stateName();  t.ms = ms;
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
  t.sync = true;
  t.tn = g_tn;
  t.boot_id = g_bootId;
  t.faults_list = codes;
  t.faults_n = k;
  t.faults_more = nf > k;
  const int lt = buildTel(telBuf, sizeof telBuf, t);

  MotStatus m = {};
  fillMot(m, ms);
  const int lm = buildMot(motBuf, sizeof motBuf, m);

  if (lt <= 0 || lm <= 0) { g_txFail++; return; }
  if (!wait && Serial.availableForWrite() < lt + lm + 2) { g_telDrops++; return; }
  sendBody(telBuf, lt);
  sendBody(motBuf, lm);
}

void sendTel() { sendTelPair(false); }

/* ──────────────────── 5c. 음압 완료·해제 확인 (v1.1 §4.6) ──────────────────── */
void vacuumTick(uint32_t now) {
  /* 흡착 완료: 판정은 hwVacuumReached() [구현]. 측정으로 확인하기 전에는 VACUUM_HOLD 로 가지 않는다 */
  if (g_vacOutput && g_state == ST_HOMED && hwVacuumReached()) {
    g_state = ST_VACUUM_HOLD;
    sendEvt("VACUUM_OK");
  }

  /* 해제 확인: 음압 출력 꺼짐 + 압력센서 유효 + 판정값 설정 + |vac_kPa| ≤ 기준이 vac_release_ms 동안 끊김 없이.
   * 한 번이라도 벗어나면 처음부터 다시 잰다. 판정할 수 없으면 false */
  const bool cand = !g_vacOutput && g_s.vac_ok && g_cfg.vac_release_kPa > 0.0f &&
                    g_cfg.vac_release_ms > 0 && fabsf(g_s.vac_kPa) <= g_cfg.vac_release_kPa;
  if (!cand) {
    g_relTiming = false;
    g_vacReleased = false;
  } else {
    if (!g_relTiming) { g_relTiming = true; g_relSince = now; }
    g_vacReleased = (uint32_t)(now - g_relSince) >= g_cfg.vac_release_ms;
  }

  if (!g_pend.active) return;
  if (g_vacReleased) {
    /* 대기 중인 요청이 있을 때만 이벤트를 보낸다. 상태 IDLE, rearm_lock 해제 */
    int n = buildVacReleased(g_out, sizeof g_out, now, g_pend.seq, g_pend.aid, g_s.vac_ok, g_s.vac_kPa);
    g_pend.active = false;
    g_faults.causeCleared("2-VU-008");
    if (g_state != ST_FAULT && g_state != ST_ESTOP) enterIdle();
    g_rearmLock = false;
    sendBody(g_out, n);
  } else if (g_cfg.vac_release_timeout_ms > 0 && !g_pend.reported &&
             (uint32_t)(now - g_pend.at) > g_cfg.vac_release_timeout_ms) {
    g_pend.reported = true;              /* 요청당 한 번. 출력은 꺼 둔 채 VACUUM_HOLD·rearm_lock 유지 */
    g_faults.raise("2-VU-008", 2, false, "VACUUM_RELEASE_TIMEOUT");
    sendErr("2-VU-008", 2, false, "VACUUM_RELEASE_TIMEOUT");
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
void readPressureSensor(Sensors& sensors) {
  sensors.vac_ok = false;  /* 실패 시 마지막 수치 자체는 덮어쓰지 않는다. */

  uint16_t adcValue = (uint16_t)analogRead(PIN_PRESSURE_SENSOR);
  if (adcValue == 0 || adcValue >= ADC_MAX_12BIT) {
    return;
  }

  const float adcVoltage =
      (float)adcValue * (ADC_REFERENCE_V / (float)ADC_MAX_12BIT);
  const float sensorVoltage = adcVoltage / PRESSURE_DIVIDER_RATIO;
  (void)sensorVoltage;

  /*
   * [구현] 센서 전체 품번을 확인한 뒤 해당 XGZP6847A 모델의 게이지압
   * 변환식을 적용한다. KPG/KPGN/KPGPN, 압력 범위, 끝의 33 유무에 따라
   * 영점 전압과 V/kPa가 달라진다. 확정 전에는 vac_kPa를 쓰거나 vac_ok를
   * true로 만들지 않는다. 측정 불가 필드는 telemetry에서 자동 생략된다.
   */
}

void readLinearSensor(Sensors &sensors) {
  sensors.linear_position_ok = false;
  sensors.linear_within_cal_span = false;
  float positionMm;
  if (!linearSensor().position(analogRead(PIN_LINEAR_SENSOR), positionMm))
    return;
  sensors.linear_position_mm = positionMm;
  sensors.linear_position_ok = true;
  sensors.linear_within_cal_span = linearSensor().within(positionMm);
  // Stable raw ADC does not prove sensor presence or rule out a stuck voltage.
  // Keep the saved manual ZERO; never use the boot position as an automatic zero.
}

void controlTick() {
  /* [구현] 500 Hz 제어 루프
     - 엔코더/변위/로드셀/전류 읽기 → g_s 갱신
     - g_state == ST_CPR_RUNNING 일 때만 PID → PWM
     - 과전류·리미트 이탈은 여기서 즉시 차단하고 raiseFault() 호출

     주의: 이 함수 안에서 Serial 을 길게 쓰지 마세요. 제어 주기가 흔들립니다. */

  /* 활성 극성은 실물 검증 전이다. 현재 핀의 HIGH 논리 레벨을 그대로 보고한다.
   * 이 bool 필드에는 *_ok가 없어 미연결을 표현할 수 없다는 프로토콜 제약이 있다. */
  g_s.limit_top = (digitalRead(PIN_LIMIT_TOP) == HIGH);
  g_s.limit_bottom = (digitalRead(PIN_LIMIT_BOTTOM) == HIGH);

  readPressureSensor(g_s);
  readLinearSensor(g_s);
  ppgReader().tick(g_s, millis());

  /* 리니어 현재 위치는 위에서 별도로 취득한다. cp_depth_mm는 명세 §6의
   * '직전 사이클 최대 압박 깊이'이므로 현재 위치로 대체하지 않는다.
   * [구현] 설치 후 영점·하강 양수 검증 및 사이클 경계/최대 깊이 산출.
   * N/A 환산 보정값과 엔코더 PPR도 미확정이므로 해당 필드는 생략한다. */
  g_s.rate_ok = false;
  g_s.depth_ok = false;
  g_s.force_ok = false;
  g_s.motor_i_ok = false;
  /* 2선 NTC의 R25/B값/분압 저항이 미확정이므로 마지막 값은 보존하고 생략한다. */
  g_s.temp_ok = false;
  g_s.fan_ok = false;

#ifdef SAVER_HOST_HOOKS
  hostSensorOverride(g_s);   /* PC 시험 전용: 센서값 주입 */
#endif

  if (I_LIMIT_A > 0.0f && g_s.motor_i_ok && g_s.motor_i_A > I_LIMIT_A) {
    raiseFault("3-CU-021", 3, "OVER_CURRENT");
  }

  jobTick();   /* v1.1: 동작 구동 훅(Serial 없음). 끝난 동작의 보고는 loop 의 serviceJob() */
}

/* ──────────────────────────── 9. 진입점 ──────────────────────────── */
/* v1.1 §2.4 boot_id: EEPROM 부팅 횟수(1~65535 순환). 리니어 보정 칸(4096~4135) 뒤 4160~4163 에
 * 값과 보수를 둔다. [한건민 확인] 다른 스케치가 이 주소를 쓰지 않는지, 부팅마다 4 byte 쓰기가 괜찮은지 */
const int BOOT_COUNTER_ADDR = 4160;
static_assert(BOOT_COUNTER_ADDR >= saver_linear::storage::ADDRESS + (int)saver_linear::storage::SIZE,
              "boot counter overlaps the linear calibration slot");
static_assert(BOOT_COUNTER_ADDR + 4 <= E2END + 1, "boot counter does not fit EEPROM");

uint16_t bootCounterNext() {
  const int a = BOOT_COUNTER_ADDR;
  const uint16_t v = (uint16_t)(EEPROM.read(a) | (EEPROM.read(a + 1) << 8));
  const uint16_t c = (uint16_t)(EEPROM.read(a + 2) | (EEPROM.read(a + 3) << 8));
  const uint16_t prev = ((uint16_t)~v == c) ? v : 0;     /* 처음이거나 깨졌으면 0 */
  const uint16_t next = (prev == 0 || prev >= 65535) ? 1 : (uint16_t)(prev + 1);
  const uint16_t inv = (uint16_t)~next;
  EEPROM.update(a, (uint8_t)(next & 0xFF));
  EEPROM.update(a + 1, (uint8_t)(next >> 8));
  EEPROM.update(a + 2, (uint8_t)(inv & 0xFF));
  EEPROM.update(a + 3, (uint8_t)(inv >> 8));
  return next;
}

void setup() {
  /* 출력 래치를 먼저 LOW 로 두어 OUTPUT 전환 순간에도 구동 신호가 올라가지 않게 한다. */
  digitalWrite(PIN_VAC1_VSP, LOW);
  digitalWrite(PIN_VAC1_EN, LOW);
  digitalWrite(PIN_VAC2_VSP, LOW);
  digitalWrite(PIN_VAC2_EN, LOW);
  digitalWrite(PIN_FAN1_PWM, LOW);
  digitalWrite(PIN_FAN2_PWM, LOW);
  digitalWrite(PIN_DRIVE1_RPWM, LOW);
  digitalWrite(PIN_DRIVE1_LPWM, LOW);
  digitalWrite(PIN_DRIVE1_EN, LOW);
  digitalWrite(PIN_DRIVE2_RPWM, LOW);
  digitalWrite(PIN_DRIVE2_LPWM, LOW);
  digitalWrite(PIN_DRIVE2_EN, LOW);

  pinMode(PIN_VAC1_VSP, OUTPUT);
  pinMode(PIN_VAC1_EN, OUTPUT);
  pinMode(PIN_VAC2_VSP, OUTPUT);
  pinMode(PIN_VAC2_EN, OUTPUT);
  pinMode(PIN_FAN1_PWM, OUTPUT);
  pinMode(PIN_FAN2_PWM, OUTPUT);
  pinMode(PIN_DRIVE1_RPWM, OUTPUT);
  pinMode(PIN_DRIVE1_LPWM, OUTPUT);
  pinMode(PIN_DRIVE1_EN, OUTPUT);
  pinMode(PIN_DRIVE2_RPWM, OUTPUT);
  pinMode(PIN_DRIVE2_LPWM, OUTPUT);
  pinMode(PIN_DRIVE2_EN, OUTPUT);

  /* 외부 회로의 바이어스와 레벨 변환을 사용하며 내부 pull-up은 가정하지 않는다. */
  pinMode(PIN_CP_UART_RX, INPUT);
  pinMode(PIN_CP_UART_TX, INPUT);  /* Serial7 미초기화: 드라이버 명령 확정 전까지 high-Z. */
  pinMode(PIN_GYRO_UART_RX, INPUT);
  pinMode(PIN_GYRO_UART_TX, INPUT); /* Serial8 취득 미구현: RX/TX 모두 high-Z, 송신하지 않음. */
  pinMode(PIN_VAC1_ERR, INPUT);
  pinMode(PIN_VAC1_RUN, INPUT);
  pinMode(PIN_VAC2_ERR, INPUT);
  pinMode(PIN_VAC2_RUN, INPUT);
  pinMode(PIN_LIMIT_TOP, INPUT);
  pinMode(PIN_LIMIT_BOTTOM, INPUT);
  pinMode(PIN_ESTOP, INPUT);
  pinMode(PIN_CP_ENCODER_A, INPUT);
  pinMode(PIN_CP_ENCODER_B, INPUT);
  pinMode(PIN_FAN1_TACH, INPUT);
  pinMode(PIN_FAN2_TACH, INPUT);
  pinMode(PIN_DRIVE1_ENCODER_A, INPUT);
  pinMode(PIN_DRIVE1_ENCODER_B, INPUT);
  pinMode(PIN_DRIVE2_ENCODER_A, INPUT);
  pinMode(PIN_DRIVE2_ENCODER_B, INPUT);

  pinMode(PIN_TEMPERATURE_NTC, INPUT);
  pinMode(PIN_PRESSURE_SENSOR, INPUT);
  pinMode(PIN_LINEAR_SENSOR, INPUT_DISABLE); /* 보정 스케치와 동일, 디지털 입력/바이어스 없음 */
  pinMode(PIN_CURRENT_SENSOR, INPUT);
  pinMode(PIN_MAX30102_SDA, INPUT);
  pinMode(PIN_MAX30102_SCL, INPUT);
  pinMode(PIN_NAU7802_SDA, INPUT);
  pinMode(PIN_NAU7802_SCL, INPUT); /* Wire1 취득 미구현: 외부 바이어스 사용, 버스 초기화 안 함. */
  /* 23/A9와 39는 미연결 예비. VESC Hall을 Teensy 입력에 분기하지 않는다. */

  analogReadResolution(12);
  analogReadAveraging(16); /* 보정 스케치의 ADC 설정과 일치, 다른 ADC 입력에도 적용됨 */
  linearSensor().begin(EEPROM); /* 부팅 시 1회 검증/복원. EEPROM 쓰기·Serial 출력 없음. */
  g_bootId = bootCounterNext(); /* v1.1: 부팅 횟수(4 byte 쓰기). 기록부는 부팅마다 비운다 */
  g_ledger.reset(g_bootId);
  Serial.begin(115200);          /* Teensy 4.1 네이티브 USB 는 이 값을 무시함 */
  ppgReader().begin();
  g_lastRx = millis();
  sendEvt("BOOT");
}

void loop() {
  pollSerial();
  checkEstop();

  uint32_t nowUs = micros();
  if (nowUs - g_lastCtrl >= CONTROL_PERIOD_US) { g_lastCtrl = nowUs; controlTick(); }

  serviceJob();          /* v1.1: 끝난 동작 기록과 완료 이벤트 */
  vacuumTick(millis());  /* v1.1: 흡착 완료, 해제 확인 */

  uint32_t now = millis();
  if (now - g_lastTel >= g_telPeriod) { g_lastTel = now; sendTel(); }

  commWatchdog();
}
