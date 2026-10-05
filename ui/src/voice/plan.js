/* 음성 재생 규칙 (prompts.json → 재생할 ID 순서)
   - when=enter    : 그 화면에 들어갈 때 1회
   - when=after:Vxx: Vxx 가 끝난 뒤 이어서
   - 그 밖(when)   : 해당 사건 때 1회 (node '*' 는 모든 화면)
   - 압박 중(S7)   : 진입 1회만. 사건 음성 없음 (119 상담원 음성과 겹침 방지)
   - AED 화면(X_AED): 진입 1회 뒤 장치 음성을 끈다. 압박 멈춘 시간 경고(aed_pause_warn)만 예외
   - hold 문구(의학 검토 전 보류)는 재생하지 않는다
   순수 함수만 둔다. 시험: scripts/smoke-test.cjs                                  */

/** 첫 문구들 뒤에 after: 로 이어지는 문구를 붙인다 (중복, 순환 방지) */
export function chain(prompts, first) {
  const out = []
  const add = (p) => {
    if (out.includes(p.id)) return
    out.push(p.id)
    prompts.filter((q) => q.when === 'after:' + p.id).forEach(add)
  }
  first.forEach(add)
  const held = new Set(prompts.filter((p) => p.hold).map((p) => p.id))
  return out.filter((id) => !held.has(id))
}

/** 화면 진입. opt.cause: S10 원인(tubing·vacuum·position·search), opt.positionError: S3S4 위치 오류 */
export function planEnter(prompts, node, opt = {}) {
  let first
  if (node === 'S3S4' && opt.positionError) first = prompts.filter((p) => p.node === node && p.when === 'position_error')
  else first = prompts.filter((p) => p.node === node && p.when === 'enter')
  if (node === 'S10' && opt.cause) first = first.concat(prompts.filter((p) => p.node === 'S10' && p.when === 'cause:' + opt.cause))
  return chain(prompts, first)
}

/** 화면 안 사건 (search_start, search_fail:EAPD_MISSING, step:3, battery_low, aed_pause_warn …) */
export function planEvent(prompts, node, when) {
  if (node === 'S7') return []
  if (node === 'X_AED' && when !== 'aed_pause_warn') return []
  return chain(prompts, prompts.filter((p) => (p.node === node || p.node === '*') && p.when === when))
}
