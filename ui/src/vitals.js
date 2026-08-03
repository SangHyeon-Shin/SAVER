import { useEffect, useState, useMemo } from 'react'

/* ============================================================
   파형 · 실시간 수치
   디자인 원본 <script data-dc-script>의 Component 클래스를 React로 옮긴 것.
   실제 장비 연결 시에는 bridge.js의 telemetry 값이 이 값을 덮어씁니다.
   ============================================================ */

/** SVG path 문자열 생성 (원본 _wave 이식) */
export function makeWave(type, w = 2400, mid = 60, amp = 40, period = 150) {
  let d = 'M 0 ' + mid
  for (let x = 2; x <= w; x += 2) {
    const t = (x % period) / period
    let y = mid
    if (type === 'comp') y = mid - amp * Math.sin(t * 2 * Math.PI)
    else if (type === 'ppg')
      y = mid - amp * (Math.exp(-Math.pow((t - 0.18) / 0.07, 2)) + 0.35 * Math.exp(-Math.pow((t - 0.42) / 0.13, 2)))
    else if (type === 'breath') y = mid - amp * Math.sin(t * 2 * Math.PI)
    d += ' L ' + x + ' ' + y.toFixed(1)
  }
  return d
}

/** 파형은 고정 경로 + CSS 애니메이션으로 흐르게 하므로 1회만 계산 */
export function useWaves() {
  return useMemo(() => ({
    comp:   makeWave('comp',   2400, 60, 40, 150),
    ppg:    makeWave('ppg',    2400, 62, 42, 150),
    breath: makeWave('breath', 2400, 60, 42, 600),
    ppg2:   makeWave('ppg',    2400, 60, 38, 240),
  }), [])
}

/**
 * 압박 수행 중 실시간 수치.
 * @param running S7(압박 수행) 상태일 때만 true → 경과시간/횟수 누적
 * @param telemetry 브리지에서 온 실측값(있으면 우선 사용)
 */
export function useVitals(running, telemetry) {
  const [sec, setSec] = useState(0)

  useEffect(() => {
    if (!running) return
    const iv = setInterval(() => setSec((s) => s + 1), 1000)
    return () => clearInterval(iv)
  }, [running])

  // 시뮬레이션 값 (원본 renderVals 이식)
  const rate  = 109 + (sec % 4 === 0 ? 2 : sec % 2 === 0 ? 1 : 0)
  const depth = (5.1 + 0.25 * Math.abs(Math.sin(sec * 1.1))).toFixed(1)
  const force = (38 + (sec % 5)).toFixed(0)
  const count = Math.floor((sec * 110) / 60)
  const mm = Math.floor(sec / 60)
  const ss = sec % 60

  const t = telemetry || {}
  return {
    sec,
    reset: () => setSec(0),
    depth:   t.cp_depth_mm != null ? (t.cp_depth_mm / 10).toFixed(1) + ' cm' : depth + ' cm',
    rate:    t.cp_rate_cpm != null ? t.cp_rate_cpm + ' /min' : rate + ' /min',
    force:   t.cp_force_N  != null ? '약 ' + (t.cp_force_N / 9.81).toFixed(0) + ' kg' : '약 ' + force + ' kg',
    elapsed: `${mm}:${String(ss).padStart(2, '0')}`,
    count:   count + '회',
    live:    telemetry != null,
  }
}
