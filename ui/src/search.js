/* ============================================================
   압박점 탐색 화면의 계산과 표시 (순수 함수, 스모크 시험에서 직접 검사)

   판정 규칙은 jetson/saver/search.py 와 같다(제출 보고서 6.2).
     말단 판정, 식 (5)  r_i = 1 - F_i / Fref_i >= tip_drop_ratio,  Fref_i = 직전 N 점 평균
     압박점 거리, 식 (7) D = min(cp_dist_max_mm, cp_dist_ref_mm × EAPD / eapd_ref_mm)
   브리지에 연결돼 있으면 UI 는 계산하지 않고 받은 값만 그린다.
   여기 detectTip·cp_distance 는 미연결 시연(searchDemo.js)에서만 쓴다.
   판정과 D 는 설계 가설이며 인체에서 검증되지 않았다.
   ============================================================ */

const num = (v) => typeof v === 'number' && Number.isFinite(v)

export function detectTip(xs, fs, n, ratio) {
  const ref = [], r = [], cand = []
  for (let i = 0; i < fs.length; i++) {
    if (i < n) { ref.push(null); r.push(null); continue }
    let m = 0
    for (let j = i - n; j < i; j++) m += fs[j]
    m /= n
    if (m <= 0) { ref.push(m); r.push(null); continue }
    const ri = 1 - fs[i] / m
    ref.push(m); r.push(ri)
    if (ri >= ratio) cand.push(i)
  }
  const groups = cand.filter((i, k) => k === 0 || cand[k - 1] !== i - 1).length
  const tip = cand.length ? cand[0] : null
  return { tipIndex: tip, tipX: tip === null ? null : xs[tip], ref, r, candidates: cand, groups }
}

// 이름은 jetson/saver/search.py 의 cp_distance 와 같게 둔다
export function cp_distance(eapd, p) {
  if (!num(eapd) || eapd <= 0) return null
  return Math.min(p.cp_dist_max_mm, (p.cp_dist_ref_mm * eapd) / p.eapd_ref_mm)
}

/* ---------- 진행 단계 (search.py 의 phase) ---------- */
export const STEPS = [
  { phase: 'home0', label: '누르는 부분 제자리로' },
  { phase: 'hold',  label: '가슴에 대고 누르기' },
  { phase: 'scan',  label: '이동하며 힘 재기' },
  { phase: 'home1', label: '끝 판단 · 누르는 부분 올림' },
  { phase: 'move',  label: '압박 위치로 이동' },
  { phase: 'done',  label: '압박 위치 도착' },
]
const ACTIVE = ['home0', 'hold', 'scan', 'home1', 'move', 'stopping']

/* ---------- 실패 사유 → 안내 문구 ---------- */
const REASON = {
  // 화면 문구는 일반인 기준 쉬운 말로 쓴다. 코드는 S10 화면에 괄호로 함께 나온다
  EAPD_MISSING: ['가슴 두께를 재지 못했습니다',
    '레이더로 다시 재야 합니다. 짐작한 값으로 압박 위치를 정하지 않습니다.'],
  PARAM_UNSET: ['위치 찾기 설정값이 비어 있습니다',
    '설정이 정해지기 전에는 위치를 찾지 않습니다.'],
  FW_VERSION: ['장치 프로그램 판이 맞지 않습니다',
    '장치 프로그램(펌웨어)을 v1.1로 올려야 합니다.'],
  UNSUPPORTED: ['장치가 위치 찾기를 지원하지 않습니다',
    '장치 프로그램에 주행·헤드 기능이 아직 없습니다.'],
  TIP_NOT_FOUND: ['가슴뼈 끝을 찾지 못했습니다',
    '환자 몸 위치와 시작 위치를 확인한 뒤 다시 찾으세요.'],
  FORCE_SAMPLES_SHORT: ['누르는 힘을 충분히 재지 못했습니다', '힘 센서를 확인한 뒤 다시 찾으세요.'],
  POSITION_UNKNOWN: ['장치 위치를 알 수 없습니다', '바퀴 센서를 확인하세요.'],
  REJECTED: ['장치가 동작 요청을 거부했습니다', '장치 상태를 확인하세요.'],
  MOT_STALE: ['장치 동작 정보가 끊겼습니다', '연결 상태를 확인하세요.'],
  NO_STATUS: ['장치 응답이 없습니다', '연결 상태를 확인하세요.'],
  EVENT_LOST: ['장치에서 완료 신호를 받지 못했습니다', '다시 찾으세요.'],
  REBOOTED: ['장치가 다시 시작되었습니다', '처음부터 다시 찾으세요.'],
  STATE_MISMATCH: ['장치 상태가 요청과 맞지 않습니다', '장치 상태를 확인하세요.'],
  TIMEOUT: ['동작이 제한 시간 안에 끝나지 않았습니다', '장치를 확인한 뒤 다시 찾으세요.'],
  MOVE_NOT_CONVERGED: ['압박 위치에 도착하지 못했습니다', '장치 이동 상태를 확인하세요.'],
  NOT_SENT: ['명령을 보내지 못했습니다', '연결 상태를 확인하세요.'],
  USER_STOP: ['위치 찾기를 멈췄습니다', '다시 하려면 찾기 시작을 누르세요.'],
  LEFT_NODE: ['위치 찾기가 중단되었습니다', ''],
  // 결선 시연 메뉴의 구동 화면: 탐색을 시작하기 전에 브리지가 멈춘 경우(jetson unit_demo.py, D-51)
  NOT_CONNECTED: ['장치가 연결되지 않았습니다', '장치(Teensy) 연결을 확인하세요.'],
  LINK_CHECKING: ['장치를 확인하고 있습니다', '잠시 뒤 다시 찾기 시작을 누르세요.'],
  LINK_DOWN: ['장치 연결이 끊겼습니다', '연결 상태를 확인하세요.'],
  ESTOP: ['비상정지가 눌려 있습니다', '비상정지를 푼 뒤 다시 찾으세요.'],
  FAULT: ['장치 고장 상태입니다', '장치 상태를 확인하세요.'],
}

