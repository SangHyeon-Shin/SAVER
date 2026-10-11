import { useCallback, useEffect, useRef, useState } from 'react'
import { detectTip, cp_distance } from './search.js'

/* ============================================================
   압박점 탐색 시연 (브리지 미연결 전용)

   브리지 search 메시지와 같은 모양의 진행값을 만든다. 화면에는 '모의 데이터'로 표시한다.
   ★ 모든 수치는 시뮬레이션 값이며 의학적·공학적 근거가 없다.
     DEMO 는 jetson/config.sim.json, PHANTOM 은 simulators/fake_teensy_v11.py 의 Phantom 과
     같은 값이다(스모크 시험에서 대조). 실기에는 브리지가 보낸 값만 쓴다.
   ============================================================ */
export const DEMO = {
  press_mm: 10.0, cp_dist_ref_mm: 89.7, eapd_ref_mm: 205.8, cp_dist_max_mm: 95,
  tip_drop_ratio: 0.3, tip_ref_n: 3, scan_step_mm: 5.0, scan_roi_mm: 120.0, scan_dir_sign: 1,
  move_step_mm: 20.0, move_tol_mm: 0.5, sim_eapd_mm: 205.8,
}
export const PHANTOM = { surface_mm: 35.0, tip_mm: 60.0, k_hard: 4.0, k_soft: 1.2, width_mm: 8.0, noise_N: 0.4 }

// 화면에서 보기 좋게 고른 시연 속도(ms). 실제 동작 시간이 아니다.
const WAIT = { home0: 700, hold: 1300, scan: 260, home1: 900, move: 380, done: 1600 }

function rng32(seed) {                       // 재현 가능한 잡음 (mulberry32)
  let a = seed >>> 0
  return () => {
    a = (a + 0x6D2B79F5) >>> 0
    let t = a
    t = Math.imul(t ^ (t >>> 15), t | 1)
    t ^= t + Math.imul(t ^ (t >>> 7), t | 61)
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296
  }
}

const r2 = (v) => Math.round(v * 100) / 100

/** 시연 진행을 한 단계씩 내는 생성기. { snap, wait } 를 낸다. */
export function* demoSearch({ eapd, flat = false, seed = 7 }) {
  const p = DEMO
  const rnd = rng32(seed)
  const gauss = () => {
    let u = 0
    while (u === 0) u = rnd()
    return Math.sqrt(-2 * Math.log(u)) * Math.cos(2 * Math.PI * rnd())
  }
  const k = (x) => {
    if (flat) return PHANTOM.k_hard
    const z = Math.max(-40, Math.min(40, (PHANTOM.tip_mm - x) / (PHANTOM.width_mm / 4)))
    return PHANTOM.k_soft + (PHANTOM.k_hard - PHANTOM.k_soft) / (1 + Math.exp(-z))
  }
  const force = (x) => r2(k(x) * p.press_mm + gauss() * PHANTOM.noise_N)
  const rule = { drop_ratio: p.tip_drop_ratio, ref_n: p.tip_ref_n, press_mm: p.press_mm,
                 dir: p.scan_dir_sign, roi_mm: p.scan_roi_mm, step_mm: p.scan_step_mm }
  const base = { simulated: true, rule, reason: null, detail: null, eapd_mm: null, D_mm: null, points: [],
                 tip_x: null, candidates_x: [], candidate_groups: 0, target_x: null, pos_x: null,
                 head: {}, moved: false }

  const D = cp_distance(eapd, p)
  if (D === null) {                          // 대체 상수 없이 시작하지 않는다(초안 §0.1-3)
    yield { snap: { ...base, phase: 'failed', reason: 'EAPD_MISSING' }, wait: 0 }
    return
  }
  let s = { ...base, eapd_mm: eapd, D_mm: r2(D), moved: true }
  yield { snap: { ...s, phase: 'home0' }, wait: WAIT.home0 }
  yield { snap: { ...s, phase: 'hold' }, wait: WAIT.hold }
  s.head = { head_contact_mm: PHANTOM.surface_mm, head_pos_mm: PHANTOM.surface_mm + p.press_mm, cp_force_N: force(0) }

  const pts = []
  let det = null
  let x = 0
  for (let cmd = p.scan_step_mm; cmd <= p.scan_roi_mm + 1e-9; cmd += p.scan_step_mm) {
    x = r2(x + p.scan_dir_sign * p.scan_step_mm)
    pts.push({ x, F: force(x) })
    det = detectTip(pts.map((q) => q.x), pts.map((q) => q.F), p.tip_ref_n, p.tip_drop_ratio)
    s = {
      ...s, pos_x: x, tip_x: det.tipX, candidate_groups: det.groups,
      candidates_x: det.candidates.map((i) => pts[i].x),
      points: pts.map((q, i) => ({ ...q, ref: det.ref[i], r: det.r[i] })),
    }
    yield { snap: { ...s, phase: 'scan' }, wait: WAIT.scan }
  }

  if (det.tipIndex !== null) s = { ...s, target_x: r2(det.tipX - p.scan_dir_sign * D) }
  yield { snap: { ...s, phase: 'home1' }, wait: WAIT.home1 }
  if (det.tipIndex === null) {
    yield { snap: { ...s, phase: 'failed', reason: 'TIP_NOT_FOUND' }, wait: 0 }
    return
  }
  let pos = x
  for (let i = 0; i < 20 && Math.abs(s.target_x - pos) > p.move_tol_mm; i++) {
    const rem = s.target_x - pos
    pos = r2(pos + Math.max(-p.move_step_mm, Math.min(p.move_step_mm, rem)))
    s = { ...s, pos_x: pos }
    yield { snap: { ...s, phase: 'move' }, wait: WAIT.move }
  }
  yield { snap: { ...s, phase: 'done' }, wait: WAIT.done }
}

/**
 * 시연 실행 훅. start() 로 시작, stop(reason) 으로 중지.
 * 끝나면 onDone(ok, snap). speed 는 시연 배속(스모크 시험 가속용).
 */
export function useSearchDemo({ eapd, flat, speed = 1, onDone }) {
  const [snap, setSnap] = useState(null)
  const gen = useRef(null)
  const timer = useRef(null)
  const last = useRef(null)
  const cb = useRef(onDone); cb.current = onDone

  const halt = () => { clearTimeout(timer.current); gen.current = null }

  const step = useCallback(() => {
    const g = gen.current
    if (!g) return
    const it = g.next()
    if (it.done) {
      gen.current = null
      const s = last.current
      cb.current && cb.current(s && s.phase === 'done', s)
      return
    }
    last.current = it.value.snap
    setSnap(it.value.snap)
    if (it.value.snap.phase === 'failed') {
      gen.current = null
      cb.current && cb.current(false, it.value.snap)
      return
    }
    timer.current = setTimeout(step, it.value.wait / Math.max(speed, 0.1))
  }, [speed])

  const start = useCallback(() => {
    if (gen.current) return
    gen.current = demoSearch({ eapd, flat, seed: 7 })
    step()
  }, [eapd, flat, step])

  const stop = useCallback((reason = 'USER_STOP') => {
    if (!gen.current) return
    halt()
    const s = { ...(last.current || {}), phase: 'failed', reason }
    last.current = s
    setSnap(s)
    cb.current && cb.current(false, s)
  }, [])

  const reset = useCallback(() => { halt(); last.current = null; setSnap(null) }, [])

  useEffect(() => () => clearTimeout(timer.current), [])

  return { snap, start, stop, reset, running: () => !!gen.current }
}
