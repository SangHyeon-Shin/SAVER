import { useCallback, useEffect, useRef, useState } from 'react'

/* ============================================================
   결선 시연 메뉴의 모의 장치 (브리지 미연결이나 개발 패널의 '모의 강제' 전용, D-51)

   브리지 텔레메트리, 레이더 메시지와 같은 모양의 값을 만든다. 화면에는 '모의 데이터'로 표시한다.
   ★ 화면 확인용 시뮬레이션 값이며 실측이 아니고 의학적 근거가 없다.
     목표값은 브리지 설정에 있으면 그 값을, 없으면 SIM 을 흉내 낸다. 실기에는 브리지가 보낸 값만 쓴다.
   ============================================================ */
export const SIM = { bpm: 110, depth_mm: 50, vac_kPa: -22.5, rr: 14, hr: 72, spo2: 97 }
/* 모의 흉부(jetson/simulators/fake_teensy.py 와 같은 모델): 강성 10 N/mm, 점성 0.3 N·s/mm.
   변위 x = D(1 - cos wt)/2 로 누를 때 한 주기 최대 힘. 모의 압박력과 모의 강성 측정에 쓴다 */
export const SIM_CHEST = { k_N_mm: 10, c_Ns_mm: 0.3 }
export function chestPeakForce(depth, bpm) {
  if (!(depth > 0)) return 0
  const a = (SIM_CHEST.k_N_mm * depth) / 2
  const b = SIM_CHEST.c_Ns_mm * depth * Math.PI * (bpm / 60)
  return a + Math.sqrt(a * a + b * b)
}
/* 모의 강성 측정 창(브리지 unit_demo.py 의 STIFF_SETTLE, STIFF_CYCLES 와 같은 값) */
export const SIM_STIFF = { settle: 2, cycles: 10 }
const OFF = { cpr: false, vac: false, breath: false, finger: false, stiff: false }
const r1 = (v) => Math.round(v * 10) / 10
const r2 = (v) => Math.round(v * 100) / 100

/** 모의 강성 측정 결과(브리지가 보내는 stiff 와 같은 모양) */
export function simStiffResult(pts, done) {
  const out = { of: SIM_STIFF.cycles, done }
  if (!pts.length) return out
  const ks = pts.map(([d, f]) => f / d)
  const n = ks.length
  const k = ks.reduce((a, b) => a + b, 0) / n
  const sd = n > 1 ? Math.sqrt(ks.reduce((a, x) => a + (x - k) ** 2, 0) / (n - 1)) : 0
  return { ...out, n, k_N_mm: r2(k), k_sd: r2(sd), f_N: r1(pts.reduce((a, p) => a + p[1], 0) / n),
    d_mm: r2(pts.reduce((a, p) => a + p[0], 0) / n), pts }
}

export function useSimDevice({ on, cfg, speed = 1 }) {
  const [st, setSt] = useState(OFF)
  const stRef = useRef(st); stRef.current = st
  const cfgRef = useRef(cfg); cfgRef.current = cfg
  const kPa = useRef(0)
  const t0 = useRef(Date.now())
  const run = useRef(null)                 // 모의 강성 측정 { t0, pts }
  const [tel, setTel] = useState(null)
  const [radar, setRadar] = useState(null)
  const [stiff, setStiff] = useState({ phase: 'idle', res: null })

  useEffect(() => {
    if (!on) { setTel(null); setRadar(null); return undefined }
    const tickSim = () => {
      const s = stRef.current
      const c = cfgRef.current || {}
      const t = (Date.now() - t0.current) / 1000
      const target = c.vacuum_target_kPa < 0 ? c.vacuum_target_kPa : SIM.vac_kPa
      const goal = s.vac ? target - 0.6 : 0                 // 모의: 목표보다 조금 낮게 유지
      kPa.current += (goal - kPa.current) * 0.15
      const holding = s.vac && Math.abs(kPa.current - goal) < 1.5
      const cpr = s.cpr || s.stiff
      // 강성 측정은 전용 목표(stiff_*)가 있으면 그것, 없으면 압박 목표(브리지와 같은 규칙)
      const tBpm = (s.stiff && c.stiff_bpm) || c.target_bpm || SIM.bpm
      const tDepth = (s.stiff && c.stiff_depth_mm) || c.target_depth_mm || SIM.depth_mm
      const m = { t: 'tel', state: cpr ? 'CPR_RUNNING' : holding ? 'VACUUM_HOLD' : 'HOMED',
                  vac_kPa: r1(kPa.current), vac_leak: false }
      if (cpr) {
        const bpm = tBpm
        const depth = tDepth + Math.sin(t * 1.9) * 1.4
        m.cp_rate_cpm = r1(bpm + Math.sin(t * 0.7) * 1.2)
        m.cp_depth_mm = r1(depth)
        m.cp_force_N = Math.round(chestPeakForce(depth, bpm) * (1 + Math.sin(t * 1.3) * 0.01))
      }
      if (s.stiff && run.current) {
        // 모의 강성 측정: speed 배로 시간을 당긴다(스모크 시험). 끝난 주기마다 (깊이, 힘) 한 점
        const bpm = tBpm
        const T = 60 / bpm
        const el = ((Date.now() - run.current.t0) / 1000) * speed
        const done = Math.max(0, Math.min(SIM_STIFF.cycles, Math.floor(el / T) - SIM_STIFF.settle))
        while (run.current.pts.length < done) {
          const tc = (SIM_STIFF.settle + run.current.pts.length + 1) * T
          const dc = tDepth + Math.sin(tc * 1.9) * 1.4
          run.current.pts.push([r2(dc), r1(chestPeakForce(dc, bpm) * (1 + Math.sin(tc * 1.3) * 0.01))])
        }
        const res = simStiffResult(run.current.pts.slice(), done)
        if (done >= SIM_STIFF.cycles) {
          run.current = null
          setSt((x) => ({ ...x, stiff: false }))
          setStiff({ phase: 'done', res })
        } else setStiff({ phase: 'running', res })
      }
      if (s.finger) {
        m.ppg_hr_bpm = r1(SIM.hr + Math.sin(t * 0.3) * 3)
        m.spo2_pct = Math.round(SIM.spo2 + Math.sin(t * 0.11))
      }
      setTel(m)
      setRadar({ type: 'radar', enabled: true, connected: true, receiving: true,
                 ...(s.breath ? { rr_bpm: Math.round(SIM.rr + Math.sin(t * 0.2)) } : {}) })
    }
    tickSim()
    const iv = setInterval(tickSim, 200)
    return () => clearInterval(iv)
  }, [on])

  const set = useCallback((k, v) => {
    if (k === 'stiff') {
      if (v) { run.current = { t0: Date.now(), pts: [] }; setStiff({ phase: 'running', res: simStiffResult([], 0) }) }
      else if (run.current) {
        // 도중 정지: 끝난 주기까지의 부분 결과
        setStiff({ phase: 'stopped', res: simStiffResult(run.current.pts.slice(), run.current.pts.length) })
        run.current = null
      }
    }
    setSt((x) => ({ ...x, [k]: v }))
  }, [])
  const reset = useCallback(() => { setSt(OFF); kPa.current = 0; run.current = null; setStiff({ phase: 'idle', res: null }) }, [])
  return { tel, radar, st, set, reset, stiff }
}
