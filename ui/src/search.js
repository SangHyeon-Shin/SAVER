/* ============================================================
   압박점 탐색 화면의 계산과 표시 (순수 함수, 스모크 시험에서 직접 검사)

   판정 규칙은 jetson/saver/search.py 와 같다(제출 보고서 6.2).
     말단 판정, 식 (5)  r_i = 1 - F_i / Fref_i >= tip_drop_ratio,  Fref_i = 직전 N 점 평균
     압박점 거리, 식 (7) D = min(cp_dist_max_mm, cp_dist_ref_mm × EAPD / eapd_ref_mm)
   브리지에 연결돼 있으면 UI 는 계산하지 않고 받은 값만 그린다.
   여기 detectTip·cpDistance 는 미연결 시연(searchDemo.js)에서만 쓴다.
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

export function cpDistance(eapd, p) {
  if (!num(eapd) || eapd <= 0) return null
  return Math.min(p.cp_dist_max_mm, (p.cp_dist_ref_mm * eapd) / p.eapd_ref_mm)
}

/* ---------- 진행 단계 (search.py 의 phase) ---------- */
export const STEPS = [
  { phase: 'home0', label: '헤드 원위치' },
  { phase: 'hold',  label: '접촉 뒤 하강 고정' },
  { phase: 'scan',  label: '주행하며 반력 측정' },
  { phase: 'home1', label: '판정 · 헤드 올림' },
  { phase: 'move',  label: '압박점으로 이동' },
  { phase: 'done',  label: '압박점 도달' },
]
const ACTIVE = ['home0', 'hold', 'scan', 'home1', 'move', 'stopping']

/* ---------- 실패 사유 → 안내 문구 ---------- */
const REASON = {
  EAPD_MISSING: ['EAPD(흉곽 전후 직경) 측정값이 없습니다',
    '레이더로 다시 측정하세요. 대체값으로 압박점을 정하지 않습니다.'],
  PARAM_UNSET: ['탐색 설정값이 비어 있습니다',
    '설정이 확정되기 전에는 탐색하지 않습니다.'],
  UNSUPPORTED: ['장치가 탐색 명령을 지원하지 않습니다',
    '프로토콜 v1.1 펌웨어가 필요합니다.'],
  TIP_NOT_FOUND: ['반력이 급감하는 지점을 찾지 못했습니다',
    '환자 정렬과 시작 위치를 확인한 뒤 다시 탐색하세요.'],
  FORCE_SAMPLES_SHORT: ['반력 측정값이 부족합니다', '로드셀 상태를 확인한 뒤 다시 탐색하세요.'],
  POSITION_UNKNOWN: ['주행 위치를 알 수 없습니다', '엔코더 상태를 확인하세요.'],
  REJECTED: ['장치가 동작 요청을 거부했습니다', '장치 상태를 확인하세요.'],
  MOT_STALE: ['장치 동작 정보가 끊겼습니다', '연결 상태를 확인하세요.'],
  NO_STATUS: ['장치 응답이 없습니다', '연결 상태를 확인하세요.'],
  EVENT_LOST: ['장치 완료 보고를 받지 못했습니다', '다시 탐색하세요.'],
  REBOOTED: ['장치가 다시 시작되었습니다', '처음부터 다시 탐색하세요.'],
  STATE_MISMATCH: ['장치 상태가 요청과 맞지 않습니다', '장치 상태를 확인하세요.'],
  TIMEOUT: ['동작이 제한 시간 안에 끝나지 않았습니다', '장치를 확인한 뒤 다시 탐색하세요.'],
  MOVE_NOT_CONVERGED: ['압박점에 도달하지 못했습니다', '주행 상태를 확인하세요.'],
  NOT_SENT: ['명령을 보내지 못했습니다', '연결 상태를 확인하세요.'],
  USER_STOP: ['탐색을 중지했습니다', '다시 시작하려면 탐색 시작을 누르세요.'],
  LEFT_NODE: ['탐색이 중단되었습니다', ''],
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
