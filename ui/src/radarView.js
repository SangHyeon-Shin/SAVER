/* ============================================================
   레이더(MR60BHA2) 표시 (순수 함수, 스모크 시험에서 직접 검사)

   - 브리지가 보낸 radar 메시지만 쓴다(jetson/saver/radar.py). 키가 없으면
     (측정 못 함, 오래됨) '—'. 모의값이나 이전 값으로 채우지 않는다.
   - 호흡수와 심박수는 압박이 멈춘 화면(S8 일시정지, S9 회복 신호 확인)에서
     장치가 멈춤을 보고한 뒤(hold)에만 보인다. 압박 중에는 가슴이 기계로 움직여
     레이더 추정이 압박 동작을 따라가므로 보이지 않는다(D-50).
   - 표시용이다. 회복 판정 같은 판단에 쓰지 않는다.
   - 가슴 두께(EAPD)는 탐색 화면용이며 hold 와 관계없다.
   ============================================================ */
export const NA = '—'

const num = (v) => typeof v === 'number' && Number.isFinite(v)

export function radarView(radar, { hold = true } = {}) {
  const r = radar || {}
  const enabled = !!r.enabled
  const live = enabled && !!r.connected && !!r.receiving
  const show = live && hold
  const per = (v) => (show && num(v) && v > 0 ? `${Math.round(v)} 회/분` : NA)

  let status
  if (!radar) status = '브리지 미연결'
  else if (!enabled) status = '레이더 사용 안 함'
  else if (!r.connected) status = '레이더 포트 없음'
  else if (!r.receiving) status = '레이더 수신 없음'
  else if (!hold) status = '압박 중에는 표시하지 않음'
  else status = '레이더 수신 중'

  return {
    enabled,
    live,
    status,
    rr: per(r.rr_bpm),
    hr: per(r.hr_bpm),
    eapd: live && num(r.eapd_mm) && r.eapd_mm > 0 ? r.eapd_mm : null,
    distance: live && num(r.distance_mm) && r.distance_mm > 0 ? r.distance_mm : null,
    floorSet: !!r.floor_set,
  }
}
