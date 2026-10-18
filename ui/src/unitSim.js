import { useCallback, useEffect, useRef, useState } from 'react'

/* ============================================================
   결선 시연 메뉴의 모의 장치 (브리지 미연결이나 개발 패널의 '모의 강제' 전용, D-51)

   브리지 텔레메트리, 레이더 메시지와 같은 모양의 값을 만든다. 화면에는 '모의 데이터'로 표시한다.
   ★ 화면 확인용 시뮬레이션 값이며 실측이 아니고 의학적 근거가 없다.
     목표값은 브리지 설정에 있으면 그 값을, 없으면 SIM 을 흉내 낸다. 실기에는 브리지가 보낸 값만 쓴다.
   ============================================================ */
export const SIM = { bpm: 110, depth_mm: 50, force_N: 380, vac_kPa: -22.5, rr: 14, hr: 72, spo2: 97 }
const OFF = { cpr: false, vac: false, breath: false, finger: false }
const r1 = (v) => Math.round(v * 10) / 10

export function useSimDevice({ on, cfg }) {
  const [st, setSt] = useState(OFF)
  const stRef = useRef(st); stRef.current = st
  const cfgRef = useRef(cfg); cfgRef.current = cfg
  const kPa = useRef(0)
  const t0 = useRef(Date.now())
  const [tel, setTel] = useState(null)
  const [radar, setRadar] = useState(null)

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
      const m = { t: 'tel', state: s.cpr ? 'CPR_RUNNING' : holding ? 'VACUUM_HOLD' : 'HOMED',
                  vac_kPa: r1(kPa.current), vac_leak: false }
      if (s.cpr) {
        m.cp_rate_cpm = r1((c.target_bpm || SIM.bpm) + Math.sin(t * 0.7) * 1.2)
        m.cp_depth_mm = r1((c.target_depth_mm || SIM.depth_mm) + Math.sin(t * 1.9) * 1.4)
        m.cp_force_N = Math.round(SIM.force_N + Math.sin(t * 1.9) * 45)
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

  const set = useCallback((k, v) => setSt((x) => ({ ...x, [k]: v })), [])
  const reset = useCallback(() => { setSt(OFF); kPa.current = 0 }, [])
  return { tel, radar, st, set, reset }
}
