/* ============================================================
   결선 시연 메뉴와 유닛 화면의 표시값 (순수 함수, 스모크 시험에서 직접 검사, D-51)

   - 브리지 연결(결선 시연 모드): 받은 텔레메트리와 레이더 값만 쓴다. 필드가 없거나
     (측정 불가, 프로토콜 v1 §6) 오래됐으면(stale) '—'. 모의값이나 이전 값으로 채우지 않는다.
   - 미연결이나 모의 강제: unitSim.js 의 모의값. 화면에 '모의 데이터'로 표시한다.
   - 판단에 쓰지 않는다. 회복(ROSC)은 장치가 판정하지 않고 사람이 확인한다
     (S9 와 같은 원칙. 상태 판단 로직 정의서의 데모 모드: ROSC 감지 비활성).
   ============================================================ */
import { radarView } from './radarView.js'

export const NA = '—'
const num = (v) => typeof v === 'number' && Number.isFinite(v)

/** 메뉴의 유닛 4개. 전체 UI 는 메뉴에서 따로 그린다 */
export const UNITS = [
  { id: 'patient', title: '환자 상태', en: 'Patient', desc: '호흡 · 심박수 · 산소포화도', src: '레이더 · 손가락 센서' },
  { id: 'compress', title: '압박', en: 'Compression', desc: '깊이 · 속도 · 강도 · 횟수', src: '압박 유닛' },
  { id: 'drive', title: '구동', en: 'Drive', desc: '압박 위치 찾기', src: '운반 및 탐색 유닛' },
  { id: 'vacuum', title: '흡착', en: 'Vacuum', desc: '바닥 음압 고정', src: '흡착 유닛' },
]
export const UNIT_IDS = UNITS.map((u) => u.id)

/* ---------- 브리지 사유 코드 → 화면 문구 ---------- */
const REASON = {
  NOT_CONNECTED: '장치(Teensy)가 연결되지 않았습니다',
  LINK_CHECKING: '장치를 확인하고 있습니다 · 잠시 뒤 다시 누르세요',
  LINK_DOWN: '장치 연결이 끊겼습니다',
  TARGET_NOT_SET: '목표 깊이가 설정되지 않았습니다 (설정 파일 필요)',
  NOT_READY: '장치가 압박 준비 상태가 아닙니다 (흡착 없이 압박하려면 시연용 펌웨어 필요)',
  NO_STATUS: '장치 상태를 받지 못했습니다',
  TIMEOUT: '장치 응답이 없습니다',
  HOME_TIMEOUT: '원점 복귀가 끝나지 않았습니다',
  VACUUM_TIMEOUT: '목표 음압에 닿지 못해 해제했습니다',
  VACUUM_FAIL: '바닥 고정에 실패했습니다',
  ESTOP: '비상정지가 눌려 있습니다',
  ESTOP_ACTIVE: '비상정지가 눌려 있습니다',
  FAULT: '장치 고장 상태입니다',
  DEVICE_STOPPED: '장치가 압박을 멈췄습니다',
  UNKNOWN_COMMAND: '장치 펌웨어가 이 명령을 모릅니다',
  NOT_ARMED: '장치가 구동 허가 상태가 아닙니다',
  NOT_HOMED: '원점 복귀가 필요합니다',
  NOT_IDLE: '장치가 대기 상태가 아닙니다',
  NOT_RUNNING: '압박 중이 아닙니다',
  USER_STOP: '멈췄습니다',
  LEFT_SCREEN: '화면을 나가서 멈췄습니다',
  UI_GONE: '화면 연결이 끊겨 멈췄습니다',
  BUSY: '이미 동작 중입니다',
  NOT_ON_SCREEN: '이 화면의 유닛이 아닙니다',
  not_unit_demo: '브리지가 시연 모드가 아닙니다 (--unit-demo)',
}

export function unitReasonText(reason) {
  if (!reason) return null
  const r = String(reason)
  if (REASON[r]) return REASON[r]
  if (r.startsWith('FAULT:')) return '장치 고장: ' + r.slice(6)
  if (r.startsWith('STATE:')) return '장치 상태가 맞지 않습니다 (' + r.slice(6) + ')'
  if (r.startsWith('HOMING')) return '원점 복귀에 실패했습니다 (' + r + ')'
  if (r.startsWith('NOT_SENT')) return '명령을 보내지 못했습니다'
  if (r.startsWith('STOP_REJECTED')) return '정지 요청이 거부됐습니다 · 비상정지 버튼을 누르세요'
  if (/^\d-[A-Z]{2}-\d{3}$/.test(r)) return '장치 오류 ' + r
  return '장치가 요청을 처리하지 못했습니다 (' + r + ')'
}

const STEP = {
  GET_STATUS: '장치 상태 확인 중', ARM: '구동 허가 중', HOME: '원점 복귀 중', SET_TARGET: '목표값 설정 중',
  START_CPR: '압박 시작 중', VACUUM_ON: '바닥에 붙이는 중', SEARCH: '위치 찾는 중',
  STOP_CPR: '멈추는 중', VACUUM_OFF: '해제하는 중', DRIVE_STOP: '멈추는 중',
}
const RUNNING = { compress: '압박 중', vacuum: '고정 중', drive: '위치 찾는 중', patient: '측정 중' }

