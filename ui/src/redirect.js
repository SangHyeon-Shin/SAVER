/* 장치 플래그에 따른 목적지 변경 판정 (protocol/state_machine_v2.json 의 redirect).
   Jetson jetson/saver/state_machine.py 의 _redirect() 와 같은 규칙입니다.
   - flags 가 없으면(null, undefined) 장치가 플래그를 보내지 않는 것(v1.0)이므로 검사하지 않는다.
   - flags 안의 값이 없거나 null 이면 null 로 보고 목록과 비교한다(오래됨 = 안전 쪽).
   규칙 자체는 JSON 에만 둡니다. 이 파일은 해석만 합니다. */
export function redirectTarget(rule, flags) {
  if (!rule || flags == null) return null
  const hit = Object.entries(rule.when_mcu || {}).some(([k, vals]) => {
    const v = flags[k] === undefined ? null : flags[k]
    return vals.includes(v)
  })
  return hit ? rule.to : null
}