export function reasonText(reason) {
  if (!reason) return null
  const hit = REASON[reason]
  if (hit) return { title: hit[0], sub: hit[1], code: reason }
  // HEAD_HELD_FAULT, DRIVE_DONE_ABORTED 처럼 장치 결과로 끝난 경우
  return { title: '장치가 동작을 중단했습니다', sub: '장치 상태를 확인하세요.', code: reason }
}

const niceMax = (v) => {
  if (!(v > 0)) return 10
  const step = v <= 20 ? 5 : v <= 60 ? 10 : v <= 150 ? 25 : 50
  return Math.ceil((v * 1.15) / step) * step
}

/**
 * 탐색 진행 메시지(브리지 search 또는 시연) → 화면 표시값.
 * 받은 값만 쓴다. 없는 값은 null 로 두고 화면은 '—' 로 표시한다.
 */
export function searchView(snap) {
  const s = snap || {}
  const rule = s.rule || null
  const dir = rule && (rule.dir === -1 || rule.dir === 1) ? rule.dir : 1
  const ratio = rule && num(rule.drop_ratio) ? rule.drop_ratio : null
  const toU = (x) => (num(x) ? dir * x : null)          // 머리 → 발 방향 거리(화면 왼쪽 = 머리)
  const cands = new Set((s.candidates_x || []).filter(num))

  const pts = (s.points || []).filter((p) => num(p.x) && num(p.F)).map((p) => ({
    x: p.x, u: dir * p.x, F: p.F,
    ref: num(p.ref) ? p.ref : null,
    thr: num(p.ref) && ratio !== null ? p.ref * (1 - ratio) : null,
    r: num(p.r) ? p.r : null,
    cand: cands.has(p.x),
  }))

  const phase = s.phase || 'idle'
  const tipU = toU(s.tip_x), targetU = toU(s.target_x), posU = toU(s.pos_x)
  const roi = rule && num(rule.roi_mm) ? rule.roi_mm : null

  const us = [0, ...pts.map((p) => p.u)]
  if (roi !== null) us.push(roi)
  if (targetU !== null) us.push(targetU)
  if (posU !== null) us.push(posU)
  const u0 = Math.floor((Math.min(...us) - 8) / 10) * 10
  const u1 = Math.ceil((Math.max(...us, roi === null ? 120 : roi) + 6) / 10) * 10
  const ys = pts.flatMap((p) => [p.F, p.ref || 0])

  const stepIdx = STEPS.findIndex((x) => x.phase === phase)
  return {
    phase,
    simulated: !!s.simulated,
    running: ACTIVE.includes(phase),
    done: phase === 'done',
    failed: phase === 'failed',
    stepIdx,
    reason: phase === 'failed' ? reasonText(s.reason) : null,
    moved: !!s.moved,
    rule, ratio, dir, roi,
    n: rule && num(rule.ref_n) ? rule.ref_n : null,
    scanTotal: rule && num(rule.roi_mm) && num(rule.step_mm) ? Math.floor(rule.roi_mm / rule.step_mm + 1e-9) : null,
    pts,
    tipU, tipFinal: ['home1', 'move', 'done'].includes(phase) || (phase === 'failed' && targetU !== null),
    targetU, posU,
    tipX: num(s.tip_x) ? s.tip_x : null, targetX: num(s.target_x) ? s.target_x : null,
    posX: num(s.pos_x) ? s.pos_x : null,
    D: num(s.D_mm) ? s.D_mm : null,
    eapd: num(s.eapd_mm) ? s.eapd_mm : null,
    eapdSim: !!(s.simulated || s.eapd_sim),      // 레이더가 아닌 모의 EAPD
    encMode: num(s.enc_mode) ? s.enc_mode : null, // 1 = 단일 엔코더 + 자이로 임시 모드(초안 §4.7)
    groups: num(s.candidate_groups) ? s.candidate_groups : 0,
    domain: { u0, u1, y1: niceMax(Math.max(0, ...ys)) },
  }
}

/** mm 표시. 값이 없으면 '—' */
export const mm = (v, digits = 1) => (num(v) ? `${v.toFixed(digits)} mm` : '—')