/** 유닛 동작 상태 → { text, tone }. tone: idle | busy | run | ok | warn */
export function phaseView(msg, unit) {
  const m = msg && msg.unit === unit ? msg : null
  const phase = m ? m.phase : 'idle'
  switch (phase) {
    case 'starting': return { phase, text: STEP[m.step] || '준비 중', tone: 'busy' }
    case 'running': return { phase, text: RUNNING[unit] || '동작 중', tone: 'run' }
    case 'stopping': return { phase, text: STEP[m.step] || '멈추는 중', tone: 'busy' }
    case 'stopped': return { phase, text: '멈춤', tone: 'idle', note: unitReasonText(m.reason) }
    case 'done': return { phase, text: '완료', tone: 'ok' }
    case 'failed': return { phase, text: '중단', tone: 'warn', note: unitReasonText(m.reason) }
    default: return { phase: 'idle', text: '대기', tone: 'idle' }
  }
}

/* ---------- 환자 상태: 레이더(호흡) + 손가락 센서(심박, 산소포화도) ---------- */
export function patientView(tel, stale, radar) {
  const t = tel && !stale ? tel : {}
  const hr = num(t.ppg_hr_bpm) && t.ppg_hr_bpm > 0 ? t.ppg_hr_bpm : null
  const spo2 = num(t.spo2_pct) && t.spo2_pct > 0 && t.spo2_pct <= 100 ? t.spo2_pct : null
  const rv = radarView(radar, { hold: true })          // 압박 없는 화면이라 그대로 보인다(D-50 의 압박 중 제한과 무관)
  const r = radar || {}
  return {
    rr: rv.rr,
    rrNum: rv.live && num(r.rr_bpm) && r.rr_bpm > 0 ? r.rr_bpm : null,
    radarHr: rv.hr,
    radarOn: rv.live,
    radarStatus: rv.status,
    hr: hr === null ? NA : `${Math.round(hr)} 회/분`,
    hrNum: hr,
    spo2: spo2 === null ? NA : `${Math.round(spo2)} %`,
    spo2Num: spo2,
    // 손가락 센서가 유효하다고 보낸 맥박 값이 있다. 센서 신호 여부일 뿐 회복 판정이 아니다
    pulse: hr !== null,
  }
}

/* ---------- 압박 ---------- */
export function compressView(tel, stale, cfg) {
  const t = tel && !stale ? tel : {}
  const c = cfg || {}
  const depth = num(t.cp_depth_mm) ? t.cp_depth_mm : null
  const rate = num(t.cp_rate_cpm) ? t.cp_rate_cpm : null
  const force = num(t.cp_force_N) ? t.cp_force_N : null
  const pos = (v) => (num(v) && v > 0 ? v : null)
  return {
    running: t.state === 'CPR_RUNNING',
    state: t.state || null,
    depthNum: depth, rateNum: rate, forceNum: force,
    depth: depth === null ? NA : `${depth.toFixed(1)} mm`,
    rate: rate === null ? NA : `${Math.round(rate)} 회/분`,
    force: force === null ? NA : `${Math.round(force)} N`,
    forceKg: force === null ? null : `약 ${Math.round(force / 9.81)} kgf`,
    targetDepth: pos(c.target_depth_mm),
    targetBpm: pos(c.target_bpm),
    forceMax: pos(c.force_max_N),
  }
}

/** 압박 횟수 추정: 받은 압박 속도(회/분)를 시간에 대해 더한다. 펌웨어가 횟수를 보내지 않아 '약'으로 표시한다.
    값이 없거나 2초 넘게 끊긴 구간은 더하지 않는다 */
export function countStep(count, rate, dtS) {
  if (!num(rate) || rate <= 0 || !(dtS > 0) || dtS > 2) return count
  return count + (rate * dtS) / 60
}

/* ---------- 흡착 ---------- */
export function vacuumView(tel, stale, cfg) {
  const t = tel && !stale ? tel : {}
  const c = cfg || {}
  const kPa = num(t.vac_kPa) ? t.vac_kPa : null
  const target = num(c.vacuum_target_kPa) && c.vacuum_target_kPa < 0 ? c.vacuum_target_kPa : null
  return {
    kPa,
    kPaText: kPa === null ? NA : `${kPa.toFixed(1)} kPa`,
    target,
    targetText: target === null ? '미설정' : `${target.toFixed(1)} kPa`,
    holding: ['VACUUM_HOLD', 'CPR_READY', 'CPR_RUNNING', 'CPR_PAUSED'].includes(t.state),
    leak: t.vac_leak === true,
    leakText: t.vac_leak === true ? '누설 감지' : t.vac_leak === false ? '없음' : NA,
  }
}

/** 파형을 분당 rate 회로 흐르게 하는 애니메이션 시간.
    vitals.js 의 경로는 2400 단위이고 화면에 1200 단위가 보이며 dur 초 동안 1200 단위 흐른다(index.css scrollwave).
    주기 period 단위 파형이 분당 rate 회 지나가려면 dur = (1200 / period) × 60 / rate. 값이 없으면 null(정지) */
export function waveDur(rate, period) {
  if (!num(rate) || rate <= 0) return null
  return `${(((1200 / period) * 60) / rate).toFixed(2)}s`
}

/** 경과 시간 m:ss */
export function clock(sec) {
  const s = Math.max(0, Math.floor(sec))
  return `${Math.floor(s / 60)}:${String(s % 60).padStart(2, '0')}`
}
