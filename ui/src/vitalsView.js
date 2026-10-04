/* ============================================================
   압박 수행 화면의 실시간 수치 (순수 함수, 스모크 시험에서 직접 검사)

   - 브리지 미연결(telemetry == null): 오프라인 시연용 모의값
   - 브리지 연결: 받은 필드만 표시한다. 필드가 생략됐거나(측정 불가,
     프로토콜 v1 §4.3) 텔레메트리가 오래됐으면(stale) '—'로 둔다.
     모의값이나 이전 값으로 채우지 않는다.
   ============================================================ */
export const NA = '—'

const num = (v) => typeof v === 'number' && Number.isFinite(v)

export function vitalsView(sec, telemetry, stale) {
  const mm = Math.floor(sec / 60)
  const ss = sec % 60
  const elapsed = `${mm}:${String(ss).padStart(2, '0')}`

  if (telemetry == null) {
    // 시뮬레이션 값 (원본 renderVals 이식)
    const rate  = 109 + (sec % 4 === 0 ? 2 : sec % 2 === 0 ? 1 : 0)
    const depth = (5.1 + 0.25 * Math.abs(Math.sin(sec * 1.1))).toFixed(1)
    const force = (38 + (sec % 5)).toFixed(0)
    const count = Math.floor((sec * 110) / 60)
    return {
      depth: depth + ' cm', rate: rate + ' /min', force: '약 ' + force + ' kg',
      elapsed, count: count + '회', live: false,
    }
  }

  const t = stale ? {} : telemetry
  return {
    depth: num(t.cp_depth_mm) ? (t.cp_depth_mm / 10).toFixed(1) + ' cm' : NA,
    rate:  num(t.cp_rate_cpm) ? t.cp_rate_cpm + ' /min' : NA,
    force: num(t.cp_force_N)  ? '약 ' + (t.cp_force_N / 9.81).toFixed(0) + ' kg' : NA,
    elapsed,
    count: NA,      // 텔레메트리에 압박 횟수 필드가 없다. 경과 시간으로 지어내지 않는다
    live: true,
  }
}
