import { useEffect, useState, useMemo } from 'react'
import { vitalsView } from './vitalsView.js'

/* ============================================================
   파형 · 실시간 수치
   디자인 원본 <script data-dc-script>의 Component 클래스를 React로 옮긴 것.
   실제 장비 연결 시에는 bridge.js의 telemetry 값만 쓴다(vitalsView.js).
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
 * @param telemetry 브리지에서 온 실측값. null 이면 오프라인 시연(모의값)
 * @param stale 텔레메트리가 오래됐거나 링크가 끊김 → 실측 칸을 비운다
 */
export function useVitals(running, telemetry, stale) {
  const [sec, setSec] = useState(0)

  useEffect(() => {
    if (!running) return
    const iv = setInterval(() => setSec((s) => s + 1), 1000)
    return () => clearInterval(iv)
  }, [running])

  return { sec, reset: () => setSec(0), ...vitalsView(sec, telemetry, stale) }
}
